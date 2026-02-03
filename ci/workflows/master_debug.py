from praktika import Workflow

from ci.defs.defs import BASE_BRANCH, DOCKERS, SECRETS, ArtifactConfigs
from ci.defs.job_configs import JobConfigs
from ci.jobs.scripts.workflow_hooks.filter_job import should_skip_job
from ci.workflows.pull_request import REGULAR_BUILD_NAMES

workflow = Workflow.Config(
    name="MasterCI",
    event=Workflow.Event.PULL_REQUEST,
    base_branches=[BASE_BRANCH],
    jobs=[
        *JobConfigs.tidy_build_arm_jobs,
        *JobConfigs.build_jobs,
        *JobConfigs.release_build_jobs,
        *[
            job.set_dependency(
                REGULAR_BUILD_NAMES + [JobConfigs.tidy_build_arm_jobs[0].name]
            )
            for job in JobConfigs.special_build_jobs
        ],
        *JobConfigs.buzz_fuzzer_jobs,
    ],
    artifacts=[
        *ArtifactConfigs.unittests_binaries,
        *ArtifactConfigs.clickhouse_binaries,
        *ArtifactConfigs.clickhouse_debians,
        *ArtifactConfigs.clickhouse_rpms,
        *ArtifactConfigs.clickhouse_tgzs,
        ArtifactConfigs.fuzzers,
        ArtifactConfigs.fuzzers_corpus,
        *ArtifactConfigs.llvm_profdata_file,
        ArtifactConfigs.llvm_coverage_html_report,
    ],
    dockers=DOCKERS,
    enable_dockers_manifest_merge=True,
    set_latest_for_docker_merged_manifest=True,
    secrets=SECRETS,
    enable_job_filtering_by_changes=True,
    enable_cache=True,
    enable_report=True,
    enable_cidb=True,
    enable_commit_status_on_failure=True,
    enable_slack_feed=True,
    pre_hooks=[
        "python3 ./ci/jobs/scripts/workflow_hooks/store_data.py",
        "python3 ./ci/jobs/scripts/workflow_hooks/version_log.py",
        "python3 ./ci/jobs/scripts/workflow_hooks/merge_sync_pr.py",
    ],
    workflow_filter_hooks=[should_skip_job],
    post_hooks=[],
)

WORKFLOWS = [
    workflow,
]
