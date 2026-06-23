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

## Ownership

- `*_view`, `span`, and `string_view` are non-owning.
- `unique_*` is move-only ownership.
- `shared_*` explicitly extends lifetime; hot paths may use intrusive reference counting.
- `handle` is move-only and closes on destruction.
- Every view documents invalidation conditions.

## Asynchronous Behavior

Core APIs remain synchronous unless an optional VIO component is enabled.

- Completion never changes thread or shard merely because an operation completed synchronously.
- Cancellation is a request with defined completion semantics, not permission to destroy live operation state.
- Deadlines use a monotonic clock.
- A public task cannot silently outlive its owning scope.

## Buffers

Borrowed byte ranges use VMem-compatible views. APIs document whether input is borrowed, consumed, retained, or copied. Scatter/gather operating-system types stay private.

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
