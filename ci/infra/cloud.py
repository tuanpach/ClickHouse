from praktika import CloudInfrastructure
from praktika.infrastructure.dedicated_host import DedicatedHost
from praktika.infrastructure.ec2_instance import EC2Instance
from praktika.infrastructure.iam_instance_profile import IAMInstanceProfile

# from praktika.infrastructure.autoscaling_group import AutoScalingGroup
# from praktika.infrastructure.launch_template import LaunchTemplate
# from praktika.infrastructure.resource_group import ResourceGroup

MAC_OS_TAHOE_IMAGE_AMI = "ami-0f8ce53a93ab42329"
MAC_HOST_RESOURCE_GROUP_NAME = "praktika-mac-hosts"
MAC_HOST_POOL_NAME = "praktika-mac-hosts-us-east-1"
MAC_VPC_NAME = "ci-cd"
MAC_SECURITY_GROUP_IDS = ["sg-061fd9184274476c0"]

PRAKTIKA_EC2_ROLE_NAME = "praktika-ec2-role"
PRAKTIKA_EC2_INSTANCE_PROFILE_NAME = "praktika-ec2-instance-profile"

CLOUD = CloudInfrastructure.Config(
    name="cloud_ci_infra",
    lambda_functions=[*CloudInfrastructure.SLACK_APP_LAMBDAS],
    iam_instance_profiles=[
        IAMInstanceProfile.Config(
            name="praktika_runner",
            role_name=PRAKTIKA_EC2_ROLE_NAME,
            instance_profile_name=PRAKTIKA_EC2_INSTANCE_PROFILE_NAME,
            policy_arns=[
                "arn:aws:iam::aws:policy/AmazonS3FullAccess",
                "arn:aws:iam::aws:policy/AmazonSSMManagedInstanceCore",
                "arn:aws:iam::aws:policy/AmazonSSMReadOnlyAccess",
                "arn:aws:iam::aws:policy/AutoScalingFullAccess",
                "arn:aws:iam::aws:policy/CloudWatchAgentServerPolicy",
                "arn:aws:iam::aws:policy/AmazonEC2FullAccess",
            ],
        )
    ],
    dedicated_hosts=[
        DedicatedHost.Config(
            name=MAC_HOST_POOL_NAME,
            host_resource_group_name=MAC_HOST_RESOURCE_GROUP_NAME,
            availability_zones=["us-east-1a"],
            instance_type="mac2-m2.metal",
            auto_placement="on",
            quantity_per_az=2,
            tags={
                "praktika": "mac",
            },
        )
    ],
    ec2_instances=[
        EC2Instance.Config(
            name="praktika-mac-single-instance-2",
            image_id=MAC_OS_TAHOE_IMAGE_AMI,
            instance_type="mac2-m2.metal",
            subnet_id="subnet-0a3886c4db842da5b",
            security_group_ids=[
                "sg-061fd9184274476c0",
                "sg-04d02f0265bee8b24",
            ],
            iam_instance_profile_name=PRAKTIKA_EC2_INSTANCE_PROFILE_NAME,
            key_name="awswork",
            user_data="",
            root_volume_type="gp3",
            root_volume_size=100,
            root_volume_encrypted=True,
            tenancy="host",
            tags={
                "praktika": "mac",
                "praktika_host_pool": MAC_HOST_POOL_NAME,
                "github:runner-type": "arm_macos_small",
            },
        ),
    ],
    # launch_templates=[
    #     LaunchTemplate.Config(
    #         name="praktika-hello-world-lt",
    #         image_id=MAC_OS_TAHOE_IMAGE_AMI,
    #         instance_type="mac2-m2.metal",
    #         security_group_ids=MAC_SECURITY_GROUP_IDS,
    #         set_default_version_to_latest=True,
    #         tenancy="host",
    #         user_data="#!/bin/bash\necho hello-world\n",
    #     )
    # ],
    # autoscaling_groups=[
    #     AutoScalingGroup.Config(
    #         name="praktika-mac-asg",
    #         vpc_name=MAC_VPC_NAME,
    #         availability_zones=["us-east-1a"],
    #         min_size=1,
    #         max_size=1,
    #         desired_capacity=1,
    #         launch_template_name="praktika-hello-world-lt",
    #         launch_template_version="$Latest",
    #     )
    # ],
)
