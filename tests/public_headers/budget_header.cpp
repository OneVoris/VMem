#include <voris/mem/budget.hpp>

#include <type_traits>

namespace
{

struct nothrow_sink
{
    void operator()(const voris::mem::budget_event &) noexcept
    {
    }
};

struct throwing_sink
{
    void operator()(const voris::mem::budget_event &)
    {
    }
};

} // namespace

static_assert(!std::is_copy_constructible_v<voris::mem::reservation_token>);
static_assert(!std::is_copy_assignable_v<voris::mem::reservation_token>);
static_assert(std::is_move_constructible_v<voris::mem::reservation_token>);
static_assert(std::is_move_assignable_v<voris::mem::reservation_token>);
static_assert(std::is_constructible_v<voris::mem::budget_event_sink, nothrow_sink &>);
static_assert(!std::is_constructible_v<voris::mem::budget_event_sink, throwing_sink &>);
static_assert(voris::mem::max_budget_hierarchy_depth == 64U);
