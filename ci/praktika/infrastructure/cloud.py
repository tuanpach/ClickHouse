from dataclasses import dataclass, field
from typing import TYPE_CHECKING, List, Optional

from ..settings import _Settings
from .autoscaling_group import AutoScalingGroup
from .dedicated_host import DedicatedHost
from .license_manager import LicenseManager
from .launch_template import LaunchTemplate
from .resource_group import ResourceGroup
from .lambda_function import lambda_app_config, lambda_worker_config

if TYPE_CHECKING:
    from .autoscaling_group import AutoScalingGroup
    from .dedicated_host import DedicatedHost
    from .license_manager import LicenseManager
    from .launch_template import LaunchTemplate
    from .resource_group import ResourceGroup
    from .lambda_function import Lambda


class CloudInfrastructure:
    SLACK_APP_LAMBDAS = [lambda_app_config, lambda_worker_config]

    @dataclass
    class Config:
        name: str
        lambda_functions: List["Lambda.Config"] = field(default_factory=list)
        dedicated_hosts: List["DedicatedHost.Config"] = field(default_factory=list)
        resource_groups: List["ResourceGroup.Config"] = field(default_factory=list)
        license_managers: List["LicenseManager.Config"] = field(default_factory=list)
        launch_templates: List["LaunchTemplate.Config"] = field(default_factory=list)
        autoscaling_groups: List["AutoScalingGroup.Config"] = field(default_factory=list)
        _settings: Optional[_Settings] = None

        def deploy(self, all=False, resource_groups_only: bool = False):
            """
            Deploy Lambda functions.

            Args:
                all: If False, only deploy code (skip settings validation and IAM policies).
                     If True, deploy everything (validate settings, deploy code, attach IAM policies).
                resource_groups_only: If True, deploy only resource groups and skip the rest.
            """
            if (
                not self.lambda_functions
                and not self.dedicated_hosts
                and not self.resource_groups
                and not self.license_managers
                and not self.launch_templates
                and not self.autoscaling_groups
            ):
                print("No infrastructure components to deploy")
                return

            # Full deployment mode: validate settings and configure environments
            if all:
                if not self._settings:
                    raise ValueError(
                        "Settings not configured. Please set _settings before deploying."
                    )

                required_settings = {
                    "EVENT_FEED_S3_PATH": self._settings.EVENT_FEED_S3_PATH,
                    "AWS_REGION": self._settings.AWS_REGION,
                }

                missing_settings = [
                    name for name, value in required_settings.items() if not value
                ]
                if missing_settings:
                    raise ValueError(
                        f"Missing required settings for Lambda deployment: {', '.join(missing_settings)}"
                    )

            # Deploy only resource groups (skip all other components)
            if resource_groups_only:
                for rg_config in self.resource_groups:
                    if self._settings and self._settings.AWS_REGION:
                        rg_config.region = self._settings.AWS_REGION

                    print("\n" + "=" * 60)
                    print(f"Deploying Resource Group: {rg_config.name}")
                    print("=" * 60)
                    rg_config.deploy()

                print("\n" + "=" * 60)
                print("Resource group deployment completed!")
                print("=" * 60)
                return

            # Deploy all Dedicated Hosts
            for host_config in self.dedicated_hosts:
                if self._settings and self._settings.AWS_REGION:
                    host_config.region = self._settings.AWS_REGION

                print("\n" + "=" * 60)
                print(f"Deploying Dedicated Hosts: {host_config.name}")
                print("=" * 60)
                host_config.deploy()

            # Deploy all Resource Groups
            for rg_config in self.resource_groups:
                if self._settings and self._settings.AWS_REGION:
                    rg_config.region = self._settings.AWS_REGION

                print("\n" + "=" * 60)
                print(f"Deploying Resource Group: {rg_config.name}")
                print("=" * 60)
                rg_config.deploy()

            # Deploy License Manager associations
            for lm_config in self.license_managers:
                if self._settings and self._settings.AWS_REGION:
                    lm_config.region = self._settings.AWS_REGION

                print("\n" + "=" * 60)
                print(f"Deploying License Manager: {lm_config.name}")
                print("=" * 60)
                lm_config.deploy()

            # Deploy all Launch Templates
            for lt_config in self.launch_templates:
                if self._settings and self._settings.AWS_REGION:
                    lt_config.region = self._settings.AWS_REGION

                print("\n" + "=" * 60)
                print(f"Deploying Launch Template: {lt_config.name}")
                print("=" * 60)
                lt_config.deploy()

            # Deploy all ASGs
            for asg_config in self.autoscaling_groups:
                if self._settings and self._settings.AWS_REGION:
                    asg_config.region = self._settings.AWS_REGION

                print("\n" + "=" * 60)
                print(f"Deploying Auto Scaling Group: {asg_config.name}")
                print("=" * 60)
                asg_config.deploy()

            # Deploy all Lambdas (code only or with configuration)
            for lambda_config in self.lambda_functions:
                # Always set region if available (needed even for code-only deploys)
                if self._settings and self._settings.AWS_REGION:
                    lambda_config.region = self._settings.AWS_REGION

                # Only set environment variables in full deployment mode
                if all and self._settings:
                    if self._settings.EVENT_FEED_S3_PATH:
                        # Inject project-specific settings into Lambda environment
                        # EVENT_FEED_S3_PATH is required by Slack Lambdas for event feed storage
                        lambda_config.environments["EVENT_FEED_S3_PATH"] = (
                            self._settings.EVENT_FEED_S3_PATH
                        )

                print("\n" + "=" * 60)
                print(f"Deploying Lambda: {lambda_config.name}")
                print("=" * 60)
                lambda_config.deploy()

            # Only attach IAM policies in full deployment mode
            if all:
                print("\n" + "=" * 60)
                print("Attaching IAM policies...")
                print("=" * 60)

                for lambda_config in self.lambda_functions:
                    role_arn = lambda_config.ext.get("role_arn")
                    if not role_arn:
                        print(
                            f"Warning: No role_arn found for {lambda_config.name}, skipping policy attachment"
                        )
                        continue

                    # Attach policies based on Lambda name patterns
                    if "worker" in lambda_config.name:
                        # Worker Lambda needs S3 read/write and CloudWatch access
                        lambda_config._attach_s3_readwrite_policy(role_arn)
                        lambda_config._attach_cloudwatch_logs_policy(role_arn)
                    elif "app" in lambda_config.name:
                        # App Lambda needs to invoke worker
                        worker_name = next(
                            (
                                lc.name
                                for lc in self.lambda_functions
                                if "worker" in lc.name
                            ),
                            None,
                        )
                        if worker_name:
                            lambda_config._attach_worker_invoke_policy(
                                role_arn, worker_name
                            )

            print("\n" + "=" * 60)
            print("Lambda deployment completed!")
            print("=" * 60)
