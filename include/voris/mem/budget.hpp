#pragma once

#include <voris/mem/checked_math.hpp>
#include <voris/mem/error.hpp>
#include <voris/mem/tag.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <expected>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace voris::mem
{

inline constexpr std::size_t max_budget_hierarchy_depth = 64U;

enum class budget_event_kind
{
    soft_limit_exceeded,
    high_watermark_crossed,
    low_watermark_crossed,
};

struct budget_dimensions
{
    std::string process{};
    std::string shard{};
    std::string subsystem{};
    std::string tag{};
};

struct budget_limits
{
    std::size_t soft_limit{std::numeric_limits<std::size_t>::max()};
    std::size_t hard_limit{std::numeric_limits<std::size_t>::max()};
    std::size_t high_watermark{std::numeric_limits<std::size_t>::max()};
    std::size_t low_watermark{std::numeric_limits<std::size_t>::max()};
};

struct budget_event_counters
{
    std::size_t soft_limit_events{};
    std::size_t high_watermark_events{};
    std::size_t low_watermark_events{};
};

struct budget_event
{
    budget_event_kind kind{};
    budget_dimensions dimensions{};
    std::size_t reserved_bytes{};
    std::size_t active_bytes{};
    budget_limits limits{};
    budget_event_counters counters{};
};

class budget_event_sink
{
  public:
    using emit_fn = void (*)(void *, const budget_event &) noexcept;

    constexpr budget_event_sink() noexcept = default;

    template <typename Sink>
        requires(std::is_nothrow_invocable_r_v<void, Sink &, const budget_event &>)
    explicit budget_event_sink(Sink &sink) noexcept
        : state_(std::addressof(sink)),
          emit_([](void *state, const budget_event &event) noexcept { (*static_cast<Sink *>(state))(event); })
    {
    }

    void emit(const budget_event &event) const noexcept
    {
        if (emit_ != nullptr)
        {
            emit_(state_, event);
        }
    }

    [[nodiscard]] explicit constexpr operator bool() const noexcept
    {
        return emit_ != nullptr;
    }

  private:
    void *state_{};
    emit_fn emit_{};
};

struct budget_options
{
    budget_dimensions dimensions{};
    budget_limits limits{};
    budget_event_sink event_sink{};
};

struct budget_snapshot
{
    budget_dimensions dimensions{};
    std::size_t reserved_bytes{};
    std::size_t active_bytes{};
    budget_limits limits{};
    budget_event_counters counters{};
};

class budget_node;

class reservation_token
{
  public:
    constexpr reservation_token() noexcept = default;

    reservation_token(const reservation_token &) = delete;
    reservation_token &operator=(const reservation_token &) = delete;

    reservation_token(reservation_token &&other) noexcept
        : node_(std::exchange(other.node_, nullptr)), bytes_(std::exchange(other.bytes_, 0U)),
          tag_(std::move(other.tag_)), state_(std::exchange(other.state_, state::empty))
    {
    }

    reservation_token &operator=(reservation_token &&other)
    {
        if (this != &other)
        {
            auto released = rollback();
            if (!released)
            {
                return *this;
            }
            node_ = std::exchange(other.node_, nullptr);
            bytes_ = std::exchange(other.bytes_, 0U);
            tag_ = std::move(other.tag_);
            state_ = std::exchange(other.state_, state::empty);
        }
        return *this;
    }

    ~reservation_token() noexcept
    {
        try
        {
            static_cast<void>(rollback());
        }
        catch (...)
        {
        }
    }

    [[nodiscard]] bool owns_reservation() const noexcept
    {
        return state_ == state::reserved && node_ != nullptr;
    }

    [[nodiscard]] bool owns_committed_accounting() const noexcept
    {
        return state_ == state::committed && node_ != nullptr;
    }

    [[nodiscard]] std::size_t bytes() const noexcept
    {
        return bytes_;
    }

    [[nodiscard]] std::string_view tag() const noexcept
    {
        return tag_;
    }

    [[nodiscard]] std::expected<void, errc> commit();
    [[nodiscard]] std::expected<void, errc> rollback();

  private:
    friend class budget_node;

    enum class state
    {
        empty,
        reserved,
        committed,
    };

    reservation_token(budget_node &node, std::size_t bytes, std::string tag) noexcept
        : node_(std::addressof(node)), bytes_(bytes), tag_(std::move(tag)), state_(state::reserved)
    {
    }

    budget_node *node_{};
    std::size_t bytes_{};
    std::string tag_{};
    state state_{state::empty};
};

// budget_node is caller-owned and thread-safe. A parent node must outlive every child node
// and every live reservation token created from that child.
class budget_node
{
  public:
    explicit budget_node(budget_options options = {}) noexcept : options_(std::move(options))
    {
    }

    budget_node(budget_node &parent, budget_options options = {}) noexcept
        : parent_(std::addressof(parent)), options_(std::move(options))
    {
    }

    budget_node(const budget_node &) = delete;
    budget_node &operator=(const budget_node &) = delete;
    budget_node(budget_node &&) = delete;
    budget_node &operator=(budget_node &&) = delete;

    [[nodiscard]] std::expected<reservation_token, errc> reserve(std::size_t bytes, memory_tag tag = default_memory_tag)
    {
        if (hierarchy_depth_exceeds_limit())
        {
            return std::unexpected(errc::size_overflow);
        }
        try
        {
            const std::string tag_name{tag.name};
            if (bytes == 0U)
            {
                return reservation_token{*this, bytes, tag_name};
            }

            std::vector<pending_event> events;
            auto locked = lock_hierarchy();
            for (auto *node : locked.nodes)
            {
                auto checked = node->can_reserve_locked(bytes, tag_name);
                if (!checked)
                {
                    return std::unexpected(checked.error());
                }
            }
            auto prepared = prepare_reserve_locked(locked.nodes, bytes, tag_name, events);
            if (!prepared)
            {
                return std::unexpected(prepared.error());
            }
            reservation_token token{*this, bytes, std::move(tag_name)};
            for (auto *node : locked.nodes)
            {
                node->reserve_locked(bytes, token.tag_);
            }
            locked.locks.clear();
            emit_events(events);
            return token;
        }
        catch (const std::bad_alloc &)
        {
            return std::unexpected(errc::out_of_memory);
        }
    }

    [[nodiscard]] std::expected<void, errc> release(std::size_t bytes, memory_tag tag = default_memory_tag)
    {
        try
        {
            const std::string tag_name{tag.name};
            return release(tag_name, bytes);
        }
        catch (const std::bad_alloc &)
        {
            return std::unexpected(errc::out_of_memory);
        }
    }

    [[nodiscard]] std::vector<budget_snapshot> snapshots() const
    {
        std::vector<budget_snapshot> result;
        std::lock_guard guard{mutex_};
        result.reserve(tag_accounts_.size());
        for (const auto &[tag, account] : tag_accounts_)
        {
            if (account.reserved_bytes == 0U && account.active_bytes == 0U)
            {
                continue;
            }
            result.push_back(make_snapshot_locked(tag, account));
        }
        return result;
    }

    template <typename Sink> void export_snapshots(Sink &&sink) const
    {
        auto exported = snapshots();
        for (const auto &snapshot : exported)
        {
            sink(snapshot);
        }
    }

    [[nodiscard]] budget_limits limits() const
    {
        std::lock_guard guard{mutex_};
        return options_.limits;
    }

    [[nodiscard]] budget_dimensions dimensions() const
    {
        std::lock_guard guard{mutex_};
        return options_.dimensions;
    }

  private:
    friend class reservation_token;

    struct tag_accounting
    {
        std::size_t reserved_bytes{};
        std::size_t active_bytes{};
    };

    struct pending_event
    {
        budget_event_sink sink{};
        budget_event event{};
    };

    struct locked_hierarchy
    {
        std::vector<budget_node *> nodes{};
        std::vector<std::unique_lock<std::mutex>> locks{};
    };

    struct prepared_tag_entries
    {
        std::vector<budget_node *> inserted_nodes{};

        void rollback(const std::string &tag_name) noexcept
        {
            for (auto *node : inserted_nodes)
            {
                node->tag_accounts_.erase(tag_name);
            }
            inserted_nodes.clear();
        }

        void commit() noexcept
        {
            inserted_nodes.clear();
        }
    };

    [[nodiscard]] locked_hierarchy lock_hierarchy()
    {
        locked_hierarchy locked;
        for (auto *node = this; node != nullptr; node = node->parent_)
        {
            locked.nodes.push_back(node);
        }
        std::reverse(locked.nodes.begin(), locked.nodes.end());
        locked.locks.reserve(locked.nodes.size());
        for (auto *node : locked.nodes)
        {
            locked.locks.emplace_back(node->mutex_);
        }
        return locked;
    }

    [[nodiscard]] bool hierarchy_depth_exceeds_limit() const noexcept
    {
        std::size_t depth{};
        for (auto *node = this; node != nullptr; node = node->parent_)
        {
            ++depth;
            if (depth > max_budget_hierarchy_depth)
            {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] std::expected<void, errc> can_reserve_locked(std::size_t bytes,
                                                               const std::string &tag_name) const noexcept
    {
        auto total = checked_add(reserved_bytes_, active_bytes_);
        if (!total)
        {
            return std::unexpected(total.error());
        }
        auto next_total = checked_add(*total, bytes);
        if (!next_total)
        {
            return std::unexpected(next_total.error());
        }
        if (options_.limits.hard_limit != std::numeric_limits<std::size_t>::max() &&
            *next_total > options_.limits.hard_limit)
        {
            return std::unexpected(errc::budget_exceeded);
        }
        auto next_reserved = checked_add(reserved_bytes_, bytes);
        if (!next_reserved)
        {
            return std::unexpected(next_reserved.error());
        }
        if (const auto found = tag_accounts_.find(tag_name); found != tag_accounts_.end())
        {
            auto tag_reserved = checked_add(found->second.reserved_bytes, bytes);
            if (!tag_reserved)
            {
                return std::unexpected(tag_reserved.error());
            }
        }
        return {};
    }

    [[nodiscard]] static std::expected<void, errc> prepare_reserve_locked(const std::vector<budget_node *> &nodes,
                                                                          std::size_t bytes,
                                                                          const std::string &tag_name,
                                                                          std::vector<pending_event> &events) noexcept
    {
        prepared_tag_entries prepared_entries;
        try
        {
            prepared_entries.inserted_nodes.reserve(nodes.size());
            for (auto *node : nodes)
            {
                const auto [unused, inserted] = node->tag_accounts_.try_emplace(tag_name);
                (void)unused;
                if (inserted)
                {
                    prepared_entries.inserted_nodes.push_back(node);
                }
            }
            events.reserve(nodes.size() * 2U);
            for (auto *node : nodes)
            {
                node->prepare_upward_events_locked(tag_name, bytes, events);
            }
            prepared_entries.commit();
            return {};
        }
        catch (const std::bad_alloc &)
        {
            prepared_entries.rollback(tag_name);
            events.clear();
            return std::unexpected(errc::out_of_memory);
        }
    }

    void reserve_locked(std::size_t bytes, const std::string &tag_name) noexcept
    {
        const auto before_total = checked_add(reserved_bytes_, active_bytes_).value();
        reserved_bytes_ = checked_add(reserved_bytes_, bytes).value();
        auto &tag_account = tag_accounts_.find(tag_name)->second;
        tag_account.reserved_bytes = checked_add(tag_account.reserved_bytes, bytes).value();
        const auto after_total = checked_add(reserved_bytes_, active_bytes_).value();
        apply_upward_counters_locked(before_total, after_total);
    }

    [[nodiscard]] std::expected<void, errc> commit(const std::string &tag_name, std::size_t bytes)
    {
        if (bytes == 0U)
        {
            return {};
        }

        {
            auto locked = lock_hierarchy();
            for (auto *node : locked.nodes)
            {
                auto checked = node->can_commit_locked(tag_name, bytes);
                if (!checked)
                {
                    return std::unexpected(checked.error());
                }
            }
            for (auto *node : locked.nodes)
            {
                node->commit_locked(tag_name, bytes);
            }
        }
        return {};
    }

    [[nodiscard]] std::expected<void, errc> rollback(const std::string &tag_name, std::size_t bytes)
    {
        if (bytes == 0U)
        {
            return {};
        }
        return release_reserved(tag_name, bytes);
    }

    [[nodiscard]] std::expected<void, errc> release(const std::string &tag_name, std::size_t bytes)
    {
        if (bytes == 0U)
        {
            return {};
        }

        std::vector<pending_event> events;
        {
            auto locked = lock_hierarchy();
            for (auto *node : locked.nodes)
            {
                auto checked = node->can_release_active_locked(tag_name, bytes);
                if (!checked)
                {
                    return std::unexpected(checked.error());
                }
            }
            prepare_release_active_events_locked(locked.nodes, tag_name, bytes, events);
            for (auto *node : locked.nodes)
            {
                node->release_active_locked(tag_name, bytes);
            }
            locked.locks.clear();
        }
        emit_events(events);
        return {};
    }

    [[nodiscard]] std::expected<void, errc> release_reserved(const std::string &tag_name, std::size_t bytes)
    {
        try
        {
            std::vector<pending_event> events;
            {
                auto locked = lock_hierarchy();
                for (auto *node : locked.nodes)
                {
                    auto checked = node->can_release_reserved_locked(tag_name, bytes);
                    if (!checked)
                    {
                        return std::unexpected(checked.error());
                    }
                }
                prepare_release_reserved_events_locked(locked.nodes, tag_name, bytes, events);
                for (auto *node : locked.nodes)
                {
                    node->release_reserved_locked(tag_name, bytes);
                }
                locked.locks.clear();
            }
            emit_events(events);
            return {};
        }
        catch (const std::bad_alloc &)
        {
            return release_reserved_without_events(tag_name, bytes);
        }
    }

    [[nodiscard]] std::expected<void, errc> can_commit_locked(const std::string &tag_name,
                                                              std::size_t bytes) const noexcept
    {
        if (reserved_bytes_ < bytes)
        {
            return std::unexpected(errc::wrong_owner);
        }
        const auto found = tag_accounts_.find(tag_name);
        if (found == tag_accounts_.end() || found->second.reserved_bytes < bytes)
        {
            return std::unexpected(errc::wrong_owner);
        }
        auto active = checked_add(active_bytes_, bytes);
        if (!active)
        {
            return std::unexpected(active.error());
        }
        auto tag_active = checked_add(found->second.active_bytes, bytes);
        if (!tag_active)
        {
            return std::unexpected(tag_active.error());
        }
        return {};
    }

    void commit_locked(const std::string &tag_name, std::size_t bytes)
    {
        reserved_bytes_ = checked_sub(reserved_bytes_, bytes).value();
        active_bytes_ = checked_add(active_bytes_, bytes).value();
        auto &tag_account = tag_accounts_.find(tag_name)->second;
        tag_account.reserved_bytes = checked_sub(tag_account.reserved_bytes, bytes).value();
        tag_account.active_bytes = checked_add(tag_account.active_bytes, bytes).value();
        erase_empty_tag_locked(tag_name);
    }

    [[nodiscard]] std::expected<void, errc> can_release_reserved_locked(const std::string &tag_name,
                                                                        std::size_t bytes) const noexcept
    {
        if (reserved_bytes_ < bytes)
        {
            return std::unexpected(errc::wrong_owner);
        }
        const auto found = tag_accounts_.find(tag_name);
        if (found == tag_accounts_.end() || found->second.reserved_bytes < bytes)
        {
            return std::unexpected(errc::wrong_owner);
        }
        return {};
    }

    [[nodiscard]] std::expected<void, errc> can_release_active_locked(const std::string &tag_name,
                                                                      std::size_t bytes) const noexcept
    {
        if (active_bytes_ < bytes)
        {
            return std::unexpected(errc::wrong_owner);
        }
        const auto found = tag_accounts_.find(tag_name);
        if (found == tag_accounts_.end() || found->second.active_bytes < bytes)
        {
            return std::unexpected(errc::wrong_owner);
        }
        return {};
    }

    void release_reserved_locked(const std::string &tag_name, std::size_t bytes) noexcept
    {
        const auto before_total = checked_add(reserved_bytes_, active_bytes_).value();
        reserved_bytes_ = checked_sub(reserved_bytes_, bytes).value();
        auto &tag_account = tag_accounts_.find(tag_name)->second;
        tag_account.reserved_bytes = checked_sub(tag_account.reserved_bytes, bytes).value();
        const auto after_total = checked_add(reserved_bytes_, active_bytes_).value();
        apply_downward_counters_locked(before_total, after_total);
        erase_empty_tag_locked(tag_name);
    }

    void release_active_locked(const std::string &tag_name, std::size_t bytes) noexcept
    {
        const auto before_total = checked_add(reserved_bytes_, active_bytes_).value();
        active_bytes_ = checked_sub(active_bytes_, bytes).value();
        auto &tag_account = tag_accounts_.find(tag_name)->second;
        tag_account.active_bytes = checked_sub(tag_account.active_bytes, bytes).value();
        const auto after_total = checked_add(reserved_bytes_, active_bytes_).value();
        apply_downward_counters_locked(before_total, after_total);
        erase_empty_tag_locked(tag_name);
    }

    [[nodiscard]] std::expected<void, errc> release_reserved_without_events(const std::string &tag_name,
                                                                            std::size_t bytes)
    {
        std::array<budget_node *, max_budget_hierarchy_depth> nodes{};
        std::size_t count{};
        for (auto *node = this; node != nullptr; node = node->parent_)
        {
            if (count == nodes.size())
            {
                return std::unexpected(errc::size_overflow);
            }
            nodes[count] = node;
            ++count;
        }

        std::array<std::unique_lock<std::mutex>, max_budget_hierarchy_depth> locks{};
        for (std::size_t index = 0U; index < count; ++index)
        {
            locks[index] = std::unique_lock<std::mutex>{nodes[count - index - 1U]->mutex_};
        }

        for (std::size_t index = 0U; index < count; ++index)
        {
            auto *node = nodes[count - index - 1U];
            auto checked = node->can_release_reserved_locked(tag_name, bytes);
            if (!checked)
            {
                return std::unexpected(checked.error());
            }
        }

        for (std::size_t index = 0U; index < count; ++index)
        {
            nodes[count - index - 1U]->release_reserved_locked(tag_name, bytes);
        }
        return {};
    }

    static void prepare_release_active_events_locked(const std::vector<budget_node *> &nodes,
                                                     const std::string &tag_name, std::size_t bytes,
                                                     std::vector<pending_event> &events)
    {
        events.reserve(nodes.size());
        for (auto *node : nodes)
        {
            node->prepare_downward_events_locked(tag_name, bytes, false, events);
        }
    }

    static void prepare_release_reserved_events_locked(const std::vector<budget_node *> &nodes,
                                                       const std::string &tag_name, std::size_t bytes,
                                                       std::vector<pending_event> &events)
    {
        events.reserve(nodes.size());
        for (auto *node : nodes)
        {
            node->prepare_downward_events_locked(tag_name, bytes, true, events);
        }
    }

    void prepare_upward_events_locked(std::string_view tag_name, std::size_t bytes,
                                      std::vector<pending_event> &events) const
    {
        const auto before_total = checked_add(reserved_bytes_, active_bytes_).value();
        const auto after_reserved = checked_add(reserved_bytes_, bytes).value();
        const auto after_total = checked_add(after_reserved, active_bytes_).value();
        budget_event_counters event_counters = counters_;
        if (crossed_above(before_total, after_total, options_.limits.soft_limit))
        {
            ++event_counters.soft_limit_events;
            append_prepared_event_locked(budget_event_kind::soft_limit_exceeded, tag_name, after_reserved,
                                         active_bytes_, event_counters, events);
        }
        if (crossed_above(before_total, after_total, options_.limits.high_watermark))
        {
            ++event_counters.high_watermark_events;
            append_prepared_event_locked(budget_event_kind::high_watermark_crossed, tag_name, after_reserved,
                                         active_bytes_, event_counters, events);
        }
    }

    void prepare_downward_events_locked(std::string_view tag_name, std::size_t bytes, bool reserved_release,
                                        std::vector<pending_event> &events) const
    {
        const auto before_total = checked_add(reserved_bytes_, active_bytes_).value();
        const auto after_reserved = reserved_release ? checked_sub(reserved_bytes_, bytes).value() : reserved_bytes_;
        const auto after_active = reserved_release ? active_bytes_ : checked_sub(active_bytes_, bytes).value();
        const auto after_total = checked_add(after_reserved, after_active).value();
        if (options_.limits.low_watermark != std::numeric_limits<std::size_t>::max() &&
            before_total > options_.limits.low_watermark && after_total <= options_.limits.low_watermark)
        {
            budget_event_counters event_counters = counters_;
            ++event_counters.low_watermark_events;
            append_prepared_event_locked(budget_event_kind::low_watermark_crossed, tag_name, after_reserved,
                                         after_active, event_counters, events);
        }
    }

    void append_prepared_event_locked(budget_event_kind kind, std::string_view tag_name, std::size_t reserved_bytes,
                                      std::size_t active_bytes, budget_event_counters event_counters,
                                      std::vector<pending_event> &events) const
    {
        if (!options_.event_sink)
        {
            return;
        }
        budget_dimensions event_dimensions = options_.dimensions;
        event_dimensions.tag = std::string{tag_name};
        events.push_back(pending_event{
            .sink = options_.event_sink,
            .event =
                budget_event{
                    .kind = kind,
                    .dimensions = std::move(event_dimensions),
                    .reserved_bytes = reserved_bytes,
                    .active_bytes = active_bytes,
                    .limits = options_.limits,
                    .counters = event_counters,
                },
        });
    }

    void apply_upward_counters_locked(std::size_t before_total, std::size_t after_total) noexcept
    {
        if (crossed_above(before_total, after_total, options_.limits.soft_limit))
        {
            ++counters_.soft_limit_events;
        }
        if (crossed_above(before_total, after_total, options_.limits.high_watermark))
        {
            ++counters_.high_watermark_events;
        }
    }

    void apply_downward_counters_locked(std::size_t before_total, std::size_t after_total) noexcept
    {
        if (options_.limits.low_watermark != std::numeric_limits<std::size_t>::max() &&
            before_total > options_.limits.low_watermark && after_total <= options_.limits.low_watermark)
        {
            ++counters_.low_watermark_events;
        }
    }

    [[nodiscard]] static bool crossed_above(std::size_t before, std::size_t after, std::size_t limit) noexcept
    {
        return limit != std::numeric_limits<std::size_t>::max() && before <= limit && after > limit;
    }

    void erase_empty_tag_locked(const std::string &tag_name)
    {
        const auto found = tag_accounts_.find(tag_name);
        if (found != tag_accounts_.end() && found->second.reserved_bytes == 0U && found->second.active_bytes == 0U)
        {
            tag_accounts_.erase(found);
        }
    }

    [[nodiscard]] budget_snapshot make_snapshot_locked(const std::string &tag_name, const tag_accounting &account) const
    {
        budget_dimensions snapshot_dimensions = options_.dimensions;
        snapshot_dimensions.tag = tag_name;
        return budget_snapshot{
            .dimensions = std::move(snapshot_dimensions),
            .reserved_bytes = account.reserved_bytes,
            .active_bytes = account.active_bytes,
            .limits = options_.limits,
            .counters = counters_,
        };
    }

    static void emit_events(const std::vector<pending_event> &events) noexcept
    {
        for (const auto &event : events)
        {
            event.sink.emit(event.event);
        }
    }

    budget_node *parent_{};
    mutable std::mutex mutex_{};
    budget_options options_{};
    std::size_t reserved_bytes_{};
    std::size_t active_bytes_{};
    budget_event_counters counters_{};
    std::map<std::string, tag_accounting> tag_accounts_{};
};

using memory_budget = budget_node;

inline std::expected<void, errc> reservation_token::commit()
{
    if (state_ != state::reserved || node_ == nullptr)
    {
        return {};
    }
    try
    {
        auto committed = node_->commit(tag_, bytes_);
        if (!committed)
        {
            return std::unexpected(committed.error());
        }
    }
    catch (const std::bad_alloc &)
    {
        return std::unexpected(errc::out_of_memory);
    }
    state_ = state::committed;
    return {};
}

inline std::expected<void, errc> reservation_token::rollback()
{
    if (state_ != state::reserved || node_ == nullptr)
    {
        return {};
    }
    auto released = node_->rollback(tag_, bytes_);
    if (!released)
    {
        return std::unexpected(released.error());
    }
    node_ = nullptr;
    bytes_ = 0U;
    tag_.clear();
    state_ = state::empty;
    return {};
}

} // namespace voris::mem
