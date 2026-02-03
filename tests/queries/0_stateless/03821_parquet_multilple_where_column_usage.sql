-- Tags: no-fasttest, no-parallel, no-replicated-database

insert into function file('03821_file.parquet') select number as x, number as y, number as z from numbers(10) settings engine_file_truncate_on_insert=1;
select * from file('03821_file.parquet') prewhere x > 5 and (x > 6 or y > 3);

insert into function file('03821_fil.parquet')
    -- Use negative numbers to test sign extension for signed types and lack of sign extension for
    -- unsigned types.
    with 5000 - number as n select

    number,

    intDiv(n, 11)::UInt8 as u8,
    n::UInt16 u16,
    n::UInt32 as u32,
    n::UInt64 as u64,
    intDiv(n, 11)::Int8 as i8,
    n::Int16 i16,
    n::Int32 as i32,
    n::Int64 as i64,

    toDate32(n*500000) as date32,
    toDateTime64(n*1e6, 3) as dt64_ms,
    toDateTime64(n*1e6, 6) as dt64_us,
    toDateTime64(n*1e6, 9) as dt64_ns,
    toDateTime64(n*1e6, 0) as dt64_s,
    toDateTime64(n*1e6, 2) as dt64_cs,

    (n/1000)::Float32 as f32,
    (n/1000)::Float64 as f64,

    n::String as s,
    n::String::FixedString(9) as fs,

    n::Decimal32(3)/1234 as d32,
    n::Decimal64(10)/12345678 as d64,
    n::Decimal128(20)/123456789012345 as d128,
    n::Decimal256(40)/123456789012345/678901234567890 as d256

    from numbers(10000) settings engine_file_truncate_on_insert=1;

SELECT DISTINCT ((u16 < 4000) AND (u16 <= 61000)) OR assumeNotNull(2) OR ((u16 = 42) OR ((61000 >= u16) AND (4000 >= u16))), deltaSumDistinct(number) IGNORE NULLS FROM file('03821_fil.parquet') PREWHERE and(and(notEquals(u32, (4000 >= u16) AND (u16 >= toUInt128(61000)) AS alias184), equals(dt64_ms, 4000 != u16)), equals(assumeNotNull(toNullable(56)), u16)) WHERE isNotDistinctFrom(u16, not(equals(u32, and(4000, (i8 >= -3) OR (assumeNotNull(42) = u16), 4000 != u16, equals(number, assumeNotNull(56))))));
