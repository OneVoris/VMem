#pragma once

#include <cstddef>
#include <cstdint>
#include <source_location>
#include <string_view>

namespace voris::mem
{

struct memory_tag
{
    std::string_view name{};

    friend constexpr bool operator==(memory_tag, memory_tag) noexcept = default;
};

inline constexpr memory_tag default_memory_tag{"default"};

struct source_location
{
    const char *file_name{};
    const char *function_name{};
    std::uint_least32_t line_number{};
    std::uint_least32_t column_number{};

    [[nodiscard]] static constexpr source_location from(std::source_location location) noexcept
    {
        return source_location{
            .file_name = location.file_name(),
            .function_name = location.function_name(),
            .line_number = location.line(),
            .column_number = location.column(),
        };
    }

    [[nodiscard]] constexpr std::uint_least32_t line() const noexcept
    {
        return line_number;
    }

    [[nodiscard]] constexpr std::uint_least32_t column() const noexcept
    {
        return column_number;
    }
};

struct allocation_request
{
    std::size_t size{};
    std::size_t alignment{};
    memory_tag tag{default_memory_tag};
    source_location location{};
};

[[nodiscard]] inline allocation_request make_allocation_request(
    std::size_t size, std::size_t alignment, memory_tag tag = default_memory_tag,
    std::source_location location = std::source_location::current()) noexcept
{
    return allocation_request{
        .size = size,
        .alignment = alignment,
        .tag = tag,
        .location = source_location::from(location),
    };
}

} // namespace voris::mem
