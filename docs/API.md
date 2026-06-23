# Public API Conventions

## Namespace and Include Layout

- Namespace: `voris::mem`
- Include prefix: `#include <voris/mem/...>`
- Primary target: `voris_vmem`
- VXrepo package: `voris-vmem`

## Language Baseline

The source baseline is C++23. Public headers may use concepts, `std::expected`, `std::span`, `std::string_view`, `std::chrono`, `std::source_location`, and move-only callables when they clarify ownership or constraints.

## Error Model

- Normal failures use explicit result values.
- Stable error enums are never reordered or reused after release.
- Platform/provider error codes may be attached for diagnostics but are not the only public classification.
- Invariant violations and invalid external input are distinct categories.
- M0 allocation APIs return `std::expected<allocation, errc>` for allocation and `std::expected<void, errc>` for release.
- `deallocate` operations are `noexcept`; ownership failures are reported as `errc::wrong_owner`.

The initial stable error identifiers are:

| Error | Meaning |
|---|---|
| `out_of_memory` | The provider could not satisfy the request without violating its limits. |
| `invalid_alignment` | The requested alignment is zero or not a power of two. |
| `size_overflow` | Size, alignment, header, or capacity arithmetic overflowed. |
| `budget_exceeded` | A configured hard budget would be exceeded. |
| `wrong_owner` | A block was released through the wrong resource, shard, or owner generation. |

## Ownership

- `*_view`, `span`, and `string_view` are non-owning.
- `unique_*` is move-only ownership.
- `shared_*` explicitly extends lifetime; hot paths may use intrusive reference counting.
- `handle` is move-only and closes on destruction.
- Every view documents invalidation conditions.
- `resource_ref` is a copyable, allocation-free, non-owning reference to a resource object. The referenced resource must outlive every `resource_ref` and every live block allocated through it.
- Every resource exposes `resource_traits` with `resource_ownership`, `resource_thread_safety`, and whether remote deallocation is supported.

## Asynchronous Behavior

Core APIs remain synchronous unless an optional VIO component is enabled.

- Completion never changes thread or shard merely because an operation completed synchronously.
- Cancellation is a request with defined completion semantics, not permission to destroy live operation state.
- Deadlines use a monotonic clock.
- A public task cannot silently outlive its owning scope.

## Buffers

Borrowed byte ranges use VMem-compatible views. APIs document whether input is borrowed, consumed, retained, or copied. Scatter/gather operating-system types stay private.

## M0 Resource Contracts

The M0 public surface is limited to contracts and value types:

```cpp
#include <voris/mem/allocation.hpp>
#include <voris/mem/checked_math.hpp>
#include <voris/mem/error.hpp>
#include <voris/mem/export.hpp>
#include <voris/mem/resource_ref.hpp>
#include <voris/mem/tag.hpp>
#include <voris/mem/usage.hpp>
```

Zero-sized allocation requests are valid when the alignment is valid. Resource implementations return an empty `allocation` with `data == nullptr`, `size == 0`, and the requested alignment. Releasing such a block is a no-op success.

Out-of-memory and budget failures do not throw exceptions. Implementations return `errc::out_of_memory` or `errc::budget_exceeded` and leave externally visible accounting unchanged.

Shard-confined resources reject releases from the wrong shard with `errc::wrong_owner` unless the resource explicitly documents a bounded remote-release path. A resource that supports remote release must bound queued work and must not invoke user code while holding private locks.

Destroying a resource while blocks allocated from it remain live violates the ownership contract. Diagnostic resources may trap, report, or retain leak snapshots, but callers must not use `resource_ref` after the referenced resource has been destroyed.

All size, alignment, header, and capacity arithmetic in public helpers uses checked operations:

```cpp
std::expected<std::size_t, errc> checked_add(std::size_t, std::size_t) noexcept;
std::expected<std::size_t, errc> checked_mul(std::size_t, std::size_t) noexcept;
std::expected<std::size_t, errc> align_up(std::size_t value, std::size_t alignment) noexcept;
```

`memory_tag` and `source_location` are copied into `allocation_request` so accounting and fault-injection resources can classify allocations without taking ownership of logging or telemetry infrastructure.

## Configuration

Configuration is represented by validated value objects with safe hard limits. Avoid positional Boolean parameters; use named options and enums.

## ABI

The `0.x` series does not promise binary compatibility. Shared-library builds hide non-public symbols. Stable provider/plugin boundaries use PImpl or a versioned C ABI rather than exposing STL, exceptions, RTTI, or compiler-specific coroutine objects.

## API Sketch

```cpp
namespace voris::mem {

enum class errc { out_of_memory, invalid_alignment, size_overflow,
                  budget_exceeded, wrong_owner };

struct allocation {
    void* data{};
    std::size_t size{};
    std::size_t alignment{};
};

class resource_ref;
class unique_buffer;
class shared_buffer;
class buffer_chain;
struct const_buffer;
struct mutable_buffer;

} // namespace voris::mem
```
