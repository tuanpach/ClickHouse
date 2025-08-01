import logging

import pytest

from helpers.cluster import ClickHouseCluster
from helpers.test_tools import TSV, assert_eq_with_retry
from helpers.wait_for_helpers import (
    wait_for_delete_empty_parts,
    wait_for_delete_inactive_parts,
)

cluster = ClickHouseCluster(__file__)
instance = cluster.add_instance(
    "instance",
    main_configs=[
        "configs/testkeeper.xml",
    ],
)
q = instance.query
path_to_data = "/var/lib/clickhouse/"


@pytest.fixture(scope="module")
def started_cluster():
    try:
        cluster.start()
        q(
            "CREATE DATABASE test ENGINE = Ordinary",
            settings={"allow_deprecated_database_ordinary": 1},
        )  # Different path in shadow/ with Atomic

        yield cluster

    finally:
        cluster.shutdown()


@pytest.fixture
def partition_table_simple(started_cluster):
    q("DROP TABLE IF EXISTS test.partition_simple")
    q(
        "CREATE TABLE test.partition_simple (date MATERIALIZED toDate(0), x UInt64, sample_key MATERIALIZED intHash64(x)) "
        "ENGINE=MergeTree PARTITION BY date SAMPLE BY sample_key ORDER BY (date,x,sample_key) "
        "SETTINGS index_granularity=8192, index_granularity_bytes=0, compress_marks=false, compress_primary_key=false, ratio_of_defaults_for_sparse_serialization=1"
    )
    q("INSERT INTO test.partition_simple ( x ) VALUES ( now() )")
    q("INSERT INTO test.partition_simple ( x ) VALUES ( now()+1 )")

    yield

    q("DROP TABLE test.partition_simple")


def test_partition_simple(partition_table_simple):
    q("ALTER TABLE test.partition_simple DETACH PARTITION 197001")
    q("ALTER TABLE test.partition_simple ATTACH PARTITION 197001")
    q("OPTIMIZE TABLE test.partition_simple")


def partition_complex_assert_columns_txt():
    data_path = instance.query(
        f"SELECT arrayElement(data_paths, 1) FROM system.tables WHERE database='test' AND name='partition_complex'"
    ).strip()
    parts = TSV(
        q(
            "SELECT name FROM system.parts WHERE database='test' AND table='partition_complex'"
        )
    )
    assert len(parts) > 0
    for part_name in parts.lines:
        path_to_columns = data_path + part_name + "/columns.txt"
        # 2 header lines + 3 columns
        assert (
            instance.exec_in_container(["wc", "-l", path_to_columns]).split()[0] == "5"
        )


def partition_complex_assert_checksums(after_detach=False):
    # Do not check increment.txt - it can be changed by other tests with FREEZE
    cmd = [
        "bash",
        "-c",
        f"cd {path_to_data} && find shadow -type f -exec"
        + " md5sum {} \\; | grep partition_complex"
        " | sed 's shadow/[0-9]*/data/[a-z0-9_-]*/ shadow/1/data/test/ g' | sort | uniq",
    ]

    # no metadata version
    if after_detach:
        checksums = (
            "082814b5aa5109160d5c0c5aff10d4df\tshadow/1/data/test/partition_complex/19700102_2_2_0/k.bin\n"
            "082814b5aa5109160d5c0c5aff10d4df\tshadow/1/data/test/partition_complex/19700201_1_1_0/v1.bin\n"
            "13cae8e658e0ca4f75c56b1fc424e150\tshadow/1/data/test/partition_complex/19700102_2_2_0/minmax_p.idx\n"
            "25daad3d9e60b45043a70c4ab7d3b1c6\tshadow/1/data/test/partition_complex/19700102_2_2_0/partition.dat\n"
            "3726312af62aec86b64a7708d5751787\tshadow/1/data/test/partition_complex/19700201_1_1_0/partition.dat\n"
            "37855b06a39b79a67ea4e86e4a3299aa\tshadow/1/data/test/partition_complex/19700102_2_2_0/checksums.txt\n"
            "38e62ff37e1e5064e9a3f605dfe09d13\tshadow/1/data/test/partition_complex/19700102_2_2_0/v1.bin\n"
            "4ae71336e44bf9bf79d2752e234818a5\tshadow/1/data/test/partition_complex/19700102_2_2_0/k.mrk\n"
            "4ae71336e44bf9bf79d2752e234818a5\tshadow/1/data/test/partition_complex/19700102_2_2_0/p.mrk\n"
            "4ae71336e44bf9bf79d2752e234818a5\tshadow/1/data/test/partition_complex/19700102_2_2_0/v1.mrk\n"
            "4ae71336e44bf9bf79d2752e234818a5\tshadow/1/data/test/partition_complex/19700201_1_1_0/k.mrk\n"
            "4ae71336e44bf9bf79d2752e234818a5\tshadow/1/data/test/partition_complex/19700201_1_1_0/p.mrk\n"
            "4ae71336e44bf9bf79d2752e234818a5\tshadow/1/data/test/partition_complex/19700201_1_1_0/v1.mrk\n"
            "55a54008ad1ba589aa210d2629c1df41\tshadow/1/data/test/partition_complex/19700201_1_1_0/primary.idx\n"
            "5f087cb3e7071bf9407e095821e2af8f\tshadow/1/data/test/partition_complex/19700201_1_1_0/checksums.txt\n"
            "77d5af402ada101574f4da114f242e02\tshadow/1/data/test/partition_complex/19700102_2_2_0/columns.txt\n"
            "77d5af402ada101574f4da114f242e02\tshadow/1/data/test/partition_complex/19700201_1_1_0/columns.txt\n"
            "7e1a7e6aeff3a879f5787bbddd6553eb\tshadow/1/data/test/partition_complex/19700102_2_2_0/columns_substreams.txt\n"
            "7e1a7e6aeff3a879f5787bbddd6553eb\tshadow/1/data/test/partition_complex/19700201_1_1_0/columns_substreams.txt\n"
            "88cdc31ded355e7572d68d8cde525d3a\tshadow/1/data/test/partition_complex/19700201_1_1_0/p.bin\n"
            "9e688c58a5487b8eaf69c9e1005ad0bf\tshadow/1/data/test/partition_complex/19700102_2_2_0/primary.idx\n"
            "c0904274faa8f3f06f35666cc9c5bd2f\tshadow/1/data/test/partition_complex/19700102_2_2_0/default_compression_codec.txt\n"
            "c0904274faa8f3f06f35666cc9c5bd2f\tshadow/1/data/test/partition_complex/19700201_1_1_0/default_compression_codec.txt\n"
            "c4ca4238a0b923820dcc509a6f75849b\tshadow/1/data/test/partition_complex/19700102_2_2_0/count.txt\n"
            "c4ca4238a0b923820dcc509a6f75849b\tshadow/1/data/test/partition_complex/19700201_1_1_0/count.txt\n"
            "cfcb770c3ecd0990dcceb1bde129e6c6\tshadow/1/data/test/partition_complex/19700102_2_2_0/p.bin\n"
            "e2af3bef1fd129aea73a890ede1e7a30\tshadow/1/data/test/partition_complex/19700201_1_1_0/k.bin\n"
            "f2312862cc01adf34a93151377be2ddf\tshadow/1/data/test/partition_complex/19700201_1_1_0/minmax_p.idx\n"
        )
    else:
        checksums = (
            "082814b5aa5109160d5c0c5aff10d4df\tshadow/1/data/test/partition_complex/19700102_2_2_0/k.bin\n"
            "082814b5aa5109160d5c0c5aff10d4df\tshadow/1/data/test/partition_complex/19700201_1_1_0/v1.bin\n"
            "13cae8e658e0ca4f75c56b1fc424e150\tshadow/1/data/test/partition_complex/19700102_2_2_0/minmax_p.idx\n"
            "25daad3d9e60b45043a70c4ab7d3b1c6\tshadow/1/data/test/partition_complex/19700102_2_2_0/partition.dat\n"
            "3726312af62aec86b64a7708d5751787\tshadow/1/data/test/partition_complex/19700201_1_1_0/partition.dat\n"
            "37855b06a39b79a67ea4e86e4a3299aa\tshadow/1/data/test/partition_complex/19700102_2_2_0/checksums.txt\n"
            "38e62ff37e1e5064e9a3f605dfe09d13\tshadow/1/data/test/partition_complex/19700102_2_2_0/v1.bin\n"
            "4ae71336e44bf9bf79d2752e234818a5\tshadow/1/data/test/partition_complex/19700102_2_2_0/k.mrk\n"
            "4ae71336e44bf9bf79d2752e234818a5\tshadow/1/data/test/partition_complex/19700102_2_2_0/p.mrk\n"
            "4ae71336e44bf9bf79d2752e234818a5\tshadow/1/data/test/partition_complex/19700102_2_2_0/v1.mrk\n"
            "4ae71336e44bf9bf79d2752e234818a5\tshadow/1/data/test/partition_complex/19700201_1_1_0/k.mrk\n"
            "4ae71336e44bf9bf79d2752e234818a5\tshadow/1/data/test/partition_complex/19700201_1_1_0/p.mrk\n"
            "4ae71336e44bf9bf79d2752e234818a5\tshadow/1/data/test/partition_complex/19700201_1_1_0/v1.mrk\n"
            "55a54008ad1ba589aa210d2629c1df41\tshadow/1/data/test/partition_complex/19700201_1_1_0/primary.idx\n"
            "5f087cb3e7071bf9407e095821e2af8f\tshadow/1/data/test/partition_complex/19700201_1_1_0/checksums.txt\n"
            "77d5af402ada101574f4da114f242e02\tshadow/1/data/test/partition_complex/19700102_2_2_0/columns.txt\n"
            "77d5af402ada101574f4da114f242e02\tshadow/1/data/test/partition_complex/19700201_1_1_0/columns.txt\n"
            "7e1a7e6aeff3a879f5787bbddd6553eb\tshadow/1/data/test/partition_complex/19700102_2_2_0/columns_substreams.txt\n"
            "7e1a7e6aeff3a879f5787bbddd6553eb\tshadow/1/data/test/partition_complex/19700201_1_1_0/columns_substreams.txt\n"
            "88cdc31ded355e7572d68d8cde525d3a\tshadow/1/data/test/partition_complex/19700201_1_1_0/p.bin\n"
            "9e688c58a5487b8eaf69c9e1005ad0bf\tshadow/1/data/test/partition_complex/19700102_2_2_0/primary.idx\n"
            "c0904274faa8f3f06f35666cc9c5bd2f\tshadow/1/data/test/partition_complex/19700102_2_2_0/default_compression_codec.txt\n"
            "c0904274faa8f3f06f35666cc9c5bd2f\tshadow/1/data/test/partition_complex/19700201_1_1_0/default_compression_codec.txt\n"
            "c4ca4238a0b923820dcc509a6f75849b\tshadow/1/data/test/partition_complex/19700102_2_2_0/count.txt\n"
            "c4ca4238a0b923820dcc509a6f75849b\tshadow/1/data/test/partition_complex/19700201_1_1_0/count.txt\n"
            "cfcb770c3ecd0990dcceb1bde129e6c6\tshadow/1/data/test/partition_complex/19700102_2_2_0/p.bin\n"
            "cfcd208495d565ef66e7dff9f98764da\tshadow/1/data/test/partition_complex/19700102_2_2_0/metadata_version.txt\n"
            "cfcd208495d565ef66e7dff9f98764da\tshadow/1/data/test/partition_complex/19700201_1_1_0/metadata_version.txt\n"
            "e2af3bef1fd129aea73a890ede1e7a30\tshadow/1/data/test/partition_complex/19700201_1_1_0/k.bin\n"
            "f2312862cc01adf34a93151377be2ddf\tshadow/1/data/test/partition_complex/19700201_1_1_0/minmax_p.idx\n"
        )

    assert TSV(instance.exec_in_container(cmd).replace("  ", "\t")) == TSV(checksums)


@pytest.fixture
def partition_table_complex(started_cluster):
    q("DROP TABLE IF EXISTS test.partition_complex")
    q(
        "CREATE TABLE test.partition_complex (p Date, k Int8, v1 Int8 MATERIALIZED k + 1) "
        "ENGINE = MergeTree PARTITION BY p ORDER BY k SETTINGS index_granularity=1, index_granularity_bytes=0, compress_marks=false, compress_primary_key=false, ratio_of_defaults_for_sparse_serialization=1, replace_long_file_name_to_hash=false"
    )
    q("INSERT INTO test.partition_complex (p, k) VALUES(toDate(31), 1)")
    q("INSERT INTO test.partition_complex (p, k) VALUES(toDate(1), 2)")

    yield

    q("DROP TABLE test.partition_complex")


def test_partition_complex(partition_table_complex):
    partition_complex_assert_columns_txt()

    q("ALTER TABLE test.partition_complex FREEZE")

    partition_complex_assert_checksums(True)

    q("ALTER TABLE test.partition_complex DETACH PARTITION 197001")
    q("ALTER TABLE test.partition_complex ATTACH PARTITION 197001")

    partition_complex_assert_columns_txt()

    q("ALTER TABLE test.partition_complex MODIFY COLUMN v1 Int8")

    # Check the backup hasn't changed
    partition_complex_assert_checksums(True)

    q("OPTIMIZE TABLE test.partition_complex")

    expected = TSV("31\t1\t2\n" "1\t2\t3")
    res = q("SELECT toUInt16(p), k, v1 FROM test.partition_complex ORDER BY k")
    assert TSV(res) == expected


@pytest.fixture
def cannot_attach_active_part_table(started_cluster):
    q("DROP TABLE IF EXISTS test.attach_active")
    q(
        "CREATE TABLE test.attach_active (n UInt64) ENGINE = MergeTree() PARTITION BY intDiv(n, 4) ORDER BY n SETTINGS compress_marks=false, compress_primary_key=false, ratio_of_defaults_for_sparse_serialization=1"
    )
    q("INSERT INTO test.attach_active SELECT number FROM system.numbers LIMIT 16")

    yield

    q("DROP TABLE test.attach_active")


def test_cannot_attach_active_part(cannot_attach_active_part_table):
    error = instance.client.query_and_get_error(
        "ALTER TABLE test.attach_active ATTACH PART '../1_2_2_0'"
    )
    print(error)
    assert 0 <= error.find("Invalid part name")

    res = q(
        "SElECT name FROM system.parts WHERE table='attach_active' AND database='test' ORDER BY name"
    )
    assert TSV(res) == TSV("0_1_1_0\n1_2_2_0\n2_3_3_0\n3_4_4_0")
    assert TSV(q("SElECT count(), sum(n) FROM test.attach_active")) == TSV("16\t120")


@pytest.fixture
def attach_check_all_parts_table(started_cluster):
    q("SYSTEM STOP MERGES")
    q("DROP TABLE IF EXISTS test.attach_partition")
    q(
        "CREATE TABLE test.attach_partition (n UInt64) ENGINE = MergeTree() PARTITION BY intDiv(n, 8) ORDER BY n "
        "SETTINGS compress_marks=false, compress_primary_key=false, ratio_of_defaults_for_sparse_serialization=1, old_parts_lifetime=0"
    )
    q(
        "INSERT INTO test.attach_partition SELECT number FROM system.numbers WHERE number % 2 = 0 LIMIT 8"
    )
    q(
        "INSERT INTO test.attach_partition SELECT number FROM system.numbers WHERE number % 2 = 1 LIMIT 8"
    )

    yield

    q("DROP TABLE test.attach_partition")
    q("SYSTEM START MERGES")


def test_attach_check_all_parts(attach_check_all_parts_table):
    q("ALTER TABLE test.attach_partition DETACH PARTITION 0")

    wait_for_delete_empty_parts(instance, "test.attach_partition")
    wait_for_delete_inactive_parts(instance, "test.attach_partition")

    data_path = instance.query(
        f"SELECT arrayElement(data_paths, 1) FROM system.tables WHERE database='test' AND name='attach_partition'"
    ).strip()

    path_to_detached = f"{data_path}/detached/"
    instance.exec_in_container(["mkdir", "{}".format(path_to_detached + "0_5_5_0")])
    instance.exec_in_container(
        [
            "cp",
            "-pr",
            path_to_detached + "0_1_1_0",
            path_to_detached + "attaching_0_6_6_0",
        ]
    )
    instance.exec_in_container(
        [
            "cp",
            "-pr",
            path_to_detached + "0_3_3_0",
            path_to_detached + "deleting_0_7_7_0",
        ]
    )

    error = instance.client.query_and_get_error(
        "ALTER TABLE test.attach_partition ATTACH PARTITION 0"
    )
    assert 0 <= error.find("No columns in part 0_5_5_0") or 0 <= error.find(
        "No columns.txt in part 0_5_5_0"
    )

    parts = q(
        "SElECT name FROM system.parts "
        "WHERE table='attach_partition' AND database='test' AND active ORDER BY name"
    )
    assert TSV(parts) == TSV("1_2_2_0\n1_4_4_0")
    detached = q(
        "SELECT name FROM system.detached_parts "
        "WHERE table='attach_partition' AND database='test' ORDER BY name"
    )
    assert TSV(detached) == TSV(
        "0_1_1_0\n0_3_3_0\n0_5_5_0\nattaching_0_6_6_0\ndeleting_0_7_7_0"
    )

    instance.exec_in_container(["rm", "-r", path_to_detached + "0_5_5_0"])

    q("ALTER TABLE test.attach_partition ATTACH PARTITION 0")
    parts = q(
        "SElECT name FROM system.parts WHERE table='attach_partition' AND database='test' ORDER BY name"
    )
    expected = "0_5_5_0\n0_6_6_0\n1_2_2_0\n1_4_4_0"
    assert TSV(parts) == TSV(expected)
    assert TSV(q("SElECT count(), sum(n) FROM test.attach_partition")) == TSV("16\t120")

    detached = q(
        "SELECT name FROM system.detached_parts "
        "WHERE table='attach_partition' AND database='test' ORDER BY name"
    )
    assert TSV(detached) == TSV("attaching_0_6_6_0\ndeleting_0_7_7_0")


@pytest.fixture
def drop_detached_parts_table(started_cluster):
    q("SYSTEM STOP MERGES")
    q("DROP TABLE IF EXISTS test.drop_detached")
    q(
        "CREATE TABLE test.drop_detached (n UInt64) ENGINE = MergeTree() PARTITION BY intDiv(n, 8) ORDER BY n SETTINGS compress_marks=false, compress_primary_key=false, ratio_of_defaults_for_sparse_serialization=1"
    )
    q(
        "INSERT INTO test.drop_detached SELECT number FROM system.numbers WHERE number % 2 = 0 LIMIT 8"
    )
    q(
        "INSERT INTO test.drop_detached SELECT number FROM system.numbers WHERE number % 2 = 1 LIMIT 8"
    )

    yield

    q("DROP TABLE test.drop_detached")
    q("SYSTEM START MERGES")


def test_drop_detached_parts(drop_detached_parts_table):
    s = {"allow_drop_detached": 1}
    q("ALTER TABLE test.drop_detached DETACH PARTITION 0")
    q("ALTER TABLE test.drop_detached DETACH PARTITION 1")

    data_path = instance.query(
        f"SELECT arrayElement(data_paths, 1) FROM system.tables WHERE database='test' AND name='drop_detached'"
    ).strip()

    path_to_detached = f"{data_path}/detached/"
    instance.exec_in_container(
        ["mkdir", "{}".format(path_to_detached + "attaching_0_6_6_0")]
    )
    instance.exec_in_container(
        ["mkdir", "{}".format(path_to_detached + "deleting_0_7_7_0")]
    )
    instance.exec_in_container(
        ["mkdir", "{}".format(path_to_detached + "any_other_name")]
    )
    instance.exec_in_container(
        ["mkdir", "{}".format(path_to_detached + "prefix_1_2_2_0_0")]
    )

    error = instance.client.query_and_get_error(
        "ALTER TABLE test.drop_detached DROP DETACHED PART '../1_2_2_0'", settings=s
    )
    assert 0 <= error.find("Invalid part name")

    q("ALTER TABLE test.drop_detached DROP DETACHED PART '0_1_1_0'", settings=s)

    error = instance.client.query_and_get_error(
        "ALTER TABLE test.drop_detached DROP DETACHED PART 'attaching_0_6_6_0'",
        settings=s,
    )
    assert 0 <= error.find("Cannot drop part")

    error = instance.client.query_and_get_error(
        "ALTER TABLE test.drop_detached DROP DETACHED PART 'deleting_0_7_7_0'",
        settings=s,
    )
    assert 0 <= error.find("Cannot drop part")

    q("ALTER TABLE test.drop_detached DROP DETACHED PART 'any_other_name'", settings=s)

    detached = q(
        "SElECT name FROM system.detached_parts WHERE table='drop_detached' AND database='test' ORDER BY name"
    )
    assert TSV(detached) == TSV(
        "0_3_3_0\n1_2_2_0\n1_4_4_0\nattaching_0_6_6_0\ndeleting_0_7_7_0\nprefix_1_2_2_0_0"
    )

    q("ALTER TABLE test.drop_detached DROP DETACHED PARTITION 1", settings=s)
    detached = q(
        "SElECT name FROM system.detached_parts WHERE table='drop_detached' AND database='test' ORDER BY name"
    )
    assert TSV(detached) == TSV("0_3_3_0\nattaching_0_6_6_0\ndeleting_0_7_7_0")


def test_system_detached_parts(drop_detached_parts_table):
    q("drop table if exists sdp_0 sync")
    q("drop table if exists sdp_1 sync")
    q("drop table if exists sdp_2 sync")
    q("drop table if exists sdp_3 sync")

    q(
        "create table sdp_0 (n int, x int) engine=MergeTree order by n SETTINGS compress_marks=false, compress_primary_key=false, ratio_of_defaults_for_sparse_serialization=1"
    )
    q(
        "create table sdp_1 (n int, x int) engine=MergeTree order by n partition by x SETTINGS compress_marks=false, compress_primary_key=false, ratio_of_defaults_for_sparse_serialization=1"
    )
    q(
        "create table sdp_2 (n int, x String) engine=MergeTree order by n partition by x SETTINGS compress_marks=false, compress_primary_key=false, ratio_of_defaults_for_sparse_serialization=1"
    )
    q(
        "create table sdp_3 (n int, x Enum('broken' = 0, 'all' = 1)) engine=MergeTree order by n partition by x"
    )

    for i in range(0, 4):
        q("system stop merges sdp_{}".format(i))
        q("insert into sdp_{} values (0, 0)".format(i))
        q("insert into sdp_{} values (1, 1)".format(i))
        for p in q(
            "select distinct partition_id from system.parts where table='sdp_{}'".format(
                i
            )
        )[:-1].split("\n"):
            q("alter table sdp_{} detach partition id '{}'".format(i, p))

    for i in range(0, 4):
        table_name = f"sdp_{i}"
        data_path = instance.query(
            f"SELECT arrayElement(data_paths, 1) FROM system.tables WHERE database='default' AND name='{table_name}'"
        ).strip()
        path_to_detached = data_path + "detached/{}"
        instance.exec_in_container(
            ["mkdir", path_to_detached.format("attaching_0_6_6_0")]
        )
        instance.exec_in_container(
            ["mkdir", path_to_detached.format("deleting_0_7_7_0")]
        )
        instance.exec_in_container(["mkdir", path_to_detached.format("any_other_name")])
        instance.exec_in_container(
            ["mkdir", path_to_detached.format("prefix_1_2_2_0_0")]
        )

        instance.exec_in_container(
            ["mkdir", path_to_detached.format("ignored_202107_714380_714380_0")]
        )
        instance.exec_in_container(
            ["mkdir", path_to_detached.format("broken_202107_714380_714380_123")]
        )
        instance.exec_in_container(
            ["mkdir", path_to_detached.format("clone_all_714380_714380_42")]
        )
        instance.exec_in_container(
            ["mkdir", path_to_detached.format("clone_all_714380_714380_42_123")]
        )
        instance.exec_in_container(
            [
                "mkdir",
                path_to_detached.format(
                    "broken-on-start_6711e2b2592d86d18fc0f260cf33ef2b_714380_714380_42_123",
                ),
            ]
        )

    res = q(
        "select system.detached_parts.* except (bytes_on_disk, `path`, modification_time) from system.detached_parts where table like 'sdp_%' order by table, name"
    )
    assert (
        res == "default\tsdp_0\tall\tall_1_1_0\tdefault\t\t1\t1\t0\n"
        "default\tsdp_0\tall\tall_2_2_0\tdefault\t\t2\t2\t0\n"
        "default\tsdp_0\t\\N\tany_other_name\tdefault\t\\N\t\\N\t\\N\t\\N\n"
        "default\tsdp_0\t0\tattaching_0_6_6_0\tdefault\tattaching\t6\t6\t0\n"
        "default\tsdp_0\t6711e2b2592d86d18fc0f260cf33ef2b\tbroken-on-start_6711e2b2592d86d18fc0f260cf33ef2b_714380_714380_42_123\tdefault\tbroken-on-start\t714380\t714380\t42\n"
        "default\tsdp_0\t202107\tbroken_202107_714380_714380_123\tdefault\tbroken\t714380\t714380\t123\n"
        "default\tsdp_0\tall\tclone_all_714380_714380_42\tdefault\tclone\t714380\t714380\t42\n"
        "default\tsdp_0\tall\tclone_all_714380_714380_42_123\tdefault\tclone\t714380\t714380\t42\n"
        "default\tsdp_0\t0\tdeleting_0_7_7_0\tdefault\tdeleting\t7\t7\t0\n"
        "default\tsdp_0\t202107\tignored_202107_714380_714380_0\tdefault\tignored\t714380\t714380\t0\n"
        "default\tsdp_0\t1\tprefix_1_2_2_0_0\tdefault\tprefix\t2\t2\t0\n"
        "default\tsdp_1\t0\t0_1_1_0\tdefault\t\t1\t1\t0\n"
        "default\tsdp_1\t1\t1_2_2_0\tdefault\t\t2\t2\t0\n"
        "default\tsdp_1\t\\N\tany_other_name\tdefault\t\\N\t\\N\t\\N\t\\N\n"
        "default\tsdp_1\t0\tattaching_0_6_6_0\tdefault\tattaching\t6\t6\t0\n"
        "default\tsdp_1\t6711e2b2592d86d18fc0f260cf33ef2b\tbroken-on-start_6711e2b2592d86d18fc0f260cf33ef2b_714380_714380_42_123\tdefault\tbroken-on-start\t714380\t714380\t42\n"
        "default\tsdp_1\t202107\tbroken_202107_714380_714380_123\tdefault\tbroken\t714380\t714380\t123\n"
        "default\tsdp_1\tall\tclone_all_714380_714380_42\tdefault\tclone\t714380\t714380\t42\n"
        "default\tsdp_1\tall\tclone_all_714380_714380_42_123\tdefault\tclone\t714380\t714380\t42\n"
        "default\tsdp_1\t0\tdeleting_0_7_7_0\tdefault\tdeleting\t7\t7\t0\n"
        "default\tsdp_1\t202107\tignored_202107_714380_714380_0\tdefault\tignored\t714380\t714380\t0\n"
        "default\tsdp_1\t1\tprefix_1_2_2_0_0\tdefault\tprefix\t2\t2\t0\n"
        "default\tsdp_2\t58ed7160db50ea45e1c6aa694c8cbfd1\t58ed7160db50ea45e1c6aa694c8cbfd1_1_1_0\tdefault\t\t1\t1\t0\n"
        "default\tsdp_2\t6711e2b2592d86d18fc0f260cf33ef2b\t6711e2b2592d86d18fc0f260cf33ef2b_2_2_0\tdefault\t\t2\t2\t0\n"
        "default\tsdp_2\t\\N\tany_other_name\tdefault\t\\N\t\\N\t\\N\t\\N\n"
        "default\tsdp_2\t0\tattaching_0_6_6_0\tdefault\tattaching\t6\t6\t0\n"
        "default\tsdp_2\t6711e2b2592d86d18fc0f260cf33ef2b\tbroken-on-start_6711e2b2592d86d18fc0f260cf33ef2b_714380_714380_42_123\tdefault\tbroken-on-start\t714380\t714380\t42\n"
        "default\tsdp_2\t202107\tbroken_202107_714380_714380_123\tdefault\tbroken\t714380\t714380\t123\n"
        "default\tsdp_2\tall\tclone_all_714380_714380_42\tdefault\tclone\t714380\t714380\t42\n"
        "default\tsdp_2\tall\tclone_all_714380_714380_42_123\tdefault\tclone\t714380\t714380\t42\n"
        "default\tsdp_2\t0\tdeleting_0_7_7_0\tdefault\tdeleting\t7\t7\t0\n"
        "default\tsdp_2\t202107\tignored_202107_714380_714380_0\tdefault\tignored\t714380\t714380\t0\n"
        "default\tsdp_2\t1\tprefix_1_2_2_0_0\tdefault\tprefix\t2\t2\t0\n"
        "default\tsdp_3\t0\t0_1_1_0\tdefault\t\t1\t1\t0\n"
        "default\tsdp_3\t1\t1_2_2_0\tdefault\t\t2\t2\t0\n"
        "default\tsdp_3\t\\N\tany_other_name\tdefault\t\\N\t\\N\t\\N\t\\N\n"
        "default\tsdp_3\t0\tattaching_0_6_6_0\tdefault\tattaching\t6\t6\t0\n"
        "default\tsdp_3\t6711e2b2592d86d18fc0f260cf33ef2b\tbroken-on-start_6711e2b2592d86d18fc0f260cf33ef2b_714380_714380_42_123\tdefault\tbroken-on-start\t714380\t714380\t42\n"
        "default\tsdp_3\t202107\tbroken_202107_714380_714380_123\tdefault\tbroken\t714380\t714380\t123\n"
        "default\tsdp_3\tall\tclone_all_714380_714380_42\tdefault\tclone\t714380\t714380\t42\n"
        "default\tsdp_3\tall\tclone_all_714380_714380_42_123\tdefault\tclone\t714380\t714380\t42\n"
        "default\tsdp_3\t0\tdeleting_0_7_7_0\tdefault\tdeleting\t7\t7\t0\n"
        "default\tsdp_3\t202107\tignored_202107_714380_714380_0\tdefault\tignored\t714380\t714380\t0\n"
        "default\tsdp_3\t1\tprefix_1_2_2_0_0\tdefault\tprefix\t2\t2\t0\n"
    )

    for i in range(0, 4):
        for p in q(
            "select distinct partition_id from system.detached_parts where table='sdp_{}' and partition_id is not null".format(
                i
            )
        )[:-1].split("\n"):
            q("alter table sdp_{} attach partition id '{}'".format(i, p))

    assert (
        q("select n, x::int AS x, count() from merge('default', '^sdp_') group by n, x")
        == "0\t0\t4\n1\t1\t4\n"
    )


def test_detached_part_dir_exists(started_cluster):
    q("drop table if exists detached_part_dir_exists sync")
    q(
        "create table detached_part_dir_exists (n int) engine=MergeTree order by n "
        "SETTINGS compress_marks=false, compress_primary_key=false, ratio_of_defaults_for_sparse_serialization=1, old_parts_lifetime=0"
    )
    data_path = q(
        f"SELECT arrayElement(data_paths, 1) FROM system.tables WHERE database='default' AND name='detached_part_dir_exists'"
    ).strip()
     
    q("insert into detached_part_dir_exists select 1")  # will create all_1_1_0
    q(
        "alter table detached_part_dir_exists detach partition id 'all'"
    )  # will move all_1_1_0 to detached/all_1_1_0 and create all_1_1_1

    wait_for_delete_empty_parts(instance, "detached_part_dir_exists")
    wait_for_delete_inactive_parts(instance, "detached_part_dir_exists")

    q("detach table detached_part_dir_exists")
    q("attach table detached_part_dir_exists")
    q("insert into detached_part_dir_exists select 1")  # will create all_1_1_0
    q("insert into detached_part_dir_exists select 1")  # will create all_2_2_0

    assert (
        q(
            "select name from system.parts where table='detached_part_dir_exists' and active order by name"
        )
        == "all_1_1_0\nall_2_2_0\n"
    )
    instance.exec_in_container(
        [
            "bash",
            "-c",
            f"mkdir {data_path}detached/all_2_2_0",
        ],
        privileged=True,
    )
    instance.exec_in_container(
        [
            "bash",
            "-c",
            f"touch {data_path}detached/all_2_2_0/file",
        ],
        privileged=True,
    )
    q(
        "alter table detached_part_dir_exists detach partition id 'all'"
    )  # directories already exist, but it's ok
    assert (
        q(
            "select name from system.detached_parts where table='detached_part_dir_exists' order by name"
        )
        == "all_1_1_0\nall_1_1_0_try1\nall_2_2_0\nall_2_2_0_try1\n"
    )
    q("drop table detached_part_dir_exists")


def test_make_clone_in_detached(started_cluster):
    q("drop table if exists clone_in_detached sync")
    q(
        "create table clone_in_detached (n int, m String) engine=ReplicatedMergeTree('/clone_in_detached', '1') order by n SETTINGS compress_marks=false, compress_primary_key=false, ratio_of_defaults_for_sparse_serialization=1"
    )

    path = instance.query(
        f"SELECT arrayElement(data_paths, 1) FROM system.tables WHERE database='default' AND name='clone_in_detached'"
    ).strip()

    # broken part already detached
    q("insert into clone_in_detached values (42, '¯-_(ツ)_-¯')")
    instance.exec_in_container(["rm", path + "all_0_0_0/data.bin"])
    instance.exec_in_container(
        ["cp", "-r", path + "all_0_0_0", path + "detached/broken_all_0_0_0"]
    )
    assert_eq_with_retry(instance, "select * from clone_in_detached", "\n")
    assert [
        "broken_all_0_0_0",
    ] == sorted(
        instance.exec_in_container(["ls", path + "detached/"]).strip().split("\n")
    )

    # there's a directory with the same name, but different content
    q("insert into clone_in_detached values (43, '¯-_(ツ)_-¯')")
    instance.exec_in_container(["rm", path + "all_1_1_0/data.bin"])
    instance.exec_in_container(
        ["cp", "-r", path + "all_1_1_0", path + "detached/broken_all_1_1_0"]
    )
    instance.exec_in_container(["rm", path + "detached/broken_all_1_1_0/primary.idx"])
    instance.exec_in_container(
        ["cp", "-r", path + "all_1_1_0", path + "detached/broken_all_1_1_0_try0"]
    )
    instance.exec_in_container(
        [
            "bash",
            "-c",
            "echo 'broken' > {}".format(
                path + "detached/broken_all_1_1_0_try0/checksums.txt"
            ),
        ]
    )
    assert_eq_with_retry(instance, "select * from clone_in_detached", "\n")
    assert [
        "broken_all_0_0_0",
        "broken_all_1_1_0",
        "broken_all_1_1_0_try0",
        "broken_all_1_1_0_try1",
    ] == sorted(
        instance.exec_in_container(["ls", path + "detached/"]).strip().split("\n")
    )

    # there are directories with the same name, but different content, and part already detached
    q("insert into clone_in_detached values (44, '¯-_(ツ)_-¯')")
    instance.exec_in_container(["rm", path + "all_2_2_0/data.bin"])
    instance.exec_in_container(
        ["cp", "-r", path + "all_2_2_0", path + "detached/broken_all_2_2_0"]
    )
    instance.exec_in_container(["rm", path + "detached/broken_all_2_2_0/primary.idx"])
    instance.exec_in_container(
        ["cp", "-r", path + "all_2_2_0", path + "detached/broken_all_2_2_0_try0"]
    )
    instance.exec_in_container(
        [
            "bash",
            "-c",
            "echo 'broken' > {}".format(
                path + "detached/broken_all_2_2_0_try0/checksums.txt"
            ),
        ]
    )
    instance.exec_in_container(
        ["cp", "-r", path + "all_2_2_0", path + "detached/broken_all_2_2_0_try1"]
    )
    assert_eq_with_retry(instance, "select * from clone_in_detached", "\n")
    assert [
        "broken_all_0_0_0",
        "broken_all_1_1_0",
        "broken_all_1_1_0_try0",
        "broken_all_1_1_0_try1",
        "broken_all_2_2_0",
        "broken_all_2_2_0_try0",
        "broken_all_2_2_0_try1",
    ] == sorted(
        instance.exec_in_container(["ls", path + "detached/"]).strip().split("\n")
    )
