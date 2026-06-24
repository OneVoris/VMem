#include <voris/mem/buffer_chain.hpp>

#include <type_traits>

static_assert(!std::is_copy_constructible_v<voris::mem::buffer_chain>);
static_assert(std::is_move_constructible_v<voris::mem::buffer_chain>);
