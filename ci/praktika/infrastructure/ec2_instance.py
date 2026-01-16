import json
from dataclasses import dataclass, field
from typing import Any, Dict, List, Optional


class EC2Instance:

    @dataclass
    class Config:
        # Stable logical name (used for discovery via tags)
        name: str
        region: str = ""

        # AMI + instance type
        image_id: str = ""
        instance_type: str = ""

        # Networking
        subnet_id: str = ""
        security_group_ids: List[str] = field(default_factory=list)

        # IAM
        iam_instance_profile_name: str = ""

        # Misc
        key_name: str = ""
        user_data: str = ""

        # Root volume (EBS) settings (optional)
        root_device_name: str = ""  # if empty, resolved from AMI
        root_volume_size: int = 0
        root_volume_type: str = ""  # e.g. gp3
        root_volume_encrypted: bool = False

        # Placement
        tenancy: str = ""  # e.g. "host"
        host_id: str = ""
        host_resource_group_name: str = ""

        # Desired behavior
        start_on_deploy: bool = True

        # Tags applied to the instance
        tags: Dict[str, str] = field(default_factory=dict)

        # Extra fetched/derived properties
        ext: Dict[str, Any] = field(default_factory=dict)

        def _merged_tags(self) -> Dict[str, str]:
            merged = {"Name": self.name}
            merged.update(self.tags or {})
            return merged

        def _resolve_host_resource_group_arn(self) -> str:
            if self.ext.get("host_resource_group_arn"):
                return self.ext["host_resource_group_arn"]

            if not self.host_resource_group_name:
                return ""

            import boto3

            rg = boto3.client("resource-groups", region_name=self.region)
            resp = rg.get_group(GroupName=self.host_resource_group_name)
            group = resp.get("Group") or {}
            arn = group.get("GroupArn", "")
            if not arn:
                raise Exception(
                    f"Failed to resolve GroupArn for Resource Group '{self.host_resource_group_name}'"
                )
            self.ext["host_resource_group_arn"] = arn
            return arn

        def _resolve_root_device_name(self) -> str:
            if self.root_device_name:
                return self.root_device_name
            if not self.image_id:
                return ""

            import boto3

            ec2 = boto3.client("ec2", region_name=self.region)
            resp = ec2.describe_images(ImageIds=[self.image_id])
            images = resp.get("Images", []) or []
            root = (images[0] if images else {}).get("RootDeviceName", "")
            if root:
                self.root_device_name = root
            return self.root_device_name

        def _desired_placement(self) -> Dict[str, Any]:
            placement: Dict[str, Any] = {}

            if self.tenancy:
                placement["Tenancy"] = self.tenancy

            if self.host_id:
                placement["Tenancy"] = "host"
                placement["HostId"] = self.host_id
                return placement

            host_rg_arn = self._resolve_host_resource_group_arn()
            if host_rg_arn:
                placement["Tenancy"] = "host"
                placement["HostResourceGroupArn"] = host_rg_arn

            return placement

        def _find_existing_instance(self) -> Optional[Dict[str, Any]]:
            import boto3

            ec2 = boto3.client("ec2", region_name=self.region)

            filters = [
                {
                    "Name": "instance-state-name",
                    "Values": ["pending", "running", "stopping", "stopped"],
                },
                {"Name": "tag:Name", "Values": [self.name]},
            ]

            resp = ec2.describe_instances(Filters=filters)
            reservations = resp.get("Reservations", []) or []
            instances: List[Dict[str, Any]] = []
            for r in reservations:
                for inst in r.get("Instances", []) or []:
                    if inst.get("InstanceId"):
                        instances.append(inst)

            if not instances:
                return None

            if len(instances) > 1:
                ids = [i.get("InstanceId") for i in instances if i.get("InstanceId")]
                raise Exception(
                    f"More than one EC2 instance matched Name={self.name}. Delete duplicates to make Name unique. Matched: {ids}"
                )

            instances.sort(key=lambda i: i.get("LaunchTime") or 0, reverse=True)
            return instances[0]

        def fetch(self):
            inst = self._find_existing_instance()
            if not inst:
                raise Exception(f"EC2 Instance '{self.name}' not found in AWS")

            self.ext["instance_id"] = inst.get("InstanceId")
            self.ext["state"] = (inst.get("State") or {}).get("Name")
            self.ext["private_ip"] = inst.get("PrivateIpAddress")
            self.ext["public_ip"] = inst.get("PublicIpAddress")
            self.ext["launch_time"] = inst.get("LaunchTime")
            return self

        def deploy(self):
            import boto3

            if not self.image_id or not self.instance_type:
                raise ValueError(
                    f"image_id and instance_type must be set for EC2Instance '{self.name}'"
                )

            existing = self._find_existing_instance()
            if existing:
                instance_id = existing.get("InstanceId")
                state = (existing.get("State") or {}).get("Name")
                self.ext["instance_id"] = instance_id
                self.ext["state"] = state

                if instance_id:
                    merged_tags = self._merged_tags()
                    ec2 = boto3.client("ec2", region_name=self.region)
                    ec2.create_tags(
                        Resources=[instance_id],
                        Tags=[{"Key": k, "Value": v} for k, v in merged_tags.items()],
                    )
                    print(
                        f"EC2Instance '{self.name}': ensured {len(merged_tags)} tag(s) on existing instance {instance_id}"
                    )

                print(
                    f"EC2Instance '{self.name}': found existing instance {instance_id} (state={state}) - skip create"
                )

                if self.start_on_deploy and state == "stopped" and instance_id:
                    ec2 = boto3.client("ec2", region_name=self.region)
                    print(f"EC2Instance '{self.name}': starting instance {instance_id}")
                    ec2.start_instances(InstanceIds=[instance_id])

                return self

            ec2 = boto3.client("ec2", region_name=self.region)

            req: Dict[str, Any] = {
                "ImageId": self.image_id,
                "InstanceType": self.instance_type,
                "MinCount": 1,
                "MaxCount": 1,
            }

            if self.subnet_id:
                req["SubnetId"] = self.subnet_id

            if self.security_group_ids:
                req["SecurityGroupIds"] = list(self.security_group_ids)

            if self.iam_instance_profile_name:
                req["IamInstanceProfile"] = {"Name": self.iam_instance_profile_name}

            if self.key_name:
                req["KeyName"] = self.key_name

            if self.user_data:
                req["UserData"] = self.user_data

            if (
                self.root_volume_size
                or self.root_volume_type
                or self.root_volume_encrypted
            ):
                device_name = self._resolve_root_device_name()
                if not device_name:
                    raise ValueError(
                        f"Failed to resolve root_device_name for EC2Instance '{self.name}' (image_id={self.image_id})"
                    )

                ebs: Dict[str, Any] = {}
                if self.root_volume_size:
                    ebs["VolumeSize"] = int(self.root_volume_size)
                if self.root_volume_type:
                    ebs["VolumeType"] = self.root_volume_type
                if self.root_volume_encrypted:
                    ebs["Encrypted"] = True

                req["BlockDeviceMappings"] = [
                    {
                        "DeviceName": device_name,
                        "Ebs": ebs,
                    }
                ]

            placement = self._desired_placement()
            if placement:
                req["Placement"] = placement

            merged_tags = self._merged_tags()
            req["TagSpecifications"] = [
                {
                    "ResourceType": "instance",
                    "Tags": [{"Key": k, "Value": v} for k, v in merged_tags.items()],
                }
            ]

            print(
                f"EC2Instance '{self.name}': RunInstances request: {json.dumps(req, default=str)}"
            )
            resp = ec2.run_instances(**req)
            instances = resp.get("Instances", []) or []
            instance_id = (instances[0] if instances else {}).get("InstanceId")
            if not instance_id:
                raise Exception(
                    f"EC2Instance '{self.name}': failed to get InstanceId from RunInstances response"
                )

            self.ext["instance_id"] = instance_id
            self.ext["state"] = (
                (instances[0].get("State") or {}).get("Name") if instances else None
            )

            if not self.start_on_deploy:
                print(
                    f"EC2Instance '{self.name}': stopping instance {instance_id} (start_on_deploy=False)"
                )
                ec2.stop_instances(InstanceIds=[instance_id])

            print(f"EC2Instance '{self.name}': launched instance {instance_id}")
            return self
