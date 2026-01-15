from praktika import CloudInfrastructure
from praktika.infrastructure.dedicated_host import DedicatedHost
from praktika.infrastructure.autoscaling_group import AutoScalingGroup
from praktika.infrastructure.launch_template import LaunchTemplate
from praktika.infrastructure.resource_group import ResourceGroup

MAC_OS_TAHOE_IMAGE_AMI="ami-0f8ce53a93ab42329"
MAC_HOST_RESOURCE_GROUP_NAME = "praktika-mac-hosts"
MAC_HOST_POOL_NAME = "praktika-mac-hosts-us-east-1"
MAC_VPC_NAME = "ci-cd"
MAC_SECURITY_GROUP_IDS = ["sg-061fd9184274476c0"]

CLOUD = CloudInfrastructure.Config(
    name="cloud_infra",
    lambda_functions=[*CloudInfrastructure.SLACK_APP_LAMBDAS],
    dedicated_hosts=[
        DedicatedHost.Config(
            name=MAC_HOST_POOL_NAME,
            availability_zones=["us-east-1a"],
            instance_type="mac2-m2.metal",
            auto_placement="on",
            quantity_per_az=1,
            tags={
                "praktika": "mac",
            },
        )
    ],
    resource_groups=[
        ResourceGroup.Config(
            name=MAC_HOST_RESOURCE_GROUP_NAME,
            resource_query={
                "Type": "TAG_FILTERS_1_0",
                "Query": "{\"ResourceTypeFilters\":[\"AWS::EC2::Host\"],\"TagFilters\":[{\"Key\":\"praktika\",\"Values\":[\"mac\"]}]}" ,
            },
        )
    ],
    launch_templates=[
        LaunchTemplate.Config(
            name="praktika-hello-world-lt",
            image_id=MAC_OS_TAHOE_IMAGE_AMI,
            instance_type="mac2-m2.metal",
            security_group_ids=MAC_SECURITY_GROUP_IDS,
            tenancy="host",
            user_data="#!/bin/bash\necho hello-world\n",
        )
    ],
    autoscaling_groups=[
        AutoScalingGroup.Config(
            name="praktika-mac-asg",
            vpc_name=MAC_VPC_NAME,
            availability_zones=["us-east-1a"],
            min_size=0,
            max_size=1,
            desired_capacity=0,
            launch_template_name="praktika-hello-world-lt",
            launch_template_version="$Latest",
        )
    ],
)
