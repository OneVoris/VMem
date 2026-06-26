#include <voris/mem/allocation.hpp>
#include <voris/mem/resource_ref.hpp>
#include <voris/mem/tag.hpp>
#include <voris/mem/usage.hpp>

#include <cstddef>
#include <type_traits>

static_assert(std::is_standard_layout_v<voris::mem::allocation>);
static_assert(std::is_trivially_copyable_v<voris::mem::allocation>);
static_assert(sizeof(voris::mem::allocation) <= sizeof(void *) + (2 * sizeof(std::size_t)));
static_assert(alignof(voris::mem::allocation) <= alignof(void *));

static_assert(std::is_standard_layout_v<voris::mem::allocation_request>);
static_assert(std::is_copy_constructible_v<voris::mem::allocation_request>);

static_assert(std::is_standard_layout_v<voris::mem::usage_snapshot>);
static_assert(std::is_trivially_copyable_v<voris::mem::usage_snapshot>);
static_assert(sizeof(voris::mem::usage_snapshot) == 6 * sizeof(std::size_t));

static_assert(std::is_trivially_copyable_v<voris::mem::resource_ref>);
static_assert(std::is_nothrow_copy_constructible_v<voris::mem::resource_ref>);
static_assert(sizeof(voris::mem::resource_ref) == 2 * sizeof(void *));

int main()
{
    return 0;
}
