# xxh3_128 Hash Function Tests

This directory contains verification tools for the `xxh3_128` hash function implementation.

## Overview

The `xxh3_128` function implements the 128-bit variant of the XXH3 hash algorithm, matching the official [xxHash](https://github.com/Cyan4973/xxHash) reference implementation.

**Return Type**: `UInt128` - A 128-bit unsigned integer representing the hash value in big-endian format.
**Display**: Use `hex(xxh3_128(...))` to display the hash as a hexadecimal string.

## Test Files

### SQL Tests

1. **`03825_xxh3_128_hash_function.sql`** - Basic string hashing tests
   - Tests simple string inputs
   - Verifies empty string handling
   - Reference values included in comments

2. **`03826_xxh3_128_types.sql`** - Comprehensive type testing
   - Tests integers (signed/unsigned, various sizes)
   - Tests floating point numbers (Float32, Float64)
   - Tests arrays and tuples
   - Tests multiple arguments
   - Includes verification instructions for each type

### Verification Script

**`verify_xxh3_128.py`** - Automated verification against reference implementation

## Manual Verification

### Prerequisites

```bash
pip install xxhash
```

### Verify String Hashing

```bash
# For strings:
python3 -c "import xxhash; print(xxhash.xxh3_128_hexdigest(b'ClickHouse').upper())"
# Expected: 14C27B7BEF95D36FECF5520CA2DAF030

# Compare with ClickHouse:
./build/programs/clickhouse local --query "SELECT hex(xxh3_128('ClickHouse'))"
# Output: 14C27B7BEF95D36FECF5520CA2DAF030
```

### Verify Integer Hashing

Integers are hashed using their little-endian binary representation:

```bash
# UInt8(42)
python3 -c "import xxhash, struct; print(xxhash.xxh3_128_hexdigest(struct.pack('<B', 42)).upper())"
# Expected: 14C9AE9594C463C479D03016B7AEED0D

# UInt32(100000)
python3 -c "import xxhash, struct; print(xxhash.xxh3_128_hexdigest(struct.pack('<I', 100000)).upper())"
# Expected: 4A45E47B73E1730370EE2DE3EE2075AE

# Int32(-100000)
python3 -c "import xxhash, struct; print(xxhash.xxh3_128_hexdigest(struct.pack('<i', -100000)).upper())"
# Expected: 0C4FCD8AFFCDB8C7B7F33C8AE911D670
```

### Verify Float Hashing

Floats are hashed using their little-endian IEEE 754 binary representation:

```bash
# Float32(3.14159)
python3 -c "import xxhash, struct; print(xxhash.xxh3_128_hexdigest(struct.pack('<f', 3.14159)).upper())"
# Expected: 7E28C21047BCFAE661980E66569925B3

# Float64(2.718281828459045)
python3 -c "import xxhash, struct; print(xxhash.xxh3_128_hexdigest(struct.pack('<d', 2.718281828459045)).upper())"
# Expected: 346AF06F305AD97FAFBF35BFE437ABE0
```

## Automated Verification

Run the verification script to test all cases:

```bash
# Using default binary location
python3 verify_xxh3_128.py

# Or specify binary path
python3 verify_xxh3_128.py /path/to/clickhouse
```

Example output:
```
================================================================================
xxh3_128 Implementation Verification
================================================================================
ClickHouse binary: ./build/programs/clickhouse
xxhash version: 3.6.0
================================================================================

1. STRING TESTS
--------------------------------------------------------------------------------
✓ Standard string           PASS
✓ Empty string              PASS
✓ Simple string             PASS
...

================================================================================
RESULT: 13/13 tests passed
✓ ALL TESTS PASSED - Implementation is correct
================================================================================
```

## Implementation Details

### Key Features

- **Algorithm**: XXH3 128-bit variant from xxHash library
- **Output**: 128-bit hash returned as `UInt128` (unsigned 128-bit integer)
- **Format**: Big-endian integer representation (mathematically equivalent to canonical hex format)
- **Compatibility**: Matches official xxHash reference implementation exactly
- **Display**: `hex(xxh3_128(...))` produces the canonical hexadecimal representation

### Data Type Handling

1. **Strings**: Hashed directly as UTF-8 bytes
2. **Integers**: Converted to little-endian binary representation
3. **Floats**: Converted to little-endian IEEE 754 binary
4. **Arrays/Tuples**: Use ClickHouse's internal serialization format
5. **Multiple arguments**: Combined using `combineHashesFunc`

### Files Modified

- `src/Functions/FunctionsHashing.h` - Implementation
- `src/Functions/FunctionsHashingMisc.cpp` - Function registration
- `tests/queries/0_stateless/03825_xxh3_128_hash_function.sql` - Basic tests
- `tests/queries/0_stateless/03826_xxh3_128_types.sql` - Type tests

## Reference

- [xxHash Official Repository](https://github.com/Cyan4973/xxHash)
- [xxHash Documentation](https://xxhash.com/)
- [Python xxhash Package](https://pypi.org/project/xxhash/)

## Notes

- Complex types (arrays, tuples, maps) use ClickHouse's internal representation
- The hash output may not directly match raw binary xxhash for these types
- All primitive types (strings, integers, floats) match the reference exactly
