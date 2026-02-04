#!/usr/bin/env bash

CURDIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=../shell_config.sh
. "$CURDIR"/../shell_config.sh

$CLICKHOUSE_CLIENT -m -q "
drop table if exists attach_with_unknown_projection sync;

create table attach_with_unknown_projection (x Int32, y Int32, projection p (select x, y order by x))
engine = ReplicatedMergeTree('/clickhouse/tables/{database}/attach_with_unknown_projection', '1')
order by y
partition by intDiv(y, 100) settings max_parts_to_merge_at_once=1;

insert into attach_with_unknown_projection select number, number from numbers(3);

set mutations_sync = 2;
alter table attach_with_unknown_projection add projection pp (select x, count() group by x);
insert into attach_with_unknown_projection select number, number from numbers(4);
select count() from attach_with_unknown_projection;

alter table attach_with_unknown_projection detach partition '0';
alter table attach_with_unknown_projection clear projection pp;
alter table attach_with_unknown_projection drop projection pp;
alter table attach_with_unknown_projection attach partition '0';
"

$CLICKHOUSE_CLIENT -m -q "
set send_logs_level='fatal';
set check_query_single_value_result = 1;
check table attach_with_unknown_projection settings check_query_single_value_result = 0;" | grep -o "Found unexpected projection directories: pp.proj
"

$CLICKHOUSE_CLIENT -m -q "
select count() from attach_with_unknown_projection;
"
