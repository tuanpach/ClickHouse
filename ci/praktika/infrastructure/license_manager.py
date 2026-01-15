import json
from dataclasses import asdict, dataclass, field
from typing import Any, Dict, List, Optional


class LicenseManager:

    @dataclass
    class Config:
        # Human name for this configuration in Praktika config
        name: str
        region: str = ""

        # License Manager License Configuration
        license_configuration_name: str = ""
        license_counting_type: str = "Core"  # Core | vCPU | Instance | Socket
        license_count: int = 1000
        license_count_hard_limit: bool = False

        # Host Resource Group (AWS Resource Groups)
        host_resource_group_arn: str = ""
        host_resource_group_name: str = ""

        # Dedicated Hosts to attach licenses to (preferred, idempotent-ish)
        dedicated_host_ids: List[str] = field(default_factory=list)
        dedicated_host_pool_name: str = ""  # praktika_host_pool tag value
        dedicated_host_tags: Dict[str, str] = field(default_factory=dict)

        # Extra fetched/derived properties
        ext: Dict[str, Any] = field(default_factory=dict)

        def dump(self):
            output_path = f"/tmp/license_manager_{self.name}.json"
            with open(output_path, "w") as f:
                json.dump(asdict(self), f, indent=2, default=str)
            print(f"LicenseManager config dumped to: {output_path}")

        def _resolve_group_arn(self) -> str:
            if self.host_resource_group_arn:
                return self.host_resource_group_arn
            if not self.host_resource_group_name:
                raise ValueError(
                    f"host_resource_group_arn or host_resource_group_name must be provided for LicenseManager '{self.name}'"
                )

            import boto3

            rg = boto3.client("resource-groups", region_name=self.region)
            resp = rg.get_group(GroupName=self.host_resource_group_name)
            group = resp.get("Group", {})
            arn = group.get("GroupArn", "")
            if not arn:
                raise Exception(
                    f"Failed to resolve GroupArn for resource group '{self.host_resource_group_name}'"
                )
            return arn

        def _resolve_dedicated_host_arns(self) -> List[str]:
            """Return EC2 Dedicated Host ARNs to attach LicenseConfiguration to."""
            if self.dedicated_host_ids:
                return [
                    f"arn:aws:ec2:{self.region}:{self._account_id()}:dedicated-host/{hid}"
                    for hid in self.dedicated_host_ids
                ]

            filters = []
            if self.dedicated_host_pool_name:
                filters.append(
                    {
                        "Name": "tag:praktika_host_pool",
                        "Values": [self.dedicated_host_pool_name],
                    }
                )
            for k, v in (self.dedicated_host_tags or {}).items():
                filters.append({"Name": f"tag:{k}", "Values": [v]})

            if not filters:
                raise ValueError(
                    f"Provide dedicated_host_ids, or dedicated_host_pool_name/tags to resolve Dedicated Hosts for LicenseManager '{self.name}'"
                )

            import boto3

            ec2 = boto3.client("ec2", region_name=self.region)
            resp = ec2.describe_hosts(Filters=filters)
            hosts = resp.get("Hosts", [])
            host_ids = [h.get("HostId") for h in hosts if h.get("HostId")]
            if not host_ids:
                raise Exception(
                    f"No Dedicated Hosts found for LicenseManager '{self.name}' using filters {filters}"
                )

            return [
                f"arn:aws:ec2:{self.region}:{self._account_id()}:dedicated-host/{hid}"
                for hid in host_ids
            ]

        def _account_id(self) -> str:
            if self.ext.get("account_id"):
                return self.ext["account_id"]

            import boto3

            sts = boto3.client("sts", region_name=self.region)
            account_id = sts.get_caller_identity().get("Account", "")
            if not account_id:
                raise Exception("Failed to resolve AWS account id via STS")
            self.ext["account_id"] = account_id
            return account_id

        def _get_or_create_license_configuration_arn(self) -> str:
            if not self.license_configuration_name:
                raise ValueError(
                    f"license_configuration_name must be set for LicenseManager '{self.name}'"
                )

            import boto3

            lm = boto3.client("license-manager", region_name=self.region)

            paginator = lm.get_paginator("list_license_configurations")
            for page in paginator.paginate():
                for lc in page.get("LicenseConfigurations", []) or []:
                    if lc.get("Name") == self.license_configuration_name:
                        arn = lc.get("LicenseConfigurationArn", "")
                        if arn:
                            return arn

            resp = lm.create_license_configuration(
                Name=self.license_configuration_name,
                LicenseCountingType=self.license_counting_type,
                LicenseCount=self.license_count,
                LicenseCountHardLimit=self.license_count_hard_limit,
            )
            arn = resp.get("LicenseConfigurationArn", "")
            if not arn:
                raise Exception(
                    f"Failed to create license configuration '{self.license_configuration_name}'"
                )
            return arn

        def deploy(self):
            """Create/fetch LicenseConfiguration and associate it with a host resource group."""
            import boto3

            license_arn = self._get_or_create_license_configuration_arn()

            lm = boto3.client("license-manager", region_name=self.region)

            host_arns = self._resolve_dedicated_host_arns()
            for host_arn in host_arns:
                lm.update_license_specifications_for_resource(
                    ResourceArn=host_arn,
                    AddLicenseSpecifications=[
                        {"LicenseConfigurationArn": license_arn},
                    ],
                )

            self.ext["dedicated_host_arns"] = host_arns
            self.ext["license_configuration_arn"] = license_arn

            print(
                f"Associated LicenseConfiguration '{self.license_configuration_name}' with {len(host_arns)} Dedicated Host(s)"
            )
            return self
