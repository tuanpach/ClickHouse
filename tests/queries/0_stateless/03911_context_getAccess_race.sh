#!/usr/bin/env bash
# Tags: race

# Test for data race in Context::getAccess() where need_recalculate_access
# was written under a shared lock while being read by another thread.
# The race is triggered by concurrent queries that perform access checks
# from multiple pipeline threads, e.g. via recursive CTEs.

CURDIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=../shell_config.sh
. "$CURDIR"/../shell_config.sh

$CLICKHOUSE_CLIENT -q "DROP TABLE IF EXISTS t_race_access"
$CLICKHOUSE_CLIENT -q "CREATE TABLE t_race_access (id UInt64, parent_id UInt64, name String) ENGINE = MergeTree ORDER BY id"
$CLICKHOUSE_CLIENT -q "INSERT INTO t_race_access SELECT number, number + 1, toString(number) FROM numbers(100)"

TIMEOUT=10

function thread_recursive_cte()
{
    local TIMELIMIT=$((SECONDS+TIMEOUT))
    while [ $SECONDS -lt "$TIMELIMIT" ]; do
        $CLICKHOUSE_CLIENT -q "
            WITH RECURSIVE cte AS (
                SELECT id, parent_id, name FROM t_race_access WHERE id = 0
                UNION ALL
                SELECT t.id, t.parent_id, t.name
                FROM t_race_access t
                INNER JOIN cte ON t.id = cte.parent_id
                WHERE cte.parent_id < 50
            )
            SELECT count() FROM cte FORMAT Null
        " 2>&1 | grep -v -e "^$" || true
    done
}

function thread_select()
{
    local TIMELIMIT=$((SECONDS+TIMEOUT))
    while [ $SECONDS -lt "$TIMELIMIT" ]; do
        $CLICKHOUSE_CLIENT -q "SELECT count() FROM t_race_access WHERE id IN (SELECT id FROM t_race_access) FORMAT Null" 2>&1 | grep -v -e "^$" || true
    done
}

for _ in $(seq 1 4); do
    thread_recursive_cte &
done

for _ in $(seq 1 2); do
    thread_select &
done

wait

$CLICKHOUSE_CLIENT -q "DROP TABLE t_race_access"

echo "OK"
