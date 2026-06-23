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
| `unsupported_platform` | The operation is intentionally unavailable for the current platform implementation. |

## Ownership

- `*_view`, `span`, and `string_view` are non-owning.
- `unique_*` is move-only ownership.
- `shared_*` explicitly extends lifetime; hot paths may use intrusive reference counting.
- `handle` is move-only and closes on destruction.
- Every view documents invalidation conditions.
- `resource_ref` is a copyable, allocation-free, non-owning reference to a resource object. The referenced resource must outlive every `resource_ref` and every live block allocated through it.
- A default-constructed `resource_ref` is invalid but safe to query. Allocation, deallocation, and remote deallocation return `errc::wrong_owner`; traits and usage return empty value objects.
- Every resource exposes `resource_traits` with `resource_ownership`, `resource_thread_safety`, and whether remote deallocation is supported.

## Asynchronous Behavior

Core APIs remain synchronous unless an optional VIO component is enabled.

- Completion never changes thread or shard merely because an operation completed synchronously.
- Cancellation is a request with defined completion semantics, not permission to destroy live operation state.
- Deadlines use a monotonic clock.
- A public task cannot silently outlive its owning scope.

## Buffers

Borrowed byte ranges use VMem-compatible views. APIs document whether input is borrowed, consumed, retained, or copied. Scatter/gather operating-system types stay private.

## M3 Buffer Contracts

M3 adds the following public headers:

```cpp
#include <voris/mem/buffer.hpp>
#include <voris/mem/unique_buffer.hpp>
#include <voris/mem/shared_buffer.hpp>
#include <voris/mem/buffer_chain.hpp>
#include <voris/mem/buffer_parser.hpp>
```

`const_buffer` and `mutable_buffer` are non-owning byte views. A null pointer is valid only when the size is zero. Their `slice(offset, count)` helpers return `errc::wrong_owner` for invalid non-empty null views, validate checked offset/count arithmetic, and return `errc::size_overflow` for out-of-bounds ranges.

`unique_buffer` is move-only ownership over an allocation from `resource_ref`. It tracks logical size separately from capacity, resizes only within capacity, and releases through the originating resource in `reset()` or destruction. Release is `noexcept` and reports provider ownership failures through `reset()`. Move assignment first releases the current owner; if release fails, both source and destination remain unchanged so callers can retry cleanup.

`shared_buffer` is an intrusive reference-counted buffer with explicit `clone()` so reference-count overflow is reportable as `errc::size_overflow`. Final release happens on the thread that drops the last reference. Cross-thread final release is accepted only when the backing resource is thread-safe or advertises remote deallocation; remote-capable non-thread-safe resources are released through `resource_ref::remote_deallocate`. Otherwise creation returns `errc::wrong_owner`. The control block records owner generation and validates it before cloning or final release. Final-release failure leaves the handle attached with use count one so release can be retried. Move assignment leaves both buffers unchanged if releasing the current destination owner fails.

`buffer_chain` stores up to four segments inline before spilling to dynamic segment storage. Segments may borrow a `const_buffer` or own a `unique_buffer`/`shared_buffer`. Append and prepend reject invalid non-empty null views with `errc::wrong_owner`. Append, prepend, consume, trim, and slice operations preserve segment order and validate checked size arithmetic. Moving a chain transfers its segments and leaves the source empty. `coalesce(resource, max_size, alignment, tag)` copies into a `unique_buffer` only when the chain size is at or below the explicit limit; exceeding the limit returns `errc::budget_exceeded`.

Parser helpers read across segment boundaries: `peek_uint_be`, `peek_uint_le`, `find_delimiter`, and `copy_prefix`. Prefix copy rejects an invalid non-empty null destination with `errc::wrong_owner`. Public headers do not expose POSIX `iovec`, Windows `WSABUF`, or platform networking/page headers; scatter/gather adapters remain private implementation/test support.

## M4 Budget Contracts

M4 adds the following public header:

```cpp
#include <voris/mem/budget.hpp>
```

`budget_node` and its alias `memory_budget` provide a standalone, caller-owned budget layer. A node is thread-safe; parent nodes must outlive child nodes and every live `reservation_token` created through a child. Reservation creation supports up to `max_budget_hierarchy_depth` nodes from root to leaf; deeper hierarchies return `errc::size_overflow` before creating a token or changing accounting. Current allocators are not forced to consume the budget layer yet.

Reservations are hierarchical. Reserving bytes on a child reserves the same bytes on every ancestor. A move-only `reservation_token` owns those reserved bytes until `commit()`, `rollback()`, move assignment cleanup, or destruction. `commit()` converts reserved bytes to active bytes exactly once and returns `errc::out_of_memory` if traversal state allocation fails before mutation. Repeated `commit()` or `rollback()` calls are no-op successes after the token has left the reserved state. Destruction rolls back only uncommitted reservations. Move construction transfers ownership. Move assignment first rolls back the destination's current uncommitted reservation, then transfers the source token.

Committed bytes do not grow forever: callers explicitly release active accounting with `budget_node::release(bytes, tag)`. Release validates aggregate and per-tag active bytes before changing state; underflow returns `errc::wrong_owner` and leaves accounting unchanged. Public `reserve()` and `release()` convert internal allocation failures that occur before mutation to `errc::out_of_memory`.

All accounting arithmetic uses checked helpers. Hard-limit failures return `errc::budget_exceeded` and leave all nodes unchanged. Soft limits emit `soft_limit_exceeded` events but do not reject reservations. High watermark events fire when total reserved plus active bytes crosses from at-or-below to above the configured high watermark. Low watermark events fire when total reserved plus active bytes crosses from above to at-or-below the configured low watermark. Low watermarks default to disabled with the `std::numeric_limits<std::size_t>::max()` sentinel.

Snapshots are immutable value objects returned by `snapshots()` or passed to `export_snapshots(sink)`. They expose process, shard, subsystem, tag, reserved bytes, active bytes, configured limits, and event counters. `snapshots()` and `export_snapshots()` allocate value-owned strings and vectors and may throw `std::bad_alloc`.

Event callbacks use `budget_event_sink`; snapshot export accepts a caller-provided sink. Both are optional and have no logging dependency. `budget_event_sink` requires a sink that is nothrow-invocable with `const budget_event&`. User callbacks are invoked after internal locks have been released, so a callback may call back into the same budget. Rollback/destructor cleanup does not depend on event-record allocation: if a low-watermark event record cannot be allocated while rolling back reserved bytes, accounting is still released and the event is dropped; counters still reflect the crossing.

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
std::expected<std::size_t, errc> checked_sub(std::size_t, std::size_t) noexcept;
std::expected<std::size_t, errc> align_up(std::size_t value, std::size_t alignment) noexcept;
```

`memory_tag` and `source_location` are copied into `allocation_request` so accounting and fault-injection resources can classify allocations without taking ownership of logging or telemetry infrastructure.

## M1 Page and Resource Contracts

M1 adds the following public headers:

```cpp
#include <voris/mem/page_source.hpp>
#include <voris/mem/page_chunk.hpp>
#include <voris/mem/system_resource.hpp>
#include <voris/mem/counting_resource.hpp>
#include <voris/mem/fault_injection_resource.hpp>
```

`os_page_source` discovers the operating-system page size and exposes `reserve`, `commit`, `decommit`, and `release` over `page_span`. Linux uses `mmap`, `mprotect`, `madvise`, and `munmap` internally. Windows and macOS currently compile and keep the same public contract, but page operations return `errc::unsupported_platform`; their complete `VirtualAlloc` and `mmap` implementations remain M6 work.

`basic_page_chunk_manager<PageSource>` reserves page-aligned spans from a page source. Requests that fit the configured chunk size use `page_allocation_kind::chunk`; requests larger than the chunk or at least the direct threshold use `page_allocation_kind::direct`. Commit failure releases the reserved span before returning the provider error.

`system_resource` is a thread-safe, caller-owned resource backed by the platform C allocation APIs. It validates power-of-two alignment, accepts zero-sized requests as empty successful allocations, reports impossible checked arithmetic as `errc::size_overflow`, and exposes usage snapshots by value. `deallocate` is `noexcept` and validates non-empty blocks against the resource's active allocation registry before calling the platform free routine. Null-with-nonzero, non-null-with-zero, foreign pointer, double free, size mismatch, and alignment mismatch releases return `errc::wrong_owner`.

`counting_resource` and `fault_injection_resource` are caller-owned wrappers around `resource_ref`. Their counters use atomics and `usage()` returns an immutable snapshot. Wrapper thread-safety follows the wrapped resource because allocation and deallocation are forwarded through the underlying `resource_ref`. Fault injection can fail deterministically by allocation call number, cumulative requested bytes, or `memory_tag`.

Usage byte accounting uses checked arithmetic. Allocation-side accounting overflow returns `errc::size_overflow` and leaves externally visible usage unchanged except for the failure counter. Deallocation that would underflow wrapper or resource accounting returns `errc::wrong_owner`; wrappers validate block shape and local accounting before forwarding release to the wrapped resource.

## M2 Arena, Pool, Slab, Synchronization, and PMR Contracts

M2 adds the following public headers:

```cpp
#include <voris/mem/arena_resource.hpp>
#include <voris/mem/fixed_block_pool.hpp>
#include <voris/mem/slab_resource.hpp>
#include <voris/mem/synchronized_resource.hpp>
#include <voris/mem/pmr_adapter.hpp>
```

`arena_resource` is caller-owned and shard-confined. It allocates monotonic chunks from an upstream `resource_ref`, validates alignment and checked growth arithmetic, returns empty blocks for zero-sized requests, and supports `reset()` with a retained-bytes limit. Individual non-empty deallocation validates ownership and is otherwise a no-op; memory is reclaimed on `reset()` or resource destruction.

`fixed_block_pool` is caller-owned and shard-confined. It uses checked stride and backing-size arithmetic for configurable block size, alignment, and capacity. Deallocation validates shape, pointer range, stride alignment, owner state, size, and alignment before returning a block to the freelist. `allocate_block()` returns `fixed_block_allocation`, which carries the plain `allocation` plus a resource-specific generation. `deallocate_block()` validates that generation and returns `errc::wrong_owner` for stale descriptors without corrupting the freelist. The plain `allocate()` and `deallocate()` methods remain available for `resource_ref` compatibility but cannot validate a generation that is not present in `allocation`.

`slab_resource` is caller-owned and shard-confined with bounded remote release support. The M2 size classes cover 8, 16, 32, 64, 128, 256, 512, and 1024 byte blocks. `allocate_block()` returns `slab_allocation`, which carries the plain `allocation` plus a resource-specific generation. `deallocate_block()` validates that generation and returns `errc::wrong_owner` for stale descriptors without corrupting slab metadata. The plain `allocate()` and `deallocate()` methods remain available for `resource_ref` compatibility but cannot validate a generation that is not present in `allocation`.

Remote frees are queued in a bounded mutex-protected queue and released by `drain_remote_frees()`. `remote_deallocate_block()` accepts a `slab_allocation` descriptor and validates generation before queueing. Stale remote descriptors return `errc::wrong_owner` and do not disturb the current owner. Plain `remote_deallocate(allocation)` remains available for `resource_ref` compatibility, but it has no caller-provided generation token.

If the queue is full or a queue insertion cannot be completed, a synchronous locked slow path releases the block immediately and increments slow-path counters in `slab_remote_snapshot`; full queues also increment saturation counters. Slab metadata, bucket free lists, slab vectors, and usage counters are protected by a single state mutex. Upstream slab allocation and deallocation are not performed while holding that state mutex; only metadata publication is locked. The remote queue mutex protects only the queue and remote counters. Code does not hold the remote queue mutex while acquiring the state mutex; the full-queue and queue-insertion-failure slow paths release the queue mutex before taking the slow-path/state locks. `slab_options::force_remote_queue_push_failure` is a deterministic test hook for this fallback path and should remain false in production configuration.

`synchronized_resource` wraps any `resource_ref` with a mutex and reports thread-safe traits. It is intended for explicit adaptation at boundaries and is not the default hot-path resource. It is non-reentrant: callers must not use an upstream resource that calls back into the same synchronized wrapper or resource graph while the wrapper lock is held.

`pmr_memory_resource` adapts a VMem `resource_ref` to `std::pmr::memory_resource`. `pmr_resource_adapter` adapts a `std::pmr::memory_resource*` to the VMem resource-like API with active allocation validation. Its upstream calls, active registry, and counters are protected by one mutex, so it reports thread-safe traits even when the wrapped PMR resource does not provide its own synchronization. It is also non-reentrant for the same reason: the wrapped PMR resource must not call back into the same adapter while an adapter operation is active. If an upstream PMR `deallocate` throws during adapter release, the adapter catches the exception, returns `errc::wrong_owner`, and leaves the local active registry and accounting unchanged so the caller can retry or destroy the adapter under the normal live-block contract.

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
