#pragma once

#include <voris/mem/export.hpp>

#include <string_view>

namespace voris::mem
{

inline constexpr std::string_view library_name = "VMem";
inline constexpr std::string_view library_version = "0.1.0";

[[nodiscard]] VORIS_VMEM_API std::string_view version() noexcept;

} // namespace voris::mem
