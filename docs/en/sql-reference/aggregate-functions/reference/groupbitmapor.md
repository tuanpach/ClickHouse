---
description: 'Calculations the OR of a bitmap column, return cardinality of type UInt64,
  if add suffix -State, then return a bitmap object. This is equivalent to `groupBitmapMerge`.'
sidebar_position: 150
slug: /sql-reference/aggregate-functions/reference/groupbitmapor
title: 'groupBitmapOr'
---

# groupBitmapOr

Calculations the OR of a bitmap column, return cardinality of type UInt64, if add suffix -State, then return a [bitmap object](../../../sql-reference/functions/bitmap-functions.md). This is equivalent to `groupBitmapMerge`.

```sql
groupBitmapOr(expr)
```

**Arguments**

`expr` – An expression that results in `AggregateFunction(groupBitmap, UInt*)` type.

**Returned value**

Value of the `UInt64` type.

**Example**

```sql
DROP TABLE IF EXISTS bitmap_column_expr_test2;
CREATE TABLE bitmap_column_expr_test2
(
    tag_id String,
    z AggregateFunction(groupBitmap, UInt32)
)
ENGINE = MergeTree
ORDER BY tag_id;

INSERT INTO bitmap_column_expr_test2 VALUES ('tag1', bitmapBuild(cast([1,2,3,4,5,6,7,8,9,10] AS Array(UInt32))));
INSERT INTO bitmap_column_expr_test2 VALUES ('tag2', bitmapBuild(cast([6,7,8,9,10,11,12,13,14,15] AS Array(UInt32))));
INSERT INTO bitmap_column_expr_test2 VALUES ('tag3', bitmapBuild(cast([2,4,6,8,10,12] AS Array(UInt32))));

SELECT groupBitmapOr(z) FROM bitmap_column_expr_test2 WHERE like(tag_id, 'tag%');
┌─groupBitmapOr(z)─┐
│             15   │
└──────────────────┘

SELECT arraySort(bitmapToArray(groupBitmapOrState(z))) FROM bitmap_column_expr_test2 WHERE like(tag_id, 'tag%');
┌─arraySort(bitmapToArray(groupBitmapOrState(z)))─┐
│ [1,2,3,4,5,6,7,8,9,10,11,12,13,14,15]           │
└─────────────────────────────────────────────────┘
```
