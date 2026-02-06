DROP TABLE IF EXISTS t_object_storage_update;

CREATE TABLE t_object_storage_update (c0 Int32)
ENGINE = S3(s3_conn, filename = currentDatabase() || '_test_03903_alter_update.parquet', format = Parquet);

INSERT INTO t_object_storage_update VALUES (0);

ALTER TABLE t_object_storage_update UPDATE c0 = 1 WHERE TRUE; -- { serverError NOT_IMPLEMENTED }

DROP TABLE IF EXISTS t_object_storage_update;
