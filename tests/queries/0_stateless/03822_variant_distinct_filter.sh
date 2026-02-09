#!/usr/bin/env bash
# Regression test for ColumnVariant::filter/permute/index optimization paths.
# The optimization for "one non-empty variant, no NULLs" must not carry over
# non-active variant columns that could have inconsistent sizes.
# The bug is a race condition most reliably triggered under ThreadSanitizer.
# This test exercises the code paths repeatedly to maximize the chance of
# catching the regression in sanitizer CI builds.
# https://s3.amazonaws.com/clickhouse-test-reports/json.html?REF=master&sha=4d4a583a5ad2322918638a3f6a01acd7e0ed7019&name_0=MasterCI&name_1=AST%20fuzzer%20%28amd_tsan%29

CURDIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=../shell_config.sh
. "$CURDIR"/../shell_config.sh

# Build a SQL script: create table, insert many single-row blocks, run queries in a loop.
# Each INSERT creates a separate block in the Memory engine.
# Many blocks with mixed variant types maximize parallel processing contention
# in ColumnVariant::filter/permute/index optimization paths.
sql="
SET max_threads = 16;
CREATE TABLE test_variant_distinct (v1 Variant(String, UInt64, Array(UInt32)), v2 Variant(String, UInt64, Array(UInt32))) ENGINE = Memory;
"

for i in $(seq 1 25); do
    sql+="INSERT INTO test_variant_distinct VALUES ($i, $i);"
    sql+="INSERT INTO test_variant_distinct VALUES ('s_$i', 's_$i');"
    sql+="INSERT INTO test_variant_distinct VALUES ([1,2,$i], [1,2,$i]);"
    sql+="INSERT INTO test_variant_distinct VALUES (NULL, NULL);"
    sql+="INSERT INTO test_variant_distinct VALUES ($i, 's_$i');"
    sql+="INSERT INTO test_variant_distinct VALUES ('s_$i', [1,2,$i]);"
    sql+="INSERT INTO test_variant_distinct VALUES ([1,2,$i], NULL);"
    sql+="INSERT INTO test_variant_distinct VALUES (NULL, $i);"
done

for _ in $(seq 1 200); do
    sql+="SELECT DISTINCT v2 FROM test_variant_distinct WHERE toLowCardinality(1) ORDER BY v2 ASC NULLS FIRST FORMAT Null;"
done

sql+="DROP TABLE test_variant_distinct;
SELECT 'OK';
"

echo "$sql" | ${CLICKHOUSE_LOCAL} --allow_experimental_variant_type=1 --allow_suspicious_types_in_order_by=1 --use_variant_default_implementation_for_comparisons=0 -n 2>&1
