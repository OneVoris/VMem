#pragma once

#include <cstddef>
#include <string_view>

namespace voris::mem
{

struct allocation
{
    void *data{};
    std::size_t size{};
    std::size_t alignment{};
};

enum class resource_ownership
{
    caller_owned,
    process_lifetime,
    shared_backend,
};

enum class resource_thread_safety
{
    thread_safe,
    shard_confined,
    externally_synchronized,
};

struct resource_traits
{
    std::string_view name{};
    resource_ownership ownership{resource_ownership::caller_owned};
    resource_thread_safety thread_safety{resource_thread_safety::externally_synchronized};
    bool supports_remote_deallocate{};
};

} // namespace voris::mem
