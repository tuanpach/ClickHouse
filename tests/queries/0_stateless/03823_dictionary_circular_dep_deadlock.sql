-- Tags: no-parallel
-- Test for https://github.com/ClickHouse/ClickHouse/issues/78360
-- A HASHED dictionary that references itself through a Merge table should not deadlock.
-- Previously this would hang forever; now it should produce an error about circular dependency.

DROP DATABASE IF EXISTS test_dict_deadlock;
CREATE DATABASE test_dict_deadlock ENGINE = Atomic;

CREATE TABLE test_dict_deadlock.t0 (c0 Int) ENGINE = Merge('test_dict_deadlock', 'd1');
CREATE DICTIONARY test_dict_deadlock.d1 (c0 Int DEFAULT 1) PRIMARY KEY (c0) SOURCE(CLICKHOUSE(DB 'test_dict_deadlock' TABLE 't0')) LAYOUT(HASHED()) LIFETIME(1);

SELECT 1 FROM test_dict_deadlock.d1; -- { serverError DEADLOCK_AVOIDED }

SYSTEM RELOAD DICTIONARY test_dict_deadlock.d1; -- { serverError DEADLOCK_AVOIDED }

DROP DICTIONARY test_dict_deadlock.d1;
DROP TABLE test_dict_deadlock.t0;
DROP DATABASE test_dict_deadlock;
