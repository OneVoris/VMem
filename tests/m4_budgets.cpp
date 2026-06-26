#include <voris/mem/budget.hpp>

#undef NDEBUG

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <vector>

#if defined(NDEBUG)
#error "VMem assert-style tests require assertions enabled"
#endif

namespace
{

class event_recorder
{
  public:
    void operator()(const voris::mem::budget_event &event) noexcept
    {
        std::lock_guard guard{mutex_};
        events_.push_back(event);
    }

    [[nodiscard]] std::size_t count(voris::mem::budget_event_kind kind) const noexcept
    {
        std::lock_guard guard{mutex_};
        return static_cast<std::size_t>(
            std::count_if(events_.begin(), events_.end(), [kind](const auto &event) { return event.kind == kind; }));
    }

    [[nodiscard]] std::vector<voris::mem::budget_event> events() const
    {
        std::lock_guard guard{mutex_};
        return events_;
    }

  private:
    mutable std::mutex mutex_{};
    std::vector<voris::mem::budget_event> events_{};
};

class reentrant_event_recorder
{
  public:
    void set_budget(voris::mem::budget_node &budget) noexcept
    {
        budget_ = &budget;
    }

    void operator()(const voris::mem::budget_event &) noexcept
    {
        assert(budget_ != nullptr);
        ++calls_;
        auto snapshots = budget_->snapshots();
        observed_snapshot_count_ = snapshots.size();
        auto token = budget_->reserve(1U, voris::mem::memory_tag{"m4.reentrant.inner"});
        assert(token);
        assert(token->rollback());
    }

    [[nodiscard]] std::size_t calls() const noexcept
    {
        return calls_;
    }

    [[nodiscard]] std::size_t observed_snapshot_count() const noexcept
    {
        return observed_snapshot_count_;
    }

  private:
    voris::mem::budget_node *budget_{};
    std::size_t calls_{};
    std::size_t observed_snapshot_count_{};
};

template <typename Recorder> [[nodiscard]] voris::mem::budget_event_sink sink_for(Recorder &recorder) noexcept
{
    return voris::mem::budget_event_sink{recorder};
}

[[nodiscard]] voris::mem::budget_options options(std::string_view process, std::string_view shard,
                                                 std::string_view subsystem, voris::mem::budget_limits limits = {},
                                                 voris::mem::budget_event_sink sink = {})
{
    return voris::mem::budget_options{
        .dimensions =
            voris::mem::budget_dimensions{
                .process = std::string{process},
                .shard = std::string{shard},
                .subsystem = std::string{subsystem},
            },
        .limits = limits,
        .event_sink = sink,
    };
}

[[nodiscard]] std::size_t reserved_for(const std::vector<voris::mem::budget_snapshot> &snapshots,
                                       std::string_view tag) noexcept
{
    for (const auto &snapshot : snapshots)
    {
        if (snapshot.dimensions.tag == tag)
        {
            return snapshot.reserved_bytes;
        }
    }
    return 0U;
}

[[nodiscard]] std::size_t active_for(const std::vector<voris::mem::budget_snapshot> &snapshots,
                                     std::string_view tag) noexcept
{
    for (const auto &snapshot : snapshots)
    {
        if (snapshot.dimensions.tag == tag)
        {
            return snapshot.active_bytes;
        }
    }
    return 0U;
}

void test_child_reservation_exhausts_parent_hard_limit()
{
    voris::mem::memory_budget process{options("process-a", "", "", voris::mem::budget_limits{.hard_limit = 100U})};
    voris::mem::budget_node shard{
        process, options("process-a", "shard-0", "cache", voris::mem::budget_limits{.hard_limit = 200U})};

    auto first = shard.reserve(80U, voris::mem::memory_tag{"m4.parent"});
    assert(first);
    auto second = shard.reserve(30U, voris::mem::memory_tag{"m4.parent"});
    assert(!second);
    assert(second.error() == voris::mem::errc::budget_exceeded);

    assert(reserved_for(process.snapshots(), "m4.parent") == 80U);
    assert(reserved_for(shard.snapshots(), "m4.parent") == 80U);
    auto failed_different_tag = shard.reserve(30U, voris::mem::memory_tag{"m4.parent.failed"});
    assert(!failed_different_tag);
    assert(failed_different_tag.error() == voris::mem::errc::budget_exceeded);
    assert(reserved_for(process.snapshots(), "m4.parent.failed") == 0U);
    assert(reserved_for(shard.snapshots(), "m4.parent.failed") == 0U);
    assert(first->rollback());
    assert(process.snapshots().empty());
}

void test_reservation_rejects_hierarchy_deeper_than_fallback_limit()
{
    voris::mem::memory_budget root{
        options("process-a", "root", "depth", voris::mem::budget_limits{.hard_limit = 1024U})};
    std::vector<std::unique_ptr<voris::mem::budget_node>> nodes;
    nodes.reserve(voris::mem::max_budget_hierarchy_depth);
    voris::mem::budget_node *parent = &root;
    for (std::size_t index = 0U; index < voris::mem::max_budget_hierarchy_depth; ++index)
    {
        nodes.push_back(std::make_unique<voris::mem::budget_node>(
            *parent, options("process-a", "deep", "depth", voris::mem::budget_limits{.hard_limit = 1024U})));
        parent = nodes.back().get();
    }

    auto rejected = parent->reserve(1U, voris::mem::memory_tag{"m4.depth"});
    assert(!rejected);
    assert(rejected.error() == voris::mem::errc::size_overflow);
    assert(root.snapshots().empty());
    assert(parent->snapshots().empty());
}

void test_soft_limit_event_does_not_reject_reservation()
{
    event_recorder recorder;
    voris::mem::memory_budget budget{options(
        "process-a", "", "soft", voris::mem::budget_limits{.soft_limit = 64U, .hard_limit = 256U}, sink_for(recorder))};

    auto first = budget.reserve(32U, voris::mem::memory_tag{"m4.soft"});
    assert(first);
    auto second = budget.reserve(40U, voris::mem::memory_tag{"m4.soft"});
    assert(second);
    assert(recorder.count(voris::mem::budget_event_kind::soft_limit_exceeded) == 1U);
    assert(reserved_for(budget.snapshots(), "m4.soft") == 72U);
}

void test_high_and_low_watermark_crossing_events()
{
    event_recorder recorder;
    voris::mem::memory_budget budget{options("process-a", "", "watermarks",
                                             voris::mem::budget_limits{
                                                 .hard_limit = 256U,
                                                 .high_watermark = 100U,
                                                 .low_watermark = 40U,
                                             },
                                             sink_for(recorder))};

    auto first = budget.reserve(80U, voris::mem::memory_tag{"m4.watermark"});
    assert(first);
    auto second = budget.reserve(30U, voris::mem::memory_tag{"m4.watermark"});
    assert(second);
    assert(recorder.count(voris::mem::budget_event_kind::high_watermark_crossed) == 1U);
    assert(second->rollback());
    assert(recorder.count(voris::mem::budget_event_kind::low_watermark_crossed) == 0U);
    assert(first->rollback());
    assert(recorder.count(voris::mem::budget_event_kind::low_watermark_crossed) == 1U);
}

void test_low_watermark_default_is_disabled()
{
    event_recorder recorder;
    voris::mem::memory_budget budget{options("process-a", "", "default-low-watermark",
                                             voris::mem::budget_limits{.hard_limit = 128U}, sink_for(recorder))};

    auto token = budget.reserve(64U, voris::mem::memory_tag{"m4.low.default"});
    assert(token);
    assert(token->rollback());
    assert(recorder.count(voris::mem::budget_event_kind::low_watermark_crossed) == 0U);
}

void test_event_callback_runs_after_budget_lock_is_released()
{
    reentrant_event_recorder recorder;
    voris::mem::memory_budget budget{options("process-a", "", "reentrant",
                                             voris::mem::budget_limits{.hard_limit = 128U, .high_watermark = 10U},
                                             sink_for(recorder))};
    recorder.set_budget(budget);

    auto token = budget.reserve(16U, voris::mem::memory_tag{"m4.reentrant.outer"});
    assert(token);
    assert(recorder.calls() == 1U);
    assert(recorder.observed_snapshot_count() == 1U);
    assert(reserved_for(budget.snapshots(), "m4.reentrant.inner") == 0U);
    assert(token->rollback());
}

void test_token_move_commit_and_rollback_do_not_double_account()
{
    voris::mem::memory_budget budget{options("process-a", "", "moves", voris::mem::budget_limits{.hard_limit = 512U})};

    auto made = budget.reserve(64U, voris::mem::memory_tag{"m4.move"});
    assert(made);
    voris::mem::reservation_token moved{std::move(*made)};
    assert(!made->owns_reservation());
    assert(moved.owns_reservation());
    assert(moved.commit());
    assert(moved.commit());
    assert(moved.rollback());
    assert(active_for(budget.snapshots(), "m4.move") == 64U);
    assert(reserved_for(budget.snapshots(), "m4.move") == 0U);

    auto other = budget.reserve(16U, voris::mem::memory_tag{"m4.move"});
    assert(other);
    other = std::move(moved);
    assert(!moved.owns_committed_accounting());
    assert(other->owns_committed_accounting());
    assert(active_for(budget.snapshots(), "m4.move") == 64U);
    assert(reserved_for(budget.snapshots(), "m4.move") == 0U);
    assert(budget.release(64U, voris::mem::memory_tag{"m4.move"}));
    assert(budget.snapshots().empty());
}

void test_move_assignment_cleans_destination_reservation_before_transfer()
{
    voris::mem::memory_budget budget{
        options("process-a", "", "move-assign", voris::mem::budget_limits{.hard_limit = 512U})};

    auto destination = budget.reserve(24U, voris::mem::memory_tag{"m4.move.destination"});
    auto source = budget.reserve(40U, voris::mem::memory_tag{"m4.move.source"});
    assert(destination);
    assert(source);
    assert(reserved_for(budget.snapshots(), "m4.move.destination") == 24U);
    assert(reserved_for(budget.snapshots(), "m4.move.source") == 40U);

    *destination = std::move(*source);

    assert(!source->owns_reservation());
    assert(destination->owns_reservation());
    assert(reserved_for(budget.snapshots(), "m4.move.destination") == 0U);
    assert(reserved_for(budget.snapshots(), "m4.move.source") == 40U);
    assert(destination->rollback());
    assert(budget.snapshots().empty());
}

void test_committed_release_underflow_returns_error()
{
    voris::mem::memory_budget budget{
        options("process-a", "", "release", voris::mem::budget_limits{.hard_limit = 512U})};
    auto token = budget.reserve(48U, voris::mem::memory_tag{"m4.release"});
    assert(token);
    assert(token->commit());
    assert(!budget.release(64U, voris::mem::memory_tag{"m4.release"}));
    assert(budget.release(64U, voris::mem::memory_tag{"m4.release"}).error() == voris::mem::errc::wrong_owner);
    assert(active_for(budget.snapshots(), "m4.release") == 48U);
    assert(budget.release(48U, voris::mem::memory_tag{"m4.release"}));
}

void test_concurrent_reservations()
{
    voris::mem::memory_budget budget{
        options("process-a", "", "concurrent", voris::mem::budget_limits{.hard_limit = 4096U})};
    constexpr std::size_t thread_count = 4U;
    constexpr std::size_t iterations = 128U;
    std::vector<std::thread> threads;
    threads.reserve(thread_count);

    for (std::size_t thread_index = 0U; thread_index < thread_count; ++thread_index)
    {
        threads.emplace_back([&budget] {
            for (std::size_t index = 0U; index < iterations; ++index)
            {
                auto token = budget.reserve(1U, voris::mem::memory_tag{"m4.concurrent"});
                assert(token);
                if ((index % 2U) == 0U)
                {
                    assert(token->commit());
                }
                else
                {
                    assert(token->rollback());
                }
            }
        });
    }
    for (auto &thread : threads)
    {
        thread.join();
    }

    assert(active_for(budget.snapshots(), "m4.concurrent") == (thread_count * iterations) / 2U);
    assert(reserved_for(budget.snapshots(), "m4.concurrent") == 0U);
    assert(budget.release((thread_count * iterations) / 2U, voris::mem::memory_tag{"m4.concurrent"}));
    assert(budget.snapshots().empty());
}

void test_export_callback_receives_dimensions()
{
    voris::mem::memory_budget budget{
        options("process-a", "shard-2", "subsystem-x", voris::mem::budget_limits{.hard_limit = 128U})};
    auto token = budget.reserve(12U, voris::mem::memory_tag{"m4.export"});
    assert(token);

    std::vector<voris::mem::budget_snapshot> exported;
    budget.export_snapshots([&exported](const voris::mem::budget_snapshot &snapshot) { exported.push_back(snapshot); });

    assert(exported.size() == 1U);
    assert(exported[0].dimensions.process == "process-a");
    assert(exported[0].dimensions.shard == "shard-2");
    assert(exported[0].dimensions.subsystem == "subsystem-x");
    assert(exported[0].dimensions.tag == "m4.export");
    assert(exported[0].reserved_bytes == 12U);
    assert(exported[0].active_bytes == 0U);
}

} // namespace

static_assert(!std::is_copy_constructible_v<voris::mem::reservation_token>);
static_assert(!std::is_copy_assignable_v<voris::mem::reservation_token>);
static_assert(std::is_move_constructible_v<voris::mem::reservation_token>);
static_assert(std::is_move_assignable_v<voris::mem::reservation_token>);

int main()
{
    test_child_reservation_exhausts_parent_hard_limit();
    test_reservation_rejects_hierarchy_deeper_than_fallback_limit();
    test_soft_limit_event_does_not_reject_reservation();
    test_high_and_low_watermark_crossing_events();
    test_low_watermark_default_is_disabled();
    test_event_callback_runs_after_budget_lock_is_released();
    test_token_move_commit_and_rollback_do_not_double_account();
    test_move_assignment_cleans_destination_reservation_before_transfer();
    test_committed_release_underflow_returns_error();
    test_concurrent_reservations();
    test_export_callback_receives_dimensions();
    return 0;
}
