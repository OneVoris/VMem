#include <voris/mem/buffer.hpp>

#include <cstddef>
#include <type_traits>

static_assert(std::is_standard_layout_v<voris::mem::const_buffer>);
static_assert(std::is_trivially_copyable_v<voris::mem::const_buffer>);
static_assert(std::is_standard_layout_v<voris::mem::mutable_buffer>);
static_assert(std::is_trivially_copyable_v<voris::mem::mutable_buffer>);
