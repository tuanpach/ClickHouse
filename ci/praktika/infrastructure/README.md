# Praktika Infrastructure Module

**Status**: Under development

This module provides classes for configuring and deploying cloud infrastructure components for Praktika CI/CD workflows.

## Module Structure

### Core Configuration Classes

#### [cloud.py](cloud.py)
Top-level infrastructure configuration class `CloudInfrastructure.Config`:
- Aggregates cloud resources (currently: Lambda functions, Auto Scaling Groups, and Resource Groups)
- Entry point for infrastructure definition and deployment orchestration

#### [lambda_function.py](lambda_function.py)
Lambda function configuration class `Lambda.Config`:
- Defines and deploys AWS Lambda functions used by Praktika (including optional native components)

#### [resource_group.py](resource_group.py)
Resource Group configuration class `ResourceGroup.Config`:
- Defines and deploys AWS Resource Groups (used to group Dedicated Hosts for macOS fleets)

#### [launch_template.py](launch_template.py)
Launch Template configuration class `LaunchTemplate.Config`:
- Defines and deploys AWS EC2 Launch Templates (used by ASGs)

#### [autoscaling_group.py](autoscaling_group.py)
Auto Scaling Group configuration class `AutoScalingGroup.Config`:
- Defines and deploys AWS EC2 Auto Scaling Groups (ASG) using an existing Launch Template

### Native Components

See [native/README.md](native/README.md) for pre-built cloud components provided by Praktika (e.g., Slack app integration).

## Usage

### Define Infrastructure

Create a cloud configuration file (e.g., `ci/infra/cloud.py`):

```python
from praktika import CloudInfrastructure

CLOUD = CloudInfrastructure.Config(
    name="my_cloud_infra",
    lambda_functions=[
        # Add your Lambda.Config instances here
        *CloudInfrastructure.SLACK_APP_LAMBDAS  # Optional: include native components
    ],
    resource_groups=[
        # Add your ResourceGroup.Config instances here
    ],
    launch_templates=[
        # Add your LaunchTemplate.Config instances here
    ],
    autoscaling_groups=[
        # Add your AutoScalingGroup.Config instances here
    ],
)
```

### Configure Settings

Set the cloud configuration path in your Praktika settings:

```python
from praktika import Settings

Settings.CLOUD_INFRASTRUCTURE_CONFIG_PATH = "./ci/infra/cloud.py"
Settings.AWS_REGION = "us-east-1"
Settings.EVENT_FEED_S3_PATH = "my-bucket/events"  # For Slack feed storage
```

### Deploy

Deploy your infrastructure to AWS:

```bash
praktika deploy
```

This command:
1. Loads configuration from `Settings.CLOUD_INFRASTRUCTURE_CONFIG_PATH`
2. Deploys all Resource Groups defined in `CLOUD.resource_groups`
3. Deploys all Launch Templates defined in `CLOUD.launch_templates`
4. Deploys all Auto Scaling Groups defined in `CLOUD.autoscaling_groups`
5. Deploys all Lambda functions defined in `CLOUD.lambda_functions`
6. Fetches secrets from AWS Parameter Store (for Lambda environment injection)

## macOS Auto Scaling Group (Dedicated Hosts)

To run macOS instances in an ASG you must use EC2 Dedicated Hosts.

Praktika configuration is split into three objects:

1. `DedicatedHost.Config`: allocates/maintains the required mac Dedicated Hosts.
2. `LaunchTemplate.Config`: defines how instances are launched (AMI, instance type, SGs, placement).
3. `AutoScalingGroup.Config`: defines scaling + networking and references the launch template.

Minimal working requirements for macOS ASG in Praktika:

- **Dedicated hosts are required**
  - Configure `DedicatedHost.Config(instance_type="mac2-m2.metal" | "mac2.metal")`.
  - Set `auto_placement="on"` so EC2 can place ASG-launched instances onto available hosts.
- **Launch template must use host tenancy**
  - Configure `LaunchTemplate.Config(tenancy="host")`.
  - Do not pin to a specific `host_id` for ASG usage.
- **ASG must be created in subnets (VPC)**
  - Either provide `subnet_ids`, or provide `vpc_id`/`vpc_name` and optionally `availability_zones` for discovery.
- **Start with desired capacity 0**
  - Use `min_size=0` and either omit `desired_capacity` or set it to `0`.

Example (macOS, Dedicated Hosts + ASG):

```python
from praktika import CloudInfrastructure
from praktika.infrastructure.dedicated_host import DedicatedHost
from praktika.infrastructure.launch_template import LaunchTemplate
from praktika.infrastructure.autoscaling_group import AutoScalingGroup

MAC_HOST_POOL_NAME = "praktika-mac-hosts-us-east-1"
MAC_VPC_NAME = "ci-cd"

CLOUD = CloudInfrastructure.Config(
    name="cloud_infra",
    dedicated_hosts=[
        DedicatedHost.Config(
            name=MAC_HOST_POOL_NAME,
            availability_zones=["us-east-1a"],
            instance_type="mac2-m2.metal",
            auto_placement="on",
            quantity_per_az=1,
            tags={"praktika": "mac"},
        )
    ],
    launch_templates=[
        LaunchTemplate.Config(
            name="praktika-mac-lt",
            image_id="ami-...",
            instance_type="mac2-m2.metal",
            security_group_ids=["sg-..."],
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
            launch_template_name="praktika-mac-lt",
        )
    ],
)
```

## Generic Auto Scaling Group configuration

`AutoScalingGroup.Config` is intentionally minimal: it manages the core set of ASG attributes needed for “runner-like” fleets.

Required fields:

- `name`
- `launch_template_id` or `launch_template_name`
- Networking: either `subnet_ids` or `vpc_id`/`vpc_name` (with optional `availability_zones` for subnet discovery)

Field reference (`AutoScalingGroup.Config`):

- **`name`**: ASG name.
- **`region`**: AWS region. Typically inherited from `Settings.AWS_REGION` by `CloudInfrastructure`.
- **`subnet_ids`**: Explicit subnet IDs for the ASG. If provided, no discovery is performed.
- **`vpc_id`**: Used for subnet discovery when `subnet_ids` is empty.
- **`vpc_name`**: VPC `Name` tag value used for subnet discovery when `vpc_id` is empty.
- **`availability_zones`**: Optional AZ filter used during subnet discovery (filters subnets by `availability-zone`).
- **`min_size` / `max_size` / `desired_capacity`**: Capacity settings. If `desired_capacity` is not set, Praktika uses `min_size`.
- **`health_check_type`**: `EC2` or `ELB`.
- **`health_check_grace_period_sec`**: Grace period for health checks.
- **`launch_template_id` / `launch_template_name`**: Which LT to use.
- **`launch_template_version`**: LT version string, default `$Latest`.
- **`target_group_arns`**: Optional list of ALB/NLB target group ARNs.
- **`tags`**: Dict of tags propagated to launched instances (`PropagateAtLaunch=True`).
- **`ext`**: Runtime/fetched fields (ARNs, resolved subnets, etc.). Not meant to be configured manually.

Related `LaunchTemplate.Config` fields you will typically set:

- **`image_id`** and **`instance_type`** (or provide raw `data`).
- **`security_group_ids`**.
- **`user_data`**.
- **`tenancy`** (for Dedicated Hosts use-cases).

## Roadmap

The long-term goal is to provide functionality for configuring and deploying complete cloud CI/CD infrastructure from scratch, enabling teams to provision their entire workflow environment declaratively.

### Future Configuration Classes

- **S3Bucket.Config**: S3 bucket creation and lifecycle management
- **Policy.Config**: IAM policies attachable to Lambdas, roles, etc.
- **Image.Config / ImageBuilder.Config**: Container image or AMI configuration
