# VMem Architecture

## 1. Boundary

Low-level memory resources, byte buffers, allocator diagnostics, and hierarchical memory budgets for the Voris stack.

### In Scope

- Provide explicit and testable ownership for hot-path memory.
- Reduce small-object and buffer allocation overhead with shard-local resources.
- Make cross-shard release safe, bounded, and observable.
- Expose memory budgets and pressure signals without depending on a logging framework.
- Preserve sanitizer visibility and deterministic fault injection.

### Out of Scope

- Replacing the process-wide allocator by default.
- Providing coroutine, network, HTTP, cache, or database semantics.
- Implementing garbage collection or a cross-process shared-memory protocol.

## 2. Dependency Boundary

| Dependency | Kind | Version policy |
|---|---|---|
| None | Internal | This repository is the root of the Voris dependency graph. |
| C++ standard library | Required | C++23 baseline. |
| Test/benchmark tools | Development only | Must not leak into the public ABI. |

The repository consumes only released public upstream APIs through VXrepo. Private headers, source copying, and relative cross-repository include paths are prohibited.

## 3. Component Model

| Component | Responsibility |
|---|---|
| OS page source | Reserve, commit, decommit, and release virtual memory. |
| Resource layer | System, arena, fixed-block, slab, counting, and fault-injection resources. |
| Buffer layer | Non-owning views plus unique, shared, and chained byte buffers. |
| Budget layer | Hierarchical reservations, hard/soft limits, watermarks, and snapshots. |
| Debug layer | Poisoning, redzones, guard pages, ownership checks, and leak snapshots. |

## 4. Primary Data Path

```text
OS pages → system resource → page pool → arena/slab/pool → buffers and subsystem objects
```

## 5. Core Invariants

- All size and alignment arithmetic is checked for overflow.
- Every resource declares whether it is thread-safe, shard-confined, or externally synchronized.
- A `resource_ref` is non-owning; the resource outlives every block allocated from it.
- Cross-shard final release cannot leak, silently grow an unbounded queue, or call user code.
- Public headers expose no operating-system allocator type.

## 6. Public API Direction

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

Public interfaces use C++23, move-only ownership where appropriate, `std::expected`-style explicit runtime errors, `std::span`/views for borrowing, and `std::chrono` for time. Provider or operating-system objects remain behind private adapters.

## 7. Error and Resource Model

- Ordinary runtime failures are values, not exceptions crossing subsystem boundaries.
- Error categories are stable identifiers; diagnostic text is not an API contract.
- Peer-controlled sizes and work queues have explicit hard limits.
- Cancellation, deadlines, and shutdown have documented ownership and completion behavior.
- Metrics and event hooks are narrow callbacks and do not create a hard logging dependency.

## 8. Concurrency and Lifetime

- Types declare whether they are thread-safe, shard-confined, immutable, or externally synchronized.
- A view never extends the lifetime of its source.
- No user callback or coroutine is resumed while a private lock protects mutable invariants.
- Cross-shard or cross-thread transfer is explicit and includes ownership transfer.
- Destruction cannot race with a still-referencing backend, waiter, callback, or provider.

## 9. Testing Contract

- Boundary sizes around alignments, pages, and `SIZE_MAX`.
- Randomized allocate/free models, remote-release saturation, and fragmentation.
- Buffer-chain property tests and parser helpers across segment boundaries.
- ASan/UBSan/TSan visibility through every custom resource.
- Fault injection for metadata and payload allocation failures.

## 10. Security Review Areas

- Integer overflow and invalid alignment.
- Use-after-free and wrong-owner release.
- Resource exhaustion and budget bypass.
- Allocator metadata corruption.

## 11. Versioning

During `0.x`, source compatibility may change between minor versions. Downstream package constraints must pin a compatible minor range. A public ABI promise begins only after a separately approved stability milestone.
