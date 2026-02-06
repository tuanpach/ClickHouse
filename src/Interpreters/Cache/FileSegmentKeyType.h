#pragma once

#include <base/EnumReflection.h>

namespace DB
{
    enum class FileSegmentKeyType : uint8_t
    {
        General = 0,
        System, // Segment for (metadata, index, marks, etc.)
        Data, // Segment for table data
    };

    inline String getKeyTypePrefix(FileSegmentKeyType type)
    {
        if (type == FileSegmentKeyType::General)
            return "";
        return String(magic_enum::enum_name(type));
    }

    inline String toString(FileSegmentKeyType type)
    {
        return String(magic_enum::enum_name(type));
    }
}
