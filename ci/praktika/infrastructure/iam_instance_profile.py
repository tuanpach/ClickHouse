import json
from dataclasses import asdict, dataclass, field
from typing import Any, Dict, List, Optional


class IAMInstanceProfile:

    @dataclass
    class Config:
        name: str
        region: str = ""

        role_name: str = ""
        instance_profile_name: str = ""
        policy_arns: List[str] = field(default_factory=list)
        tags: Dict[str, str] = field(default_factory=dict)

        ext: Dict[str, Any] = field(default_factory=dict)

        def deploy(self):
            import time

            import boto3

            if not self.role_name:
                raise ValueError(
                    f"role_name must be set for IAMInstanceProfile '{self.name}'"
                )
            if not self.instance_profile_name:
                raise ValueError(
                    f"instance_profile_name must be set for IAMInstanceProfile '{self.name}'"
                )

            iam = boto3.client("iam")

            assume_role_policy = {
                "Version": "2012-10-17",
                "Statement": [
                    {
                        "Effect": "Allow",
                        "Principal": {"Service": "ec2.amazonaws.com"},
                        "Action": "sts:AssumeRole",
                    }
                ],
            }

            try:
                role = iam.get_role(RoleName=self.role_name).get("Role", {})
            except Exception:
                resp = iam.create_role(
                    RoleName=self.role_name,
                    AssumeRolePolicyDocument=json.dumps(assume_role_policy),
                    Tags=[{"Key": k, "Value": v} for k, v in (self.tags or {}).items()],
                )
                role = resp.get("Role", {})

            role_arn = role.get("Arn", "")
            if role_arn:
                self.ext["role_arn"] = role_arn

            for policy_arn in self.policy_arns or []:
                if not policy_arn:
                    continue
                try:
                    iam.attach_role_policy(
                        RoleName=self.role_name, PolicyArn=policy_arn
                    )
                except Exception as e:
                    print(
                        f"Warning: Failed to attach policy {policy_arn} to {self.role_name}: {e}"
                    )

            try:
                ip = iam.get_instance_profile(
                    InstanceProfileName=self.instance_profile_name
                ).get("InstanceProfile", {})
            except Exception:
                resp = iam.create_instance_profile(
                    InstanceProfileName=self.instance_profile_name,
                    Tags=[{"Key": k, "Value": v} for k, v in (self.tags or {}).items()],
                )
                ip = resp.get("InstanceProfile", {})

            ip_arn = ip.get("Arn", "")
            if ip_arn:
                self.ext["instance_profile_arn"] = ip_arn

            role_names = [
                r.get("RoleName") for r in (ip.get("Roles") or []) if r.get("RoleName")
            ]
            if self.role_name not in role_names:
                iam.add_role_to_instance_profile(
                    InstanceProfileName=self.instance_profile_name,
                    RoleName=self.role_name,
                )

            # IAM is eventually consistent. Wait until instance profile is visible.
            last_exc: Optional[Exception] = None
            for _ in range(30):
                try:
                    ip = iam.get_instance_profile(
                        InstanceProfileName=self.instance_profile_name
                    ).get("InstanceProfile", {})
                    if ip.get("InstanceProfileName"):
                        break
                except Exception as e:
                    last_exc = e
                time.sleep(2)
            else:
                if last_exc:
                    raise last_exc

            print(
                f"Successfully deployed IAM instance profile: {self.instance_profile_name} (role={self.role_name})"
            )
            return self
