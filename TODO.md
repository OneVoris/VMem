# VMem TODO List

Task identifiers follow `VMEM-M<milestone>-<sequence>`. A task may be checked only after the repository-level Definition of Done is satisfied.

## M0 — Contracts and Repository Skeleton

- [x] **VMEM-M0-001** Create the `voris_vmem` XMake target, public include layout, and export macros.
- [x] **VMEM-M0-002** Define `errc`, the allocation descriptor, resource ownership, and thread-safety classifications.
- [x] **VMEM-M0-003** Implement overflow-safe `align_up`, checked addition and multiplication, and power-of-two validation.
- [x] **VMEM-M0-004** Define allocation-free, copyable, non-owning `resource_ref` type erasure.
- [x] **VMEM-M0-005** Define `memory_tag`, source-location capture, and the minimum usage-counter interface.
- [x] **VMEM-M0-006** Add standalone public-header compilation tests and ABI size/static-assert tests.
- [x] **VMEM-M0-007** Write an ADR for zero-sized allocations, out-of-memory behavior, wrong-shard release, and resource destruction with live blocks.

## M1 — OS Pages and Base Resources

- [x] **VMEM-M1-001** Implement the Linux page source: reserve, commit, decommit, release, and page-size discovery.
- [x] **VMEM-M1-002** Implement a portable `system_resource` with aligned allocation.
- [x] **VMEM-M1-003** Implement page-backed chunk management and a direct path for large allocations.
- [x] **VMEM-M1-004** Implement `fault_injection_resource` with failure triggers by call count, byte count, and tag.
- [x] **VMEM-M1-005** Implement `counting_resource` and immutable usage snapshots.
- [x] **VMEM-M1-006** Add contract placeholders and CI gates for Windows `VirtualAlloc` and macOS `mmap` page sources.
- [x] **VMEM-M1-007** Test reserve/commit failures, alignment, zero-sized requests, and sizes near `SIZE_MAX`.

## M2 — Arena, Pool, and Slab Resources

- [x] **VMEM-M2-001** Implement a monotonic arena with growth policy, reset, and retained-page limits.
- [x] **VMEM-M2-002** Implement a fixed-block pool with configurable alignment.
- [x] **VMEM-M2-003** Design the size-class table and slab page layout and record them in an ADR.
- [x] **VMEM-M2-004** Implement the shard-local slab freelist fast path.
- [x] **VMEM-M2-005** Implement block owner and generation validation.
- [x] **VMEM-M2-006** Implement a bounded MPSC remote-free queue and drain operation.
- [x] **VMEM-M2-007** Define an observable, leak-free slow path for a full remote-free queue.
- [x] **VMEM-M2-008** Implement a thread-safe wrapper and document that it is not the default hot-path resource.
- [x] **VMEM-M2-009** Implement adapters to and from `std::pmr::memory_resource`.
- [x] **VMEM-M2-010** Add fragmentation, cross-shard free, and contention benchmarks.

## M3 — Buffer Infrastructure

- [x] **VMEM-M3-001** Implement `const_buffer`, `mutable_buffer`, and bounds-checked slicing.
- [x] **VMEM-M3-002** Implement move-only `unique_buffer` with capacity, size, and resource ownership.
- [x] **VMEM-M3-003** Implement intrusive-reference-counted `shared_buffer` and define cross-shard final release.
- [x] **VMEM-M3-004** Implement a small-inline `buffer_chain`.
- [x] **VMEM-M3-005** Implement append, prepend, consume, trim, and slice operations.
- [x] **VMEM-M3-006** Implement bounded coalescing that returns an error instead of allocating beyond the limit.
- [x] **VMEM-M3-007** Implement private POSIX `iovec` and Windows `WSABUF` adapters.
- [x] **VMEM-M3-008** Add cross-segment parser helpers for integer peeking, delimiter search, and prefix copy.
- [x] **VMEM-M3-009** Add property tests and fuzzing for buffer-chain operations.
- [x] **VMEM-M3-010** Benchmark append, consume, and gather conversion with 1, 2, 4, and 16 segments.

## M4 — Budgets and Pressure Feedback

- [x] **VMEM-M4-001** Implement hierarchical budget reservation tokens.
- [x] **VMEM-M4-002** Support process, shard, subsystem, and tag dimensions in accounting snapshots.
- [x] **VMEM-M4-003** Define soft limits, hard limits, and high/low watermark events.
- [x] **VMEM-M4-004** Ensure reservation-token move, rollback, and commit cannot double-account.
- [x] **VMEM-M4-005** Test concurrent reservations and parent/child budget exhaustion.
- [x] **VMEM-M4-006** Provide snapshot and export callbacks without a logging dependency.

## M5 — Debugging, Hardening, and Observability

- [x] **VMEM-M5-001** Implement poison-on-allocate/free and configurable redzones.
- [x] **VMEM-M5-002** Detect double free, wrong resource, and wrong generation.
- [x] **VMEM-M5-003** Implement guard pages for large allocations in debug mode.
- [x] **VMEM-M5-004** Implement leak-snapshot diffing without requiring a global process-exit hook.
- [x] **VMEM-M5-005** Verify that custom resources preserve ASan, UBSan, and TSan diagnostics.
- [x] **VMEM-M5-006** Add allocator-corruption fuzz and stress harnesses.
- [x] **VMEM-M5-007** Export per-size-class active, free, remote, and fragmentation snapshots.

## M6 — Platforms and Release Readiness

- [x] **VMEM-M6-001** Complete Windows and macOS page sources.
- [x] **VMEM-M6-002** Establish cache-line and alignment assumption APIs with x86_64/arm64 static checks; target-runner runtime validation remains release evidence.
- [x] **VMEM-M6-003** Add optional huge-page support, disabled by default with safe fallback.
- [x] **VMEM-M6-004** Publish API documentation, examples, and migration notes.
- [x] **VMEM-M6-005** Establish release benchmark thresholds and regression alerts.

## Definition of Done

- [ ] Behavioral contracts, thread safety, and lifetime rules are documented publicly.
- [ ] Normal, overflow, OOM, wrong-owner, and concurrent-release paths are tested.
- [ ] Debug, Release, ASan+UBSan, and TSan configurations pass.
- [ ] Hot-path work has reproducible benchmarks with no unexplained RSS or fragmentation regression.
- [ ] Every public header compiles independently and exposes no private OS or third-party types.
- [ ] TODOs, ADRs, and the changelog are updated in the same change.
