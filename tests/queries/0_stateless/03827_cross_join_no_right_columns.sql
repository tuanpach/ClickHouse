-- Test that cross join works correctly when no columns from the right side are needed.
-- This used to cause std::out_of_range in HashJoin::getTotalRowCount() because
-- columns_info.columns was empty but .at(0) was used to get the row count.

DROP TABLE IF EXISTS t1;
DROP TABLE IF EXISTS t2;

CREATE TABLE t1 (x UInt64) ENGINE = MergeTree ORDER BY x;
CREATE TABLE t2 (y UInt64) ENGINE = MergeTree ORDER BY y;

INSERT INTO t1 VALUES (1), (2), (3);
INSERT INTO t2 VALUES (10), (20);

-- No columns from t2 referenced - the right side block will have zero columns
SELECT count() FROM t1, t2;

DROP TABLE t1;
DROP TABLE t2;
