#include <Core/Settings.h>
#include <DataTypes/DataTypeEnum.h>
#include <DataTypes/DataTypeNullable.h>
#include <DataTypes/DataTypeString.h>
#include <DataTypes/DataTypesNumber.h>
#include <Databases/DatabaseReplicated.h>
#include <Interpreters/ReplicatedDatabaseQueryStatusSource.h>

namespace DB
{

namespace ErrorCodes
{
extern const int TIMEOUT_EXCEEDED;
}

ReplicatedDatabaseQueryStatusSource::ReplicatedDatabaseQueryStatusSource(
    const String & zk_node_path, ContextPtr context_, const Strings & hosts_to_wait)
    : DistributedQueryStatusSource(zk_node_path, getSampleBlock(), context_, hosts_to_wait, "ReplicatedDatabaseQueryStatusSource")
{
}

ExecutionStatus ReplicatedDatabaseQueryStatusSource::checkStatus(const String & host_id)
{
    /// Replicated database retries in case of error, it should not write error status.
#ifdef DEBUG_OR_SANITIZER_BUILD
    fs::path status_path = fs::path(node_path) / "finished" / host_id;
    return getExecutionStatus(status_path);
#else
    return ExecutionStatus{0};
#endif
}

NameSet ReplicatedDatabaseQueryStatusSource::getOfflineHosts(const NameSet & hosts_to_wait, const ZooKeeperPtr & zookeeper)
{
    fs::path replicas_path;
    if (node_path.ends_with('/'))
        replicas_path = fs::path(node_path).parent_path().parent_path().parent_path() / "replicas";
    else
        replicas_path = fs::path(node_path).parent_path().parent_path() / "replicas";

    Strings paths;
    Strings hosts_array;
    for (const auto & host : hosts_to_wait)
    {
        hosts_array.push_back(host);
        paths.push_back(replicas_path / host / "active");
    }

    NameSet offline;
    auto res = zookeeper->tryGet(paths);
    for (size_t i = 0; i < res.size(); ++i)
        if (res[i].error == Coordination::Error::ZNONODE)
            offline.insert(hosts_array[i]);

    if (offline.size() == hosts_to_wait.size())
    {
        /// Avoid reporting that all hosts are offline
        LOG_WARNING(log, "Did not find active hosts, will wait for all {} hosts. This should not happen often", offline.size());
        return {};
    }

    return offline;
}

Chunk ReplicatedDatabaseQueryStatusSource::generateChunkWithUnfinishedHosts() const
{
    NameSet unfinished_hosts = waiting_hosts;
    for (const auto & host_id : finished_hosts)
        unfinished_hosts.erase(host_id);

    NameSet active_hosts_set = NameSet{current_active_hosts.begin(), current_active_hosts.end()};

    /// Query is not finished on the rest hosts, so fill the corresponding rows with NULLs.
    MutableColumns columns = output.getHeader().cloneEmptyColumns();
    for (const String & host_id : unfinished_hosts)
    {
        size_t num = 0;
        auto [shard, replica] = DatabaseReplicated::parseFullReplicaName(host_id);
        columns[num++]->insert(shard);
        columns[num++]->insert(replica);
        if (active_hosts_set.contains(host_id))
            columns[num++]->insert(IN_PROGRESS);
        else
            columns[num++]->insert(QUEUED);

        columns[num++]->insert(unfinished_hosts.size());
        columns[num++]->insert(current_active_hosts.size());
    }
    return Chunk(std::move(columns), unfinished_hosts.size());
}

Strings ReplicatedDatabaseQueryStatusSource::getNodesToWait()
{
    String node_to_wait = "finished";
    if (context->getSettingsRef().database_replicated_enforce_synchronous_settings)
    {
        node_to_wait = "synced";
    }

    return {String(fs::path(node_path) / node_to_wait), String(fs::path(node_path) / "active")};
}

Chunk ReplicatedDatabaseQueryStatusSource::handleTimeoutExceeded()
{
    timeout_exceeded = true;

    size_t num_unfinished_hosts = waiting_hosts.size() - num_hosts_finished;
    size_t num_active_hosts = current_active_hosts.size();

    constexpr auto msg_format = "ReplicatedDatabase DDL task {} is not finished on {} of {} hosts "
                                "({} of them are currently executing the task, {} are inactive). "
                                "They are going to execute the query in background. Was waiting for {} seconds{}";

    if (throw_on_timeout || (throw_on_timeout_only_active && !stop_waiting_offline_hosts))
    {
        if (!first_exception)
            first_exception = std::make_unique<Exception>(Exception(
                ErrorCodes::TIMEOUT_EXCEEDED,
                msg_format,
                node_path,
                num_unfinished_hosts,
                waiting_hosts.size(),
                num_active_hosts,
                offline_hosts.size(),
                watch.elapsedSeconds(),
                stop_waiting_offline_hosts ? "" : ", which is longer than distributed_ddl_task_timeout"));

        /// For Replicated database print a list of unfinished hosts as well. Will return empty block on next iteration.
        return generateChunkWithUnfinishedHosts();
    }

    LOG_INFO(
        log,
        msg_format,
        node_path,
        num_unfinished_hosts,
        waiting_hosts.size(),
        num_active_hosts,
        offline_hosts.size(),
        watch.elapsedSeconds(),
        stop_waiting_offline_hosts ? "" : "which is longer than distributed_ddl_task_timeout");

    return generateChunkWithUnfinishedHosts();
}

Chunk ReplicatedDatabaseQueryStatusSource::stopWaitingOfflineHosts()
{
    // Same logic as timeout exceeded
    return handleTimeoutExceeded();
}

void ReplicatedDatabaseQueryStatusSource::handleNonZeroStatusCode(const ExecutionStatus & status, const String & host_id)
{
    assert(status.code != 0);

    if (!first_exception && context->getSettingsRef().distributed_ddl_output_mode != DistributedDDLOutputMode::NEVER_THROW)
    {
        throw Exception(ErrorCodes::LOGICAL_ERROR, "There was an error on {}: {} (probably it's a bug)", host_id, status.message);
    }
}

void ReplicatedDatabaseQueryStatusSource::fillHostStatus(const String & host_id, const ExecutionStatus & status, MutableColumns & columns)
{
    size_t num = 0;
    if (status.code != 0)
        throw Exception(ErrorCodes::LOGICAL_ERROR, "There was an error on {}: {} (probably it's a bug)", host_id, status.message);
    auto [shard, replica] = DatabaseReplicated::parseFullReplicaName(host_id);
    columns[num++]->insert(shard);
    columns[num++]->insert(replica);
    columns[num++]->insert(OK);
}

Block ReplicatedDatabaseQueryStatusSource::getSampleBlock()
{
    auto get_status_enum = []()
    {
        return std::make_shared<DataTypeEnum8>(DataTypeEnum8::Values{
            {"OK", static_cast<Int8>(OK)},
            {"IN_PROGRESS", static_cast<Int8>(IN_PROGRESS)},
            {"QUEUED", static_cast<Int8>(QUEUED)},
        });
    };

    return Block{
        {std::make_shared<DataTypeString>(), "shard"},
        {std::make_shared<DataTypeString>(), "replica"},
        {get_status_enum(), "status"},
        {std::make_shared<DataTypeUInt64>(), "num_hosts_remaining"},
        {std::make_shared<DataTypeUInt64>(), "num_hosts_active"},
    };
}

}
