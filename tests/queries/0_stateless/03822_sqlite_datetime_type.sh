#!/usr/bin/env bash
# Tags: no-fasttest

CUR_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=../shell_config.sh
. "$CUR_DIR"/../shell_config.sh

DB_PATH="${CLICKHOUSE_TMP}/test_${CLICKHOUSE_DATABASE}_sqlite_datetime.db"

rm -f "${DB_PATH}"

# Test DateTime type - SQLite stores TEXT, ClickHouse reads as DateTime
sqlite3 "${DB_PATH}" 'CREATE TABLE tx_datetime (c0 TEXT);'
sqlite3 "${DB_PATH}" "INSERT INTO tx_datetime VALUES ('2024-01-01 12:00:00');"

# Test Date type
sqlite3 "${DB_PATH}" 'CREATE TABLE tx_date (c0 TEXT);'
sqlite3 "${DB_PATH}" "INSERT INTO tx_date VALUES ('2024-01-15');"

# Test UUID type
sqlite3 "${DB_PATH}" 'CREATE TABLE tx_uuid (c0 TEXT);'
sqlite3 "${DB_PATH}" "INSERT INTO tx_uuid VALUES ('550e8400-e29b-41d4-a716-446655440000');"

${CLICKHOUSE_LOCAL} --query="
CREATE DATABASE tmpdb ENGINE = SQLite('${DB_PATH}');

-- Test DateTime: this triggers the bug - Bad cast from ColumnVector<unsigned int> to ColumnString
CREATE TABLE t_datetime (c0 DateTime) ENGINE = SQLite('${DB_PATH}', 'tx_datetime');
SELECT * FROM t_datetime;

-- Test Date
CREATE TABLE t_date (c0 Date) ENGINE = SQLite('${DB_PATH}', 'tx_date');
SELECT * FROM t_date;

-- Test UUID
CREATE TABLE t_uuid (c0 UUID) ENGINE = SQLite('${DB_PATH}', 'tx_uuid');
SELECT * FROM t_uuid;
"

rm -f "${DB_PATH}"
