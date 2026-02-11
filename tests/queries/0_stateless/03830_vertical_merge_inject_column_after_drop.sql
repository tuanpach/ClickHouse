-- Vertical merge should not fail when a dropped column is still physically present in parts.
-- `injectRequiredColumns` must not pick a dropped column as the minimum-size column to inject.

DROP TABLE IF EXISTS data;

CREATE TABLE data
(
    key Int,
    value String
)
ENGINE = MergeTree()
ORDER BY key
SETTINGS
    min_bytes_for_wide_part = 0,
    min_rows_for_wide_part = 0,
    vertical_merge_algorithm_min_rows_to_activate = 0,
    vertical_merge_algorithm_min_columns_to_activate = 0;

INSERT INTO data VALUES (1, 'a');
INSERT INTO data VALUES (2, 'b');

-- Add a highly compressible column that will become the smallest column in parts
ALTER TABLE data ADD COLUMN h UInt64;
ALTER TABLE data MODIFY COLUMN h UInt64 SETTINGS mutations_sync = 2;

-- Drop the column from metadata; parts still have it physically on disk
ALTER TABLE data DROP COLUMN h SETTINGS mutations_sync = 2;

-- Vertical merge: `injectRequiredColumns` should not pick the dropped column `h`
OPTIMIZE TABLE data FINAL;

SELECT * FROM data ORDER BY key;

DROP TABLE data;
