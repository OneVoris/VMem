#pragma once

#include <voris/mem/allocation.hpp>
#include <voris/mem/error.hpp>
#include <voris/mem/resource_ref.hpp>
#include <voris/mem/tag.hpp>
#include <voris/mem/usage.hpp>

#include <expected>
#include <mutex>

namespace voris::mem {

class synchronized_resource {
public:
    explicit synchronized_resource(resource_ref upstream) noexcept : upstream_(upstream) {}

    [[nodiscard]] std::expected<allocation, errc>
    allocate(const allocation_request& request) noexcept {
        std::lock_guard lock{mutex_};
        return upstream_.allocate(request);
    }

    [[nodiscard]] std::expected<void, errc> deallocate(allocation block) noexcept {
        std::lock_guard lock{mutex_};
        return upstream_.deallocate(block);
    }

    [[nodiscard]] resource_traits traits() const noexcept {
        return resource_traits{
            .name = "synchronized_resource",
            .ownership = resource_ownership::caller_owned,
            .thread_safety = resource_thread_safety::thread_safe,
            .supports_remote_deallocate = upstream_.traits().supports_remote_deallocate,
        };
    }

    [[nodiscard]] usage_snapshot usage() const noexcept {
        std::lock_guard lock{mutex_};
        return upstream_.usage();
    }

private:
    resource_ref upstream_;
    mutable std::mutex mutex_{};
};

} // namespace voris::mem
