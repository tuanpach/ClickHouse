-- Regression test for ColumnVariant::filter optimization path.
-- The optimization for "one non-empty variant, no NULLs" must not carry over
-- non-active variant columns that could have inconsistent sizes.
-- https://s3.amazonaws.com/clickhouse-test-reports/json.html?REF=master&sha=4d4a583a5ad2322918638a3f6a01acd7e0ed7019&name_0=MasterCI&name_1=AST%20fuzzer%20%28amd_tsan%29

SET allow_suspicious_types_in_order_by = 1;
SET use_variant_default_implementation_for_comparisons = 0;

DROP TABLE IF EXISTS test_variant_distinct;

CREATE TABLE test_variant_distinct (v1 Variant(String, UInt64, Array(UInt32)), v2 Variant(String, UInt64, Array(UInt32))) ENGINE = Memory;

INSERT INTO test_variant_distinct VALUES (42, 42);
INSERT INTO test_variant_distinct VALUES (42, 43);
INSERT INTO test_variant_distinct VALUES (43, 42);
INSERT INTO test_variant_distinct VALUES ('abc', 'abc');
INSERT INTO test_variant_distinct VALUES ('abc', 'abd');
INSERT INTO test_variant_distinct VALUES ('abd', 'abc');
INSERT INTO test_variant_distinct VALUES ([1,2,3], [1,2,3]);
INSERT INTO test_variant_distinct VALUES ([1,2,3], [1,2,4]);
INSERT INTO test_variant_distinct VALUES ([1,2,4], [1,2,3]);
INSERT INTO test_variant_distinct VALUES (NULL, NULL);
INSERT INTO test_variant_distinct VALUES (42, 'abc');
INSERT INTO test_variant_distinct VALUES ('abc', 42);
INSERT INTO test_variant_distinct VALUES (42, [1,2,3]);
INSERT INTO test_variant_distinct VALUES ([1,2,3], 42);
INSERT INTO test_variant_distinct VALUES (42, NULL);
INSERT INTO test_variant_distinct VALUES (NULL, 42);
INSERT INTO test_variant_distinct VALUES ('abc', [1,2,3]);
INSERT INTO test_variant_distinct VALUES ([1,2,3], 'abc');
INSERT INTO test_variant_distinct VALUES ('abc', NULL);
INSERT INTO test_variant_distinct VALUES (NULL, 'abc');
INSERT INTO test_variant_distinct VALUES ([1,2,3], NULL);
INSERT INTO test_variant_distinct VALUES (NULL, [1,2,3]);

SELECT DISTINCT v2 FROM test_variant_distinct WHERE toLowCardinality(1) ORDER BY v2 ASC NULLS FIRST;

DROP TABLE test_variant_distinct;
