#!/usr/bin/env python3
"""
Verification script for xxh3_128 hash function implementation.

This script compares ClickHouse's xxh3_128 implementation against
the official xxhash reference implementation.

Requirements:
    pip install xxhash

Usage:
    python3 verify_xxh3_128.py [clickhouse_binary_path]

Example:
    python3 verify_xxh3_128.py ./build/programs/clickhouse
"""

import subprocess
import sys
import struct

try:
    import xxhash
except ImportError:
    print("Error: xxhash module not found. Install with: pip install xxhash")
    sys.exit(1)


def clickhouse_hash(binary, query):
    """Execute a ClickHouse query and return the result."""
    try:
        result = subprocess.check_output(
            [binary, 'local', '--query', query],
            text=True,
            stderr=subprocess.DEVNULL
        ).strip()
        return result
    except subprocess.CalledProcessError as e:
        print(f"Error executing query: {e}")
        return None


def verify_string(binary, test_str, description):
    """Verify string hashing."""
    escaped = test_str.replace("'", "\\'")
    query = f"SELECT hex(xxh3_128('{escaped}'))"

    ch_result = clickhouse_hash(binary, query)
    if ch_result is None:
        return False

    ref_result = xxhash.xxh3_128_hexdigest(test_str.encode()).upper()

    match = ch_result == ref_result
    symbol = "✓" if match else "✗"

    print(f"{symbol} {description:<25} {'PASS' if match else 'FAIL'}")
    if not match:
        print(f"   ClickHouse: {ch_result}")
        print(f"   Reference:  {ref_result}")

    return match


def verify_integer(binary, sql_type, value, pack_format, description):
    """Verify integer hashing."""
    query = f"SELECT hex(xxh3_128({sql_type}({value})))"

    ch_result = clickhouse_hash(binary, query)
    if ch_result is None:
        return False

    binary_data = struct.pack(pack_format, value)
    ref_result = xxhash.xxh3_128_hexdigest(binary_data).upper()

    match = ch_result == ref_result
    symbol = "✓" if match else "✗"

    print(f"{symbol} {description:<25} {'PASS' if match else 'FAIL'}")
    if not match:
        print(f"   ClickHouse: {ch_result}")
        print(f"   Reference:  {ref_result}")

    return match


def verify_float(binary, sql_type, value, pack_format, description):
    """Verify float hashing."""
    query = f"SELECT hex(xxh3_128({sql_type}({value})))"

    ch_result = clickhouse_hash(binary, query)
    if ch_result is None:
        return False

    binary_data = struct.pack(pack_format, value)
    ref_result = xxhash.xxh3_128_hexdigest(binary_data).upper()

    match = ch_result == ref_result
    symbol = "✓" if match else "✗"

    print(f"{symbol} {description:<25} {'PASS' if match else 'FAIL'}")
    if not match:
        print(f"   ClickHouse: {ch_result}")
        print(f"   Reference:  {ref_result}")

    return match


def main():
    if len(sys.argv) > 1:
        clickhouse_binary = sys.argv[1]
    else:
        clickhouse_binary = './build/programs/clickhouse'

    print("=" * 80)
    print("xxh3_128 Implementation Verification")
    print("=" * 80)
    print(f"ClickHouse binary: {clickhouse_binary}")
    print(f"xxhash version: {xxhash.VERSION}")
    print("=" * 80)

    total_tests = 0
    passed_tests = 0

    # String tests
    print("\n1. STRING TESTS")
    print("-" * 80)
    string_tests = [
        ('ClickHouse', 'Standard string'),
        ('', 'Empty string'),
        ('test', 'Simple string'),
        ('a', 'Single character'),
        ('Hello, World!', 'Greeting'),
    ]

    for test_str, desc in string_tests:
        if verify_string(clickhouse_binary, test_str, desc):
            passed_tests += 1
        total_tests += 1

    # Integer tests
    print("\n2. INTEGER TESTS")
    print("-" * 80)
    int_tests = [
        ('toUInt8', 42, '<B', 'UInt8(42)'),
        ('toUInt16', 1000, '<H', 'UInt16(1000)'),
        ('toUInt32', 100000, '<I', 'UInt32(100000)'),
        ('toInt8', -42, '<b', 'Int8(-42)'),
        ('toInt32', -100000, '<i', 'Int32(-100000)'),
    ]

    for sql_type, value, pack_fmt, desc in int_tests:
        if verify_integer(clickhouse_binary, sql_type, value, pack_fmt, desc):
            passed_tests += 1
        total_tests += 1

    # Float tests
    print("\n3. FLOAT TESTS")
    print("-" * 80)
    float_tests = [
        ('toFloat32', 3.14159, '<f', 'Float32(3.14159)'),
        ('toFloat64', 2.718281828459045, '<d', 'Float64(e)'),
        ('toFloat32', 0.0, '<f', 'Float32(0.0)'),
    ]

    for sql_type, value, pack_fmt, desc in float_tests:
        if verify_float(clickhouse_binary, sql_type, value, pack_fmt, desc):
            passed_tests += 1
        total_tests += 1

    # Summary
    print("\n" + "=" * 80)
    print(f"RESULT: {passed_tests}/{total_tests} tests passed")

    if passed_tests == total_tests:
        print("✓ ALL TESTS PASSED - Implementation is correct")
        print("=" * 80)
        return 0
    else:
        print(f"✗ {total_tests - passed_tests} test(s) failed")
        print("=" * 80)
        return 1


if __name__ == '__main__':
    sys.exit(main())
