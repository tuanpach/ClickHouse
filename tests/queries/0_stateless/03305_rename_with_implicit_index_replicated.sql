-- Tags: zookeeper
-- This is a whole separate new set of tests since ReplicatedMergeTree deals with the metadata changes differently (via Keeper)

-- Test 1: Rename column with implicit indices
DROP TABLE IF EXISTS t_rename_implicit_index SYNC;

CREATE TABLE t_rename_implicit_index (a UInt64, b UInt64)
ENGINE = ReplicatedMergeTree('/clickhouse/tables/{database}/t_rename_implicit_index', '1')
ORDER BY a
SETTINGS add_minmax_index_for_numeric_columns = 1;

SELECT 'Initial indices:';
SELECT name, type, expr FROM system.data_skipping_indices
WHERE database = currentDatabase() AND table = 't_rename_implicit_index'
ORDER BY name;

INSERT INTO t_rename_implicit_index VALUES (1, 2), (3, 4);

ALTER TABLE t_rename_implicit_index RENAME COLUMN b TO c;
SYSTEM SYNC REPLICA t_rename_implicit_index;

SELECT 'After rename:';
SELECT name, type, expr FROM system.data_skipping_indices
WHERE database = currentDatabase() AND table = 't_rename_implicit_index'
ORDER BY name;

SELECT * FROM t_rename_implicit_index ORDER BY a;

DROP TABLE t_rename_implicit_index SYNC;

-- Test 2: Add column with implicit indices
DROP TABLE IF EXISTS t_add_column_implicit SYNC;

CREATE TABLE t_add_column_implicit (a UInt64)
ENGINE = ReplicatedMergeTree('/clickhouse/tables/{database}/t_add_column_implicit', '1')
ORDER BY a
SETTINGS add_minmax_index_for_numeric_columns = 1;

SELECT 'Before adding column:';
SELECT name, type, expr FROM system.data_skipping_indices
WHERE database = currentDatabase() AND table = 't_add_column_implicit'
ORDER BY name;

INSERT INTO t_add_column_implicit VALUES (1), (2);

ALTER TABLE t_add_column_implicit ADD COLUMN b UInt64 DEFAULT 0;
SYSTEM SYNC REPLICA t_add_column_implicit;

SELECT 'After adding column:';
SELECT name, type, expr FROM system.data_skipping_indices
WHERE database = currentDatabase() AND table = 't_add_column_implicit'
ORDER BY name;

SELECT * FROM t_add_column_implicit ORDER BY a;

DROP TABLE t_add_column_implicit SYNC;

-- Test 3: Modify column type from numeric to non-numeric (implicit index should be removed)
DROP TABLE IF EXISTS t_modify_type_implicit SYNC;

CREATE TABLE t_modify_type_implicit (a UInt64, b UInt32)
ENGINE = ReplicatedMergeTree('/clickhouse/tables/{database}/t_modify_type_implicit', '1')
ORDER BY a
SETTINGS add_minmax_index_for_numeric_columns = 1, add_minmax_index_for_string_columns=0;

SELECT 'Before type change:';
SELECT name, type, expr FROM system.data_skipping_indices
WHERE database = currentDatabase() AND table = 't_modify_type_implicit'
ORDER BY name;

INSERT INTO t_modify_type_implicit VALUES (1, 2), (3, 4);

ALTER TABLE t_modify_type_implicit MODIFY COLUMN b String;
SYSTEM SYNC REPLICA t_modify_type_implicit;

SELECT 'After type change to String:';
SELECT name, type, expr FROM system.data_skipping_indices
WHERE database = currentDatabase() AND table = 't_modify_type_implicit'
ORDER BY name;

SELECT * FROM t_modify_type_implicit ORDER BY a;

DROP TABLE t_modify_type_implicit SYNC;

-- Test 4: Add both explicit index and new column simultaneously
DROP TABLE IF EXISTS t_add_index_and_column SYNC;

CREATE TABLE t_add_index_and_column (a UInt64)
ENGINE = ReplicatedMergeTree('/clickhouse/tables/{database}/t_add_index_and_column', '1')
ORDER BY a
SETTINGS add_minmax_index_for_numeric_columns = 1;

SELECT 'Before adding index and column:';
SELECT name, type, expr FROM system.data_skipping_indices
WHERE database = currentDatabase() AND table = 't_add_index_and_column'
ORDER BY name;

INSERT INTO t_add_index_and_column VALUES (1), (2);

-- Add both a new column and a new explicit index
ALTER TABLE t_add_index_and_column ADD COLUMN b UInt64 DEFAULT 0, ADD INDEX idx_explicit_a a TYPE minmax GRANULARITY 1;
SYSTEM SYNC REPLICA t_add_index_and_column;

SELECT 'After adding index and column:';
SELECT name, type, expr FROM system.data_skipping_indices
WHERE database = currentDatabase() AND table = 't_add_index_and_column'
ORDER BY name;

SELECT * FROM t_add_index_and_column ORDER BY a;

DROP TABLE t_add_index_and_column SYNC;

-- Test 5: Drop column with implicit index
DROP TABLE IF EXISTS t_drop_column_implicit SYNC;

CREATE TABLE t_drop_column_implicit (a UInt64, b UInt64, c UInt64)
ENGINE = ReplicatedMergeTree('/clickhouse/tables/{database}/t_drop_column_implicit', '1')
ORDER BY a
SETTINGS add_minmax_index_for_numeric_columns = 1;

SELECT 'Before dropping column:';
SELECT name, type, expr FROM system.data_skipping_indices
WHERE database = currentDatabase() AND table = 't_drop_column_implicit'
ORDER BY name;

INSERT INTO t_drop_column_implicit VALUES (1, 2, 3), (4, 5, 6);

ALTER TABLE t_drop_column_implicit DROP COLUMN b;
SYSTEM SYNC REPLICA t_drop_column_implicit;

SELECT 'After dropping column b:';
SELECT name, type, expr FROM system.data_skipping_indices
WHERE database = currentDatabase() AND table = 't_drop_column_implicit'
ORDER BY name;

SELECT * FROM t_drop_column_implicit ORDER BY a;

DROP TABLE t_drop_column_implicit SYNC;

-- Test 6: Multiple operations at once (rename + add column)
DROP TABLE IF EXISTS t_multiple_ops SYNC;

CREATE TABLE t_multiple_ops (a UInt64, b UInt64)
ENGINE = ReplicatedMergeTree('/clickhouse/tables/{database}/t_multiple_ops', '1')
ORDER BY a
SETTINGS add_minmax_index_for_numeric_columns = 1;

SELECT 'Before multiple operations:';
SELECT name, type, expr FROM system.data_skipping_indices
WHERE database = currentDatabase() AND table = 't_multiple_ops'
ORDER BY name;

INSERT INTO t_multiple_ops VALUES (1, 2), (3, 4);

-- Rename b to old_b and add new column c
ALTER TABLE t_multiple_ops RENAME COLUMN b TO old_b, ADD COLUMN c UInt64 DEFAULT 0;
SYSTEM SYNC REPLICA t_multiple_ops;

SELECT 'After rename and add:';
SELECT name, type, expr FROM system.data_skipping_indices
WHERE database = currentDatabase() AND table = 't_multiple_ops'
ORDER BY name;

SELECT * FROM t_multiple_ops ORDER BY a;

DROP TABLE t_multiple_ops SYNC;

-- Test 7: Explicit index on same column (should not create duplicate implicit index)
DROP TABLE IF EXISTS t_explicit_prevents_implicit SYNC;

CREATE TABLE t_explicit_prevents_implicit (a UInt64, b UInt64)
ENGINE = ReplicatedMergeTree('/clickhouse/tables/{database}/t_explicit_prevents_implicit', '1')
ORDER BY a
SETTINGS add_minmax_index_for_numeric_columns = 1;

SELECT 'Before adding explicit index:';
SELECT name, type, expr FROM system.data_skipping_indices
WHERE database = currentDatabase() AND table = 't_explicit_prevents_implicit'
ORDER BY name;

INSERT INTO t_explicit_prevents_implicit VALUES (1, 2), (3, 4);

-- Add explicit minmax index on column b (should not duplicate the implicit one)
ALTER TABLE t_explicit_prevents_implicit ADD INDEX idx_explicit_b b TYPE minmax GRANULARITY 1;
SYSTEM SYNC REPLICA t_explicit_prevents_implicit;

SELECT 'After adding explicit index on b:';
SELECT name, type, expr FROM system.data_skipping_indices
WHERE database = currentDatabase() AND table = 't_explicit_prevents_implicit'
ORDER BY name;

-- Verify only one index exists per column
SELECT column_name, count() as index_count FROM (
    SELECT arrayJoin(splitByChar(',', expr)) as column_name
    FROM system.data_skipping_indices
    WHERE database = currentDatabase() AND table = 't_explicit_prevents_implicit'
    AND type = 'minmax'
)
GROUP BY column_name
ORDER BY column_name;

DROP TABLE t_explicit_prevents_implicit SYNC;

-- Test 8: Drop explicit index (implicit should remain/regenerate)
DROP TABLE IF EXISTS t_drop_explicit_index SYNC;

CREATE TABLE t_drop_explicit_index (a UInt64, b UInt64)
ENGINE = ReplicatedMergeTree('/clickhouse/tables/{database}/t_drop_explicit_index', '1')
ORDER BY a
SETTINGS add_minmax_index_for_numeric_columns = 1;

-- Add explicit index first
ALTER TABLE t_drop_explicit_index ADD INDEX idx_explicit_a a TYPE minmax GRANULARITY 1;
SYSTEM SYNC REPLICA t_drop_explicit_index;

SELECT 'After adding explicit index:';
SELECT name, type, expr FROM system.data_skipping_indices
WHERE database = currentDatabase() AND table = 't_drop_explicit_index'
ORDER BY name;

INSERT INTO t_drop_explicit_index VALUES (1, 2), (3, 4);

-- Drop the explicit index, implicit should remain
ALTER TABLE t_drop_explicit_index DROP INDEX idx_explicit_a;
SYSTEM SYNC REPLICA t_drop_explicit_index;

SELECT 'After dropping explicit index:';
SELECT name, type, expr FROM system.data_skipping_indices
WHERE database = currentDatabase() AND table = 't_drop_explicit_index'
ORDER BY name;

DROP TABLE t_drop_explicit_index SYNC;
