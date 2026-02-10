#pragma once

#include <magic_enum.hpp>
#include <fmt/format.h>

/// Only Coordination::OpNum needs a wider range (values from -11 to 997).
/// The default range [-128, 127] covers all other enums.
/// See contrib/magic_enum/doc/limitations.md#enum-range
namespace Coordination { enum class OpNum : int32_t; }
template <> struct magic_enum::customize::enum_range<Coordination::OpNum> {
    static constexpr int min = -20;
    static constexpr int max = 1000;
};


template <typename T> concept is_enum = std::is_enum_v<T>;

namespace detail
{
template <is_enum E, class F, size_t ...I>
constexpr void static_for(F && f, std::index_sequence<I...>)
{
    (f(std::integral_constant<E, magic_enum::enum_value<E>(I)>()) , ...);
}
}

/**
 * Iterate over enum values in compile-time (compile-time switch/case, loop unrolling).
 *
 * @example static_for<E>([](auto enum_value) { return template_func<enum_value>(); }
 * ^ enum_value can be used as a template parameter
 */
template <is_enum E, class F>
constexpr void static_for(F && f)
{
    constexpr size_t count = magic_enum::enum_count<E>();
    detail::static_for<E>(std::forward<F>(f), std::make_index_sequence<count>());
}

/// Enable printing enum values as strings via fmt + magic_enum
template <is_enum T>
struct fmt::formatter<T> : fmt::formatter<std::string_view>
{
    constexpr auto format(T value, auto& format_context) const
    {
        return formatter<string_view>::format(magic_enum::enum_name(value), format_context);
    }
};
