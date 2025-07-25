---
description: 'System table containing information about parts of MergeTree'
keywords: ['system table', 'parts']
slug: /operations/system-tables/parts
title: 'system.parts'
---

# system.parts

Contains information about parts of [MergeTree](../../engines/table-engines/mergetree-family/mergetree.md) tables.

Each row describes one data part.

Columns:

- `partition` ([String](../../sql-reference/data-types/string.md)) – The partition name. To learn what a partition is, see the description of the [ALTER](/sql-reference/statements/alter) query.

    Formats:

  - `YYYYMM` for automatic partitioning by month.
  - `any_string` when partitioning manually.

- `name` ([String](../../sql-reference/data-types/string.md)) – Name of the data part. The part naming structure can be used to determine many aspects of the data, ingest, and merge patterns. The part naming format is the following:

```text
<partition_id>_<minimum_block_number>_<maximum_block_number>_<level>_<data_version>
```

* Definitions:
  - `partition_id` - identifies the partition key
  - `minimum_block_number` - identifies the minimum block number in the part. ClickHouse always merges continuous blocks
  - `maximum_block_number` - identifies the maximum block number in the part
  - `level` - incremented by one with each additional merge on the part. A level of 0 indicates this is a new part that has not been merged. It is important to remember that all parts in ClickHouse are always immutable
  - `data_version` - optional value, incremented when a part is mutated (again, mutated data is always only written to a new part, since parts are immutable)

- `uuid` ([UUID](../../sql-reference/data-types/uuid.md)) -  The UUID of data part.

- `part_type` ([String](../../sql-reference/data-types/string.md)) — The data part storing format.

    Possible Values:

  - `Wide` — Each column is stored in a separate file in a filesystem.
  - `Compact` — All columns are stored in one file in a filesystem.

    Data storing format is controlled by the `min_bytes_for_wide_part` and `min_rows_for_wide_part` settings of the [MergeTree](../../engines/table-engines/mergetree-family/mergetree.md) table.

- `active` ([UInt8](../../sql-reference/data-types/int-uint.md)) – Flag that indicates whether the data part is active. If a data part is active, it's used in a table. Otherwise, it's deleted. Inactive data parts remain after merging.

- `marks` ([UInt64](../../sql-reference/data-types/int-uint.md)) – The number of marks. To get the approximate number of rows in a data part, multiply `marks` by the index granularity (usually 8192) (this hint does not work for adaptive granularity).

- `rows` ([UInt64](../../sql-reference/data-types/int-uint.md)) – The number of rows.

- `bytes_on_disk` ([UInt64](../../sql-reference/data-types/int-uint.md)) – Total size of all the data part files in bytes.

- `data_compressed_bytes` ([UInt64](../../sql-reference/data-types/int-uint.md)) – Total size of compressed data in the data part. All the auxiliary files (for example, files with marks) are not included.

- `data_uncompressed_bytes` ([UInt64](../../sql-reference/data-types/int-uint.md)) – Total size of uncompressed data in the data part. All the auxiliary files (for example, files with marks) are not included.

- `primary_key_size` ([UInt64](../../sql-reference/data-types/int-uint.md)) – The amount of memory (in bytes) used by primary key values in the primary.idx/cidx file on disk.

- `marks_bytes` ([UInt64](../../sql-reference/data-types/int-uint.md)) – The size of the file with marks.

- `secondary_indices_compressed_bytes` ([UInt64](../../sql-reference/data-types/int-uint.md)) – Total size of compressed data for secondary indices in the data part. All the auxiliary files (for example, files with marks) are not included.

- `secondary_indices_uncompressed_bytes` ([UInt64](../../sql-reference/data-types/int-uint.md)) – Total size of uncompressed data for secondary indices in the data part. All the auxiliary files (for example, files with marks) are not included.

- `secondary_indices_marks_bytes` ([UInt64](../../sql-reference/data-types/int-uint.md)) – The size of the file with marks for secondary indices.

- `modification_time` ([DateTime](../../sql-reference/data-types/datetime.md)) – The time the directory with the data part was modified. This usually corresponds to the time of data part creation.

- `remove_time` ([DateTime](../../sql-reference/data-types/datetime.md)) – The time when the data part became inactive.

- `refcount` ([UInt32](../../sql-reference/data-types/int-uint.md)) – The number of places where the data part is used. A value greater than 2 indicates that the data part is used in queries or merges.

- `min_date` ([Date](../../sql-reference/data-types/date.md)) – The minimum value of the date key in the data part.

- `max_date` ([Date](../../sql-reference/data-types/date.md)) – The maximum value of the date key in the data part.

- `min_time` ([DateTime](../../sql-reference/data-types/datetime.md)) – The minimum value of the date and time key in the data part.

- `max_time`([DateTime](../../sql-reference/data-types/datetime.md)) – The maximum value of the date and time key in the data part.

- `partition_id` ([String](../../sql-reference/data-types/string.md)) – ID of the partition.

- `min_block_number` ([UInt64](../../sql-reference/data-types/int-uint.md)) – The minimum data block number that makes up the current part after merging.

- `max_block_number` ([UInt64](../../sql-reference/data-types/int-uint.md)) – The maximum data block number that makes up the current part after merging.

- `level` ([UInt32](../../sql-reference/data-types/int-uint.md)) – Depth of the merge tree. Zero means that the current part was created by insert rather than by merging other parts.

- `data_version` ([UInt64](../../sql-reference/data-types/int-uint.md)) – Number that is used to determine which mutations should be applied to the data part (mutations with a version higher than `data_version`).

- `primary_key_bytes_in_memory` ([UInt64](../../sql-reference/data-types/int-uint.md)) – The amount of memory (in bytes) used by primary key values.

- `primary_key_bytes_in_memory_allocated` ([UInt64](../../sql-reference/data-types/int-uint.md)) – The amount of memory (in bytes) reserved for primary key values.

- `is_frozen` ([UInt8](../../sql-reference/data-types/int-uint.md)) – Flag that shows that a partition data backup exists. 1, the backup exists. 0, the backup does not exist. For more details, see [FREEZE PARTITION](/sql-reference/statements/alter/partition#freeze-partition)

- `database` ([String](../../sql-reference/data-types/string.md)) – Name of the database.

- `table` ([String](../../sql-reference/data-types/string.md)) – Name of the table.

- `engine` ([String](../../sql-reference/data-types/string.md)) – Name of the table engine without parameters.

- `path` ([String](../../sql-reference/data-types/string.md)) – Absolute path to the folder with data part files.

- `disk_name` ([String](../../sql-reference/data-types/string.md)) – Name of a disk that stores the data part.

- `hash_of_all_files` ([String](../../sql-reference/data-types/string.md)) – [sipHash128](/sql-reference/functions/hash-functions#siphash128) of compressed files.

- `hash_of_uncompressed_files` ([String](../../sql-reference/data-types/string.md)) – [sipHash128](/sql-reference/functions/hash-functions#siphash128) of uncompressed files (files with marks, index file etc.).

- `uncompressed_hash_of_compressed_files` ([String](../../sql-reference/data-types/string.md)) – [sipHash128](/sql-reference/functions/hash-functions#siphash128) of data in the compressed files as if they were uncompressed.

- `delete_ttl_info_min` ([DateTime](../../sql-reference/data-types/datetime.md)) — The minimum value of the date and time key for [TTL DELETE rule](../../engines/table-engines/mergetree-family/mergetree.md/#table_engine-mergetree-ttl).

- `delete_ttl_info_max` ([DateTime](../../sql-reference/data-types/datetime.md)) — The maximum value of the date and time key for [TTL DELETE rule](../../engines/table-engines/mergetree-family/mergetree.md/#table_engine-mergetree-ttl).

- `move_ttl_info.expression` ([Array](../../sql-reference/data-types/array.md)([String](../../sql-reference/data-types/string.md))) — Array of expressions. Each expression defines a [TTL MOVE rule](../../engines/table-engines/mergetree-family/mergetree.md/#table_engine-mergetree-ttl).

:::note
The `move_ttl_info.expression` array is kept mostly for backward compatibility, now the simplest way to check `TTL MOVE` rule is to use the `move_ttl_info.min` and `move_ttl_info.max` fields.
:::

- `move_ttl_info.min` ([Array](../../sql-reference/data-types/array.md)([DateTime](../../sql-reference/data-types/datetime.md))) — Array of date and time values. Each element describes the minimum key value for a [TTL MOVE rule](../../engines/table-engines/mergetree-family/mergetree.md/#table_engine-mergetree-ttl).

- `move_ttl_info.max` ([Array](../../sql-reference/data-types/array.md)([DateTime](../../sql-reference/data-types/datetime.md))) — Array of date and time values. Each element describes the maximum key value for a [TTL MOVE rule](../../engines/table-engines/mergetree-family/mergetree.md/#table_engine-mergetree-ttl).

- `bytes` ([UInt64](../../sql-reference/data-types/int-uint.md)) – Alias for `bytes_on_disk`.

- `marks_size` ([UInt64](../../sql-reference/data-types/int-uint.md)) – Alias for `marks_bytes`.

**Example**

```sql
SELECT * FROM system.parts LIMIT 1 FORMAT Vertical;
```

```text
Row 1:
──────
partition:                             tuple()
name:                                  all_1_4_1_6
part_type:                             Wide
active:                                1
marks:                                 2
rows:                                  6
bytes_on_disk:                         310
data_compressed_bytes:                 157
data_uncompressed_bytes:               91
secondary_indices_compressed_bytes:    58
secondary_indices_uncompressed_bytes:  6
secondary_indices_marks_bytes:         48
marks_bytes:                           144
modification_time:                     2020-06-18 13:01:49
remove_time:                           1970-01-01 00:00:00
refcount:                              1
min_date:                              1970-01-01
max_date:                              1970-01-01
min_time:                              1970-01-01 00:00:00
max_time:                              1970-01-01 00:00:00
partition_id:                          all
min_block_number:                      1
max_block_number:                      4
level:                                 1
data_version:                          6
primary_key_bytes_in_memory:           8
primary_key_bytes_in_memory_allocated: 64
is_frozen:                             0
database:                              default
table:                                 months
engine:                                MergeTree
disk_name:                             default
path:                                  /var/lib/clickhouse/data/default/months/all_1_4_1_6/
hash_of_all_files:                     2d0657a16d9430824d35e327fcbd87bf
hash_of_uncompressed_files:            84950cc30ba867c77a408ae21332ba29
uncompressed_hash_of_compressed_files: 1ad78f1c6843bbfb99a2c931abe7df7d
delete_ttl_info_min:                   1970-01-01 00:00:00
delete_ttl_info_max:                   1970-01-01 00:00:00
move_ttl_info.expression:              []
move_ttl_info.min:                     []
move_ttl_info.max:                     []
```

**See Also**

- [MergeTree family](../../engines/table-engines/mergetree-family/mergetree.md)
- [TTL for Columns and Tables](../../engines/table-engines/mergetree-family/mergetree.md/#table_engine-mergetree-ttl)
