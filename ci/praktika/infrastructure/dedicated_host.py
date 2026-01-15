import json
from dataclasses import asdict, dataclass, field
from typing import Any, Dict, List


class DedicatedHost:

    @dataclass
    class Config:
        # Logical pool name (used for tagging/identification)
        name: str
        region: str = ""

        # If set, allocate hosts in all availability zones in the region.
        all_availability_zones: bool = False
        availability_zones: List[str] = field(default_factory=list)

        # EC2 instance type for host allocation (mac hosts)
        instance_type: str = ""

        # Auto placement for Dedicated Hosts ("on" or "off")
        auto_placement: str = "off"

        # Desired host count per AZ
        quantity_per_az: int = 1

        # Tags applied to allocated hosts
        tags: Dict[str, str] = field(default_factory=dict)

        # Extra fetched/derived properties
        ext: Dict[str, Any] = field(default_factory=dict)

        def dump(self):
            output_path = f"/tmp/dedicated_host_{self.name}.json"
            with open(output_path, "w") as f:
                json.dump(asdict(self), f, indent=2, default=str)
            print(f"DedicatedHost config dumped to: {output_path}")

        def _resolved_availability_zones(self) -> List[str]:
            if self.availability_zones:
                return self.availability_zones
            if not self.all_availability_zones:
                raise ValueError(
                    f"Either availability_zones must be set or all_availability_zones=True for DedicatedHost '{self.name}'"
                )

            import boto3

            ec2 = boto3.client("ec2", region_name=self.region)
            resp = ec2.describe_availability_zones(
                Filters=[{"Name": "region-name", "Values": [self.region]}]
            )
            zones = [
                z["ZoneName"]
                for z in resp.get("AvailabilityZones", [])
                if z.get("State") == "available" and z.get("ZoneName")
            ]
            if not zones:
                raise Exception(f"No available AZs found for region '{self.region}'")
            return zones

        def _host_filters(self, az: str) -> List[Dict[str, Any]]:
            filters: List[Dict[str, Any]] = [
                {"Name": "availability-zone", "Values": [az]},
            ]

            if self.instance_type:
                filters.append({"Name": "instance-type", "Values": [self.instance_type]})

            # Stable identification tags
            merged_tags = {"praktika_host_pool": self.name, **(self.tags or {})}
            for k, v in merged_tags.items():
                filters.append({"Name": f"tag:{k}", "Values": [v]})

            return filters

        def fetch(self):
            import boto3

            ec2 = boto3.client("ec2", region_name=self.region)

            azs = self._resolved_availability_zones()
            hosts_by_az: Dict[str, List[str]] = {}

            for az in azs:
                resp = ec2.describe_hosts(Filters=self._host_filters(az))
                hosts = resp.get("Hosts", [])
                host_ids = [h.get("HostId") for h in hosts if h.get("HostId")]
                hosts_by_az[az] = host_ids

            self.ext["hosts_by_az"] = hosts_by_az
            print(f"Successfully fetched Dedicated Hosts for pool: {self.name}")
            return self

        def deploy(self):
            import boto3

            if not self.instance_type:
                raise ValueError(
                    f"instance_type must be set for DedicatedHost '{self.name}' (e.g. mac2-m2.metal)"
                )
            if self.quantity_per_az < 1:
                raise ValueError(
                    f"quantity_per_az must be >= 1 for DedicatedHost '{self.name}'"
                )

            ec2 = boto3.client("ec2", region_name=self.region)

            azs = self._resolved_availability_zones()

            # Make allocation idempotent-ish by counting existing tagged hosts
            self.fetch()
            hosts_by_az: Dict[str, List[str]] = self.ext.get("hosts_by_az", {})

            allocated_by_az: Dict[str, List[str]] = {}

            merged_tags = {"praktika_host_pool": self.name, **(self.tags or {})}

            for az in azs:
                existing = hosts_by_az.get(az, [])

                # Enforce desired auto placement on existing hosts
                if existing:
                    try:
                        ec2.modify_hosts(HostIds=existing, AutoPlacement=self.auto_placement)
                    except Exception as e:
                        print(
                            f"Warning: Failed to set AutoPlacement={self.auto_placement} for existing hosts in {az}: {e}"
                        )

                missing = self.quantity_per_az - len(existing)
                if missing <= 0:
                    print(
                        f"DedicatedHost pool '{self.name}': AZ {az} already has {len(existing)} host(s), need {self.quantity_per_az} - skip"
                    )
                    allocated_by_az[az] = []
                    continue

                print(
                    f"Allocating {missing} Dedicated Host(s) for pool '{self.name}' in {az} (instance_type={self.instance_type})"
                )

                resp = ec2.allocate_hosts(
                    AvailabilityZone=az,
                    InstanceType=self.instance_type,
                    Quantity=missing,
                    AutoPlacement=self.auto_placement,
                    TagSpecifications=[
                        {
                            "ResourceType": "dedicated-host",
                            "Tags": [
                                {"Key": k, "Value": v}
                                for k, v in merged_tags.items()
                            ],
                        }
                    ],
                )

                new_ids = resp.get("HostIds", [])
                allocated_by_az[az] = new_ids
                print(
                    f"Allocated {len(new_ids)} Dedicated Host(s) in {az} for pool '{self.name}': {new_ids}"
                )

            self.ext["allocated_by_az"] = allocated_by_az
            return self
