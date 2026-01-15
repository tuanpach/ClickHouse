import json
import base64
from dataclasses import asdict, dataclass, field
from typing import Any, Dict, List, Optional


class LaunchTemplate:

    @dataclass
    class Config:
        # Launch Template name
        name: str
        region: str = ""

        # High-level fields (optional). If `data` is provided, it is used as-is.
        image_id: str = ""
        instance_type: str = ""
        # If set, will be base64-encoded and applied as LaunchTemplateData.UserData
        user_data: str = ""
        security_group_ids: List[str] = field(default_factory=list)
        tenancy: str = ""  # e.g. "host"
        # For scalable macOS fleets prefer HostResourceGroupArn over pinning a single HostId.
        host_resource_group_arn: str = ""
        host_resource_group_name: str = ""
        host_id: str = ""

        # Raw launch template data (passed directly to EC2 API as LaunchTemplateData)
        data: Dict[str, Any] = field(default_factory=dict)

        # If set, update will create a new version; if False, existing LT must not exist
        create_new_version: bool = True

        # Extra fetched/derived properties
        ext: Dict[str, Any] = field(default_factory=dict)

        def dump(self):
            """
            Dump launch template config as json into /tmp/launch_template_{name}.json
            """
            output_path = f"/tmp/launch_template_{self.name}.json"

            with open(output_path, "w") as f:
                json.dump(asdict(self), f, indent=2, default=str)

            print(f"LaunchTemplate config dumped to: {output_path}")

        def fetch(self):
            """
            Fetch Launch Template configuration from AWS and store in ext.

            Raises:
                Exception: If launch template does not exist or AWS API call fails
            """
            import boto3

            ec2 = boto3.client("ec2", region_name=self.region)

            resp = ec2.describe_launch_templates(LaunchTemplateNames=[self.name])
            lts = resp.get("LaunchTemplates", [])
            if not lts:
                raise Exception(f"Launch Template '{self.name}' not found in AWS")

            lt = lts[0]

            self.ext["launch_template_id"] = lt.get("LaunchTemplateId")
            self.ext["launch_template_name"] = lt.get("LaunchTemplateName")
            self.ext["latest_version_number"] = lt.get("LatestVersionNumber")
            self.ext["default_version_number"] = lt.get("DefaultVersionNumber")
            self.ext["created_time"] = lt.get("CreateTime")

            print(f"Successfully fetched configuration for Launch Template: {self.name}")
            return self

        def _resolve_launch_template_id(self) -> str:
            if self.ext.get("launch_template_id"):
                return self.ext["launch_template_id"]
            self.fetch()
            if not self.ext.get("launch_template_id"):
                raise Exception(f"Failed to resolve Launch Template id for '{self.name}'")
            return self.ext["launch_template_id"]

        def _build_launch_template_data(self) -> Dict[str, Any]:
            if self.data:
                return self.data

            if not self.image_id or not self.instance_type:
                raise ValueError(
                    f"Either data must be provided, or image_id + instance_type must be set for Launch Template '{self.name}'"
                )

            lt_data: Dict[str, Any] = {
                "ImageId": self.image_id,
                "InstanceType": self.instance_type,
            }

            if self.security_group_ids:
                lt_data["SecurityGroupIds"] = list(self.security_group_ids)

            if self.user_data:
                lt_data["UserData"] = base64.b64encode(
                    self.user_data.encode("utf-8")
                ).decode("utf-8")

            if self.tenancy:
                lt_data.setdefault("Placement", {})
                lt_data["Placement"]["Tenancy"] = self.tenancy

            if self.host_id:
                lt_data["Placement"] = {
                    "Tenancy": "host",
                    "HostId": self.host_id,
                }
                return lt_data

            host_rg_arn = self.host_resource_group_arn
            if not host_rg_arn and self.host_resource_group_name:
                import boto3

                rg = boto3.client("resource-groups", region_name=self.region)
                group_resp = rg.get_group(GroupName=self.host_resource_group_name)
                group = group_resp.get("Group", {})
                host_rg_arn = group.get("GroupArn", "")
                if not host_rg_arn:
                    raise Exception(
                        f"Failed to resolve GroupArn for resource group '{self.host_resource_group_name}'"
                    )

            if host_rg_arn:
                lt_data["Placement"] = {
                    "Tenancy": "host",
                    "HostResourceGroupArn": host_rg_arn,
                }

            return lt_data

        def deploy(self):
            """
            Create or update (create new version) an EC2 Launch Template.

            Notes:
                - This component expects `data` to be a valid EC2 LaunchTemplateData dict.
                - It intentionally does not attempt to diff/merge existing template data.
            """
            import boto3

            launch_template_data = self._build_launch_template_data()

            ec2 = boto3.client("ec2", region_name=self.region)

            # Determine if LT exists
            exists = False
            try:
                self.fetch()
                exists = True
                print(f"Fetched existing configuration for Launch Template: {self.name}")
            except Exception:
                print(f"Launch Template {self.name} does not exist yet, will create new")

            if not exists:
                resp = ec2.create_launch_template(
                    LaunchTemplateName=self.name,
                    LaunchTemplateData=launch_template_data,
                )
                lt = resp.get("LaunchTemplate", {})
                self.ext["launch_template_id"] = lt.get("LaunchTemplateId")
                self.ext["latest_version_number"] = lt.get("LatestVersionNumber")
                self.ext["default_version_number"] = lt.get("DefaultVersionNumber")
                print(f"Successfully created Launch Template: {self.name}")
                return self

            # Exists
            if not self.create_new_version:
                raise ValueError(
                    f"Launch Template '{self.name}' already exists and create_new_version=False"
                )

            lt_id = self._resolve_launch_template_id()

            resp = ec2.create_launch_template_version(
                LaunchTemplateId=lt_id,
                LaunchTemplateData=launch_template_data,
            )

            version = resp.get("LaunchTemplateVersion", {})
            new_version_number: Optional[int] = version.get("VersionNumber")
            if new_version_number is not None:
                self.ext["latest_version_number"] = new_version_number

            print(
                f"Successfully created new version for Launch Template: {self.name} (version={new_version_number})"
            )
            return self
