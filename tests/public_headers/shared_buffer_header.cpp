#include <voris/mem/shared_buffer.hpp>

#include <type_traits>

static_assert(!std::is_copy_constructible_v<voris::mem::shared_buffer>);
static_assert(std::is_move_constructible_v<voris::mem::shared_buffer>);
