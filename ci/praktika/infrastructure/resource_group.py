import json
from dataclasses import asdict, dataclass, field
from typing import Any, Dict, Optional


class ResourceGroup:

    @dataclass
    class Config:
        # Resource Group name
        name: str
        region: str = ""

        # Resource group query (passed to AWS Resource Groups API)
        # Typical usage: TAG_FILTERS_1_0 query to select Dedicated Hosts by tags.
        resource_query: Dict[str, Any] = field(default_factory=dict)

        # Optional metadata
        description: str = ""
        tags: Dict[str, str] = field(default_factory=dict)

        # Extra fetched/derived properties
        ext: Dict[str, Any] = field(default_factory=dict)

        def dump(self):
            """
            Dump resource group config as json into /tmp/resource_group_{name}.json
            """
            output_path = f"/tmp/resource_group_{self.name}.json"

            with open(output_path, "w") as f:
                json.dump(asdict(self), f, indent=2, default=str)

            print(f"ResourceGroup config dumped to: {output_path}")

        def fetch(self):
            """
            Fetch Resource Group configuration from AWS and store in ext.

            Raises:
                Exception: If resource group does not exist or AWS API call fails
            """
            import boto3

            rg = boto3.client("resource-groups", region_name=self.region)

            group_resp = rg.get_group(GroupName=self.name)
            group = group_resp.get("Group", {})

            query_resp = rg.get_group_query(GroupName=self.name)
            query = query_resp.get("GroupQuery", {})

            tags_resp = rg.get_tags(Arn=group.get("GroupArn", ""))
            tags = tags_resp.get("Tags", {})

            self.ext["group_arn"] = group.get("GroupArn")
            self.ext["group_name"] = group.get("Name")
            self.ext["description"] = group.get("Description", "")
            self.ext["resource_query"] = query.get("ResourceQuery")
            self.ext["tags"] = tags

            print(f"Successfully fetched configuration for Resource Group: {self.name}")
            return self

        def deploy(self):
            """
            Create or update a Resource Group.

            Notes:
                - This is intentionally a thin wrapper around AWS Resource Groups.
                - It updates the group query and tags if the group already exists.
            """
            import boto3

            if not isinstance(self.resource_query, dict) or not self.resource_query:
                raise ValueError(
                    f"resource_query must be a non-empty dict for Resource Group '{self.name}'"
                )

            rg = boto3.client("resource-groups", region_name=self.region)

            exists = False
            try:
                self.fetch()
                exists = True
                print(f"Fetched existing configuration for Resource Group: {self.name}")
            except Exception:
                print(f"Resource Group {self.name} does not exist yet, will create new")

            if not exists:
                resp = rg.create_group(
                    Name=self.name,
                    Description=self.description,
                    ResourceQuery=self.resource_query,
                    Tags=self.tags,
                )
                group = resp.get("Group", {})
                self.ext["group_arn"] = group.get("GroupArn")
                print(f"Successfully created Resource Group: {self.name}")
                return self

            # Update existing
            rg.update_group(
                GroupName=self.name,
                Description=self.description,
            )

            rg.update_group_query(
                GroupName=self.name,
                ResourceQuery=self.resource_query,
            )

            # Replace tags (best-effort): remove existing, then add desired
            group_arn: Optional[str] = self.ext.get("group_arn")
            if group_arn:
                try:
                    existing_tags = self.ext.get("tags") or {}
                    if existing_tags:
                        rg.untag(Arn=group_arn, Keys=list(existing_tags.keys()))
                except Exception as e:
                    print(f"Warning: Failed to untag Resource Group '{self.name}': {e}")

                if self.tags:
                    rg.tag(Arn=group_arn, Tags=self.tags)

            print(f"Successfully updated Resource Group: {self.name}")
            return self
