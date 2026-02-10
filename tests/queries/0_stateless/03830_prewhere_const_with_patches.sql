-- Tags: no-replicated-database
-- Test for constant PREWHERE with patch parts (lightweight updates).
-- Constant PREWHERE expressions on tables with patch parts currently cause
-- a LOGICAL_ERROR exception in `adjustLastGranule` (issue #94700) because
-- `num_read_rows` is 0 (no columns physically read for constant PREWHERE),
-- while `total_rows_per_granule` reflects the full granule sizes.
-- A previous fix (PR #95056) was reverted (PR #96574) because it introduced
-- a regression with spurious zero-filled rows.
-- When issue #94700 is properly fixed, update this test to expect correct results
-- instead of LOGICAL_ERROR.
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

-- These queries currently fail with LOGICAL_ERROR in `adjustLastGranule`.
-- When issue #94700 is fixed, they should return: 10000, 10000, 0,
-- the GROUP BY result, 10000, and 10000 respectively.

SELECT count() FROM t_prewhere_const_patches PREWHERE 18; -- { serverError LOGICAL_ERROR }

SELECT count() FROM t_prewhere_const_patches PREWHERE 1; -- { serverError LOGICAL_ERROR }

SELECT count() FROM t_prewhere_const_patches PREWHERE 0; -- { serverError LOGICAL_ERROR }

-- The original fuzzed query pattern from issue #94700
SELECT b, c, count() FROM t_prewhere_const_patches PREWHERE toUInt128(toUInt128(1)) = isNotNull(toLowCardinality(1)) GROUP BY b, c ORDER BY b, c; -- { serverError LOGICAL_ERROR }

-- Non-aligned max_block_size to exercise `adjustLastGranule` edge cases
SELECT count() FROM t_prewhere_const_patches PREWHERE 1 SETTINGS max_block_size = 100; -- { serverError LOGICAL_ERROR }
SELECT count() FROM t_prewhere_const_patches PREWHERE 1 SETTINGS max_block_size = 8482; -- { serverError LOGICAL_ERROR }

DROP TABLE IF EXISTS t_prewhere_const_patches SYNC;
