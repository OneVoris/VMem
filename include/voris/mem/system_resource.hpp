#pragma once

#include <voris/mem/allocation.hpp>
#include <voris/mem/error.hpp>
#include <voris/mem/export.hpp>
#include <voris/mem/tag.hpp>
#include <voris/mem/usage.hpp>

#include <atomic>
#include <expected>
#include <mutex>
#include <unordered_map>

namespace voris::mem {

class VORIS_VMEM_API system_resource {
public:
    [[nodiscard]] std::expected<allocation, errc>
    allocate(const allocation_request& request) noexcept;

    [[nodiscard]] std::expected<void, errc> deallocate(allocation block) noexcept;

    [[nodiscard]] resource_traits traits() const noexcept;
    [[nodiscard]] usage_snapshot usage() const noexcept;

private:
    std::atomic<std::size_t> active_bytes_{};
    std::atomic<std::size_t> active_allocation_count_{};
    std::atomic<std::size_t> total_allocations_{};
    std::atomic<std::size_t> total_deallocations_{};
    std::atomic<std::size_t> failed_allocations_{};
    std::mutex active_allocations_mutex_{};
    std::unordered_map<void*, allocation> active_allocations_{};
};

} // namespace voris::mem
