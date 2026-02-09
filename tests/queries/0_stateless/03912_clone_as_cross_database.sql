-- Tags: no-replicated-database
-- Tag no-replicated-database: Unsupported type of CREATE TABLE ... CLONE AS ... query

DROP DATABASE IF EXISTS clone_as_source_03912;
CREATE DATABASE clone_as_source_03912;

CREATE TABLE clone_as_source_03912.source (x Int8, y String) ENGINE = MergeTree PRIMARY KEY x;
INSERT INTO clone_as_source_03912.source VALUES (1, 'a'), (2, 'b'), (3, 'c');

-- CLONE AS with a fully-qualified source table from another database
CREATE TABLE clone_target CLONE AS clone_as_source_03912.source;
SELECT * FROM clone_target ORDER BY x;

DROP TABLE clone_target;
DROP DATABASE clone_as_source_03912;
