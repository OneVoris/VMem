# ADR 0004 — M3 Buffer Infrastructure

- Status: Accepted
- Date: 2026-06-24
- Owners: repository maintainers
- Related tasks: VMEM-M3-001, VMEM-M3-002, VMEM-M3-003, VMEM-M3-004, VMEM-M3-005, VMEM-M3-006, VMEM-M3-007, VMEM-M3-008, VMEM-M3-009, VMEM-M3-010

## Context

M3 introduces byte buffers above the resource layer. The buffer APIs must keep M0-M2 invariants: checked size and alignment arithmetic, value-returned errors, `noexcept` release, explicit lifetime ownership, and no public operating-system scatter/gather types.

The shared buffer contract needs deterministic behavior when the final reference is dropped from a different thread or shard. The existing `resource_ref` abstraction only had ordinary `deallocate`, while M2 resources can separately advertise bounded remote deallocation.

## Decision

Borrowed ranges are represented by `const_buffer` and `mutable_buffer`, both pointer-plus-size byte views. Non-empty null views are invalid. Slicing rejects invalid view shape with `errc::wrong_owner`, validates offset/count with checked helpers, and returns `errc::size_overflow` for invalid bounds.

`unique_buffer` is move-only and owns a single allocation from `resource_ref`. It tracks logical size separately from allocation capacity, rejects resize beyond capacity, and releases through the original resource. Destruction ignores the release result, while explicit `reset()` returns provider errors for tests and deterministic cleanup paths.

Move assignment for `unique_buffer` and `shared_buffer` first releases the current destination owner. If release fails, assignment is a no-op: the destination keeps its original owner and the source keeps the incoming owner so both can be released later.

`shared_buffer` uses an intrusive control block allocated with the payload. Copy construction is intentionally not provided; callers use `clone()` so reference-count overflow can return `errc::size_overflow`. The control block records owner generation, maximum reference count, payload capacity/alignment, resource reference, allocation descriptor, and whether final release must use remote deallocation.

Final shared-buffer release occurs on the thread that releases the last reference. Creation accepts cross-thread final release only when the backing resource is thread-safe or advertises remote deallocation. Thread-safe resources use ordinary `deallocate`; remote-capable non-thread-safe resources use `resource_ref::remote_deallocate`. Resources that are neither thread-safe nor remote-capable are rejected with `errc::wrong_owner` unless callers explicitly disable cross-thread final-release acceptance for local-only use. If the final deallocation fails, the handle remains attached with use count one so the same release can be retried after the owner/resource problem is corrected.

`resource_ref` now carries an optional remote-deallocate vtable entry. Resource types with a public `remote_deallocate(allocation) noexcept -> std::expected<void, errc>` member dispatch to it; other thread-safe resources fall back to ordinary `deallocate`, and non-thread-safe resources without a remote path return `errc::wrong_owner`. A default-constructed `resource_ref` is invalid but safe: operations return `errc::wrong_owner`, while traits and usage return empty values. This keeps the `resource_ref` object size unchanged while allowing M3 shared buffers to honor M2 remote-release traits.

`buffer_chain` uses four inline segment slots before spilling to `std::vector`. It is move-only because segments may own move-only buffers. Chain slicing creates borrowed views over the original chain; callers must keep the original storage alive for the slice lifetime. Moving a chain transfers the segments and leaves the source empty. Borrowed non-empty null views are rejected before storage in the chain. Bounded coalescing returns `errc::budget_exceeded` before allocating when the caller's explicit limit would be exceeded.

Scatter/gather conversion is private under `src/`. Public headers expose only neutral buffers and parser helpers. POSIX `iovec` and Windows `WSABUF` conversions are guarded by platform macros and are used only by tests or internal targets.

## Alternatives Considered

Implicit shared-buffer copy construction was rejected because refcount overflow cannot be reported from a copy constructor. Explicit `clone()` keeps overflow deterministic and testable.

Always requiring thread-safe backing resources for shared buffers was rejected because M2 already defines resources with bounded remote release. Extending `resource_ref` with remote-deallocate dispatch lets shared buffers use that public capability without exposing resource internals.

Putting scatter/gather adapters in public headers was rejected because it would leak POSIX or Windows networking types into the public ABI. A private adapter header keeps platform types out of installed headers.

## Consequences

Shared-buffer users must choose between thread-safe resources, remote-capable resources, or local-only creation. The final release thread is explicit and tested; no background worker or user callback is invoked.

`buffer_chain` keeps small chains allocation-free for segment metadata. Larger chains can allocate segment storage and report `errc::out_of_memory` from mutating operations if metadata allocation fails.

The M3 benchmark and fuzz targets are smoke-scale validation tools. They do not define release thresholds, but they provide reproducible coverage for append, consume, gather conversion, and randomized chain state transitions.

## Verification

M3 is verified by public-header compilation tests, focused buffer contract tests, deterministic property-style chain tests, a deterministic fuzz smoke target, Debug and Release test runs, and the M3 buffer benchmark.
