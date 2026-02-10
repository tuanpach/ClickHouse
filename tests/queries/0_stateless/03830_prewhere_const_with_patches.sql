-- Tags: no-replicated-database
-- Test for constant PREWHERE with patch parts (lightweight updates).
-- Verifies correctness when using constant PREWHERE expressions on tables
-- with patch parts from lightweight updates.
-- A previous fix (PR #95056) for an exception in `adjustLastGranule` was reverted
-- (PR #96574) because it introduced a regression with spurious zero-filled rows.
-- This test ensures correct results for constant PREWHERE with patches.
-- https://github.com/ClickHouse/ClickHouse/pull/96574
-- https://github.com/ClickHouse/ClickHouse/issues/94700

DROP TABLE IF EXISTS t_prewhere_const_patches SYNC;

SET enable_lightweight_update = 1;
SET mutations_sync = 2;

CREATE TABLE t_prewhere_const_patches (a UInt64, b UInt64, c UInt64, d UInt64)
ENGINE = MergeTree
ORDER BY tuple()
SETTINGS
    min_bytes_for_wide_part = 0,
    min_bytes_for_full_part_storage = 0,
    ratio_of_defaults_for_sparse_serialization = 1.0,
    enable_block_number_column = 1,
    enable_block_offset_column = 1;

-- Stop merges to ensure patch parts are preserved
SYSTEM STOP MERGES t_prewhere_const_patches;

INSERT INTO t_prewhere_const_patches SELECT number, 0, 0, 0 FROM numbers(10000);

-- Apply some lightweight updates to create patch parts
UPDATE t_prewhere_const_patches SET b = 1 WHERE a % 4 = 0;
UPDATE t_prewhere_const_patches SET c = 2 WHERE a % 4 = 0;

-- Constant PREWHERE with true value should return all rows
SELECT count() FROM t_prewhere_const_patches PREWHERE 18;

-- Also test with explicit true constant
SELECT count() FROM t_prewhere_const_patches PREWHERE 1;

-- Test with false constant (should return 0)
SELECT count() FROM t_prewhere_const_patches PREWHERE 0;

-- Test with the original fuzzed query pattern (complex constant expression)
-- This was the original query from issue #94700
SELECT b, c, count() FROM t_prewhere_const_patches PREWHERE toUInt128(toUInt128(1)) = isNotNull(toLowCardinality(1)) GROUP BY b, c ORDER BY b, c;

-- Test with non-aligned max_block_size to exercise `adjustLastGranule` edge cases
SELECT count() FROM t_prewhere_const_patches PREWHERE 1 SETTINGS max_block_size = 100;
SELECT count() FROM t_prewhere_const_patches PREWHERE 1 SETTINGS max_block_size = 8482;

DROP TABLE IF EXISTS t_prewhere_const_patches SYNC;
