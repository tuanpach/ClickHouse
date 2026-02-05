-- Regression test for https://github.com/ClickHouse/ClickHouse/issues/78287
-- The assertion in `MergeTreePrefetchedReadPool::startPrefetches` could fail
-- when `filesystem_prefetch_max_memory_usage` is low enough that the first tasks
-- of all threads are skipped for prefetching, so the top of the prefetch queue
-- has a priority higher than the initial (highest) priority.

DROP TABLE IF EXISTS t0;

CREATE TABLE t0 (c0 Tuple(Int256, String)) ENGINE = MergeTree() ORDER BY tuple() SETTINGS min_bytes_for_wide_part = 0;

SET allow_prefetched_read_pool_for_local_filesystem = 1, filesystem_prefetch_max_memory_usage = 4096, max_threads = 4;

INSERT INTO TABLE t0 SELECT (number, repeat('x', number % 100 + 1)) FROM numbers(1000);
INSERT INTO TABLE t0 SELECT (number + 1000, repeat('y', number % 200 + 1)) FROM numbers(500);
INSERT INTO TABLE t0 SELECT (number + 2000, repeat('z', number % 50 + 1)) FROM numbers(200);
INSERT INTO TABLE t0 SELECT (number + 3000, repeat('w', number % 150 + 1)) FROM numbers(800);
INSERT INTO TABLE t0 SELECT (number + 4000, repeat('a', number % 300 + 1)) FROM numbers(100);

SELECT * FROM t0 FORMAT Null;

DROP TABLE t0;
