#pragma once

#include <string_view>

namespace voris::mem {

enum class errc {
    out_of_memory = 1,
    invalid_alignment = 2,
    size_overflow = 3,
    budget_exceeded = 4,
    wrong_owner = 5,
    unsupported_platform = 6,
};

[[nodiscard]] constexpr std::string_view to_string(errc code) noexcept {
    switch (code) {
    case errc::out_of_memory:
        return "out_of_memory";
    case errc::invalid_alignment:
        return "invalid_alignment";
    case errc::size_overflow:
        return "size_overflow";
    case errc::budget_exceeded:
        return "budget_exceeded";
    case errc::wrong_owner:
        return "wrong_owner";
    case errc::unsupported_platform:
        return "unsupported_platform";
    }
    return "unknown";
}

} // namespace voris::mem
