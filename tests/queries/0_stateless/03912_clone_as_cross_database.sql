-- Tags: no-replicated-database
-- Tag no-replicated-database: Unsupported type of CREATE TABLE ... CLONE AS ... query

DROP DATABASE IF EXISTS {CLICKHOUSE_DATABASE_1:Identifier};
CREATE DATABASE {CLICKHOUSE_DATABASE_1:Identifier};

CREATE TABLE {CLICKHOUSE_DATABASE_1:Identifier}.source (x Int8, y String) ENGINE = MergeTree PRIMARY KEY x;
INSERT INTO {CLICKHOUSE_DATABASE_1:Identifier}.source VALUES (1, 'a'), (2, 'b'), (3, 'c');

-- CLONE AS with a fully-qualified source table from another database
CREATE TABLE clone_target CLONE AS {CLICKHOUSE_DATABASE_1:Identifier}.source;
SELECT * FROM clone_target ORDER BY x;

DROP TABLE clone_target;
DROP DATABASE {CLICKHOUSE_DATABASE_1:Identifier};
