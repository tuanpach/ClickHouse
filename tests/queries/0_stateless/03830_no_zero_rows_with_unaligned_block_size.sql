-- Regression test for https://github.com/ClickHouse/ClickHouse/pull/96574
-- When `adjustLastGranule` uses `total_rows_per_granule` instead of `numReadRows`
-- for `num_rows`, blocks could contain extra zero-filled rows (e.g. 1970-01-01 for Date columns).
-- This test verifies that no spurious zero-filled rows appear when reading with
-- `max_block_size` that doesn't align with `index_granularity`.

DROP TABLE IF EXISTS t_no_zero_rows;

CREATE TABLE t_no_zero_rows (d Date, x UInt64)
ENGINE = MergeTree
ORDER BY x
SETTINGS index_granularity = 8192;

-- Insert enough rows to span multiple granules with dates that never include 1970-01-01
INSERT INTO t_no_zero_rows SELECT toDate('2024-01-01') + (number % 30), number FROM numbers(100000);

-- Use max_block_size that doesn't align with index_granularity (8482 vs 8192)
-- This is the exact scenario from the bug report.
SELECT DISTINCT d FROM t_no_zero_rows ORDER BY d SETTINGS max_block_size = 8482, max_threads = 2;

-- Also verify no zero-valued rows with different unaligned block sizes
SELECT count() FROM t_no_zero_rows WHERE d = '1970-01-01' SETTINGS max_block_size = 8482;
SELECT count() FROM t_no_zero_rows WHERE d = '1970-01-01' SETTINGS max_block_size = 100;
SELECT count() FROM t_no_zero_rows WHERE d = '1970-01-01' SETTINGS max_block_size = 9999;

-- Verify total count is correct
SELECT count() FROM t_no_zero_rows;

DROP TABLE t_no_zero_rows;
