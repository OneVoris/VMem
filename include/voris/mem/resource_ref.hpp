#pragma once

#include <voris/mem/allocation.hpp>
#include <voris/mem/error.hpp>
#include <voris/mem/tag.hpp>
#include <voris/mem/usage.hpp>

#include <concepts>
#include <expected>
#include <memory>
#include <source_location>
#include <type_traits>

namespace voris::mem
{

namespace detail
{

struct resource_vtable
{
    std::expected<allocation, errc> (*allocate)(void *, const allocation_request &) noexcept;
    std::expected<void, errc> (*deallocate)(void *, allocation) noexcept;
    std::expected<void, errc> (*remote_deallocate)(void *, allocation) noexcept;
    resource_traits (*traits)(const void *) noexcept;
    usage_snapshot (*usage)(const void *) noexcept;
};

template <typename Resource>
concept resource_like =
    requires(Resource &resource, const Resource &const_resource, const allocation_request &request, allocation block) {
        { resource.allocate(request) } noexcept -> std::same_as<std::expected<allocation, errc>>;
        { resource.deallocate(block) } noexcept -> std::same_as<std::expected<void, errc>>;
        { const_resource.traits() } noexcept -> std::same_as<resource_traits>;
        { const_resource.usage() } noexcept -> std::same_as<usage_snapshot>;
    };

template <resource_like Resource> struct resource_model
{
    static std::expected<allocation, errc> allocate(void *state, const allocation_request &request) noexcept
    {
        return static_cast<Resource *>(state)->allocate(request);
    }

    static std::expected<void, errc> deallocate(void *state, allocation block) noexcept
    {
        return static_cast<Resource *>(state)->deallocate(block);
    }

    static std::expected<void, errc> remote_deallocate(void *state, allocation block) noexcept
    {
        if constexpr (requires(Resource &resource, allocation release_block) {
                          {
                              resource.remote_deallocate(release_block)
                          } noexcept -> std::same_as<std::expected<void, errc>>;
                      })
        {
            return static_cast<Resource *>(state)->remote_deallocate(block);
        }
        else
        {
            auto *resource = static_cast<Resource *>(state);
            if (resource->traits().thread_safety == resource_thread_safety::thread_safe)
            {
                return resource->deallocate(block);
            }
            return std::unexpected(errc::wrong_owner);
        }
    }

    static resource_traits traits(const void *state) noexcept
    {
        return static_cast<const Resource *>(state)->traits();
    }

    static usage_snapshot usage(const void *state) noexcept
    {
        return static_cast<const Resource *>(state)->usage();
    }
};

template <resource_like Resource>
inline constexpr resource_vtable resource_vtable_for{
    .allocate = &resource_model<Resource>::allocate,
    .deallocate = &resource_model<Resource>::deallocate,
    .remote_deallocate = &resource_model<Resource>::remote_deallocate,
    .traits = &resource_model<Resource>::traits,
    .usage = &resource_model<Resource>::usage,
};

} // namespace detail

class resource_ref
{
  public:
    using allocate_result = std::expected<allocation, errc>;
    using deallocate_result = std::expected<void, errc>;

    constexpr resource_ref() noexcept = default;

    template <detail::resource_like Resource>
        requires(!std::same_as<std::remove_cvref_t<Resource>, resource_ref>)
    explicit resource_ref(Resource &resource) noexcept
        : state_(std::addressof(resource)), vtable_(&detail::resource_vtable_for<std::remove_cvref_t<Resource>>)
    {
    }

    [[nodiscard]] constexpr bool valid() const noexcept
    {
        return vtable_ != nullptr;
    }

    [[nodiscard]] constexpr explicit operator bool() const noexcept
    {
        return valid();
    }

    [[nodiscard]] allocate_result allocate(const allocation_request &request) const noexcept
    {
        if (!valid())
        {
            return std::unexpected(errc::wrong_owner);
        }
        return vtable_->allocate(state_, request);
    }

    [[nodiscard]] allocate_result allocate(
        std::size_t size, std::size_t alignment, memory_tag tag = default_memory_tag,
        std::source_location location = std::source_location::current()) const noexcept
    {
        return allocate(make_allocation_request(size, alignment, tag, location));
    }

    [[nodiscard]] deallocate_result deallocate(allocation block) const noexcept
    {
        if (!valid())
        {
            return std::unexpected(errc::wrong_owner);
        }
        return vtable_->deallocate(state_, block);
    }

    [[nodiscard]] deallocate_result remote_deallocate(allocation block) const noexcept
    {
        if (!valid())
        {
            return std::unexpected(errc::wrong_owner);
        }
        return vtable_->remote_deallocate(state_, block);
    }

    [[nodiscard]] resource_traits traits() const noexcept
    {
        if (!valid())
        {
            return {};
        }
        return vtable_->traits(state_);
    }

    [[nodiscard]] usage_snapshot usage() const noexcept
    {
        if (!valid())
        {
            return {};
        }
        return vtable_->usage(state_);
    }

  private:
    void *state_{};
    const detail::resource_vtable *vtable_{};
};

} // namespace voris::mem
