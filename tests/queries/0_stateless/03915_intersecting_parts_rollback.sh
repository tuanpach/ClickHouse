#!/usr/bin/env bash
# Tags: no-fasttest, no-replicated-database

# Creation of a database with Ordinary engine emits a warning.
CLICKHOUSE_CLIENT_SERVER_LOGS_LEVEL=fatal

CURDIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=../shell_config.sh
. "$CURDIR"/../shell_config.sh

# Test for a bug where failed merge transaction rollback left renamed parts on disk,
# causing intersecting parts on ATTACH TABLE.
# When implicit_transaction=1 is used with a table in Ordinary database,
# merge commit fails because Ordinary database doesn't support transactions.
# The merge result is already renamed from tmp to final name, so rollback
# must rename it back to a tmp prefix to avoid stale intersecting parts on disk.

db_name="${CLICKHOUSE_DATABASE}_ordinary_03915"

$CLICKHOUSE_CLIENT --allow_deprecated_database_ordinary=1 -q "CREATE DATABASE IF NOT EXISTS ${db_name} ENGINE=Ordinary"

$CLICKHOUSE_CLIENT -q "
    CREATE TABLE ${db_name}.test_intersect (x UInt64) ENGINE = MergeTree ORDER BY x
        SETTINGS merge_max_block_size = 1000
"

# Disable background merges while inserting parts to control part count
$CLICKHOUSE_CLIENT -q "SYSTEM STOP MERGES ${db_name}.test_intersect"

# Insert several parts (without implicit_transaction, so they succeed)
for i in $(seq 1 5); do
    $CLICKHOUSE_CLIENT -q "INSERT INTO ${db_name}.test_intersect SELECT number + ($i - 1) * 100 FROM numbers(100)"
done

# Resume merges so OPTIMIZE FINAL can actually start a merge.
# SYSTEM STOP MERGES prevents even explicit OPTIMIZE from working.
$CLICKHOUSE_CLIENT -q "SYSTEM START MERGES ${db_name}.test_intersect"

# OPTIMIZE with implicit_transaction=1 will trigger a merge, which renames tmp to final,
# but then commit fails with NOT_IMPLEMENTED because Ordinary database has no UUID.
# The fix renames the part back to tmp_ prefix on rollback.
$CLICKHOUSE_CLIENT --implicit_transaction=1 --throw_on_unsupported_query_inside_transaction=0 \
    -q "OPTIMIZE TABLE ${db_name}.test_intersect FINAL" 2>/dev/null ||:

# Stop merges again before inserting more parts
$CLICKHOUSE_CLIENT -q "SYSTEM STOP MERGES ${db_name}.test_intersect"

# Insert more parts to shift block numbers
for i in $(seq 6 10); do
    $CLICKHOUSE_CLIENT -q "INSERT INTO ${db_name}.test_intersect SELECT number + ($i - 1) * 100 FROM numbers(100)"
done

# Resume merges for the second OPTIMIZE
$CLICKHOUSE_CLIENT -q "SYSTEM START MERGES ${db_name}.test_intersect"

# Try another merge that would overlap with the first failed one
$CLICKHOUSE_CLIENT --implicit_transaction=1 --throw_on_unsupported_query_inside_transaction=0 \
    -q "OPTIMIZE TABLE ${db_name}.test_intersect FINAL" 2>/dev/null ||:

# DETACH and ATTACH should not produce a LOGICAL_ERROR about intersecting parts
$CLICKHOUSE_CLIENT -q "DETACH TABLE ${db_name}.test_intersect"
$CLICKHOUSE_CLIENT -q "ATTACH TABLE ${db_name}.test_intersect"

# Verify the table is accessible and all data is intact
$CLICKHOUSE_CLIENT -q "SELECT count() FROM ${db_name}.test_intersect"

$CLICKHOUSE_CLIENT -q "DROP DATABASE ${db_name}"
