# ADR 0006 — M5 Debug Hardening and Observability

- Status: Accepted
- Date: 2026-06-24
- Owners: repository maintainers
- Related tasks: VMEM-M5-001, VMEM-M5-002, VMEM-M5-003, VMEM-M5-004, VMEM-M5-005, VMEM-M5-006, VMEM-M5-007

## Context

M5 adds allocator diagnostics without replacing the core M0-M4 resource contracts. The debug layer must catch common ownership and corruption errors, preserve sanitizer visibility, expose leak information on demand, and avoid expanding into M6 platform completion.

## Decision

Add `debug_resource`, a caller-owned wrapper around `resource_ref`. Its own metadata is protected by an internal mutex, but its advertised resource traits follow the wrapped resource so the wrapper does not turn a shard-confined resource into a general cross-thread allocation backend by contract.

The wrapper allocates a larger upstream block, aligns the user payload inside it, and surrounds the payload with configurable redzones. Payload bytes can be poisoned deterministically on allocation and before free. When sanitizer preservation is enabled, ASan builds also poison the debug redzones so payload overruns produce compiler sanitizer diagnostics. Deallocation validates block shape, owner metadata, generation, and redzones before forwarding the release. A plain `allocation` path detects double free and wrong resource through resource-local metadata. `allocate_block()` returns `debug_allocation` with a generation token so callers that retain descriptors can detect stale or wrong-generation releases.

Large allocation guard pages are requested only when `debug_resource_options::guard_pages_for_large_allocations` is enabled and the request reaches the configured threshold. On platforms where `os_page_source` supports reservation and commit, the wrapper reserves guard pages around the committed payload span. Where the page source is unsupported, the allocation falls back to the upstream resource and increments explicit fallback counters. This keeps M5 diagnostics useful on Windows/macOS without claiming M6 page-source completion.

Leak detection is pull-based. `leak_snapshot()` returns active allocation records, and `diff_leak_snapshots(before, after)` reports added and removed records. No global process-exit hook is required or installed.

`slab_resource` gains `size_class_snapshots()`, a narrow observability API that reports active, free, queued remote, and fragmentation bytes per existing size class without changing the M2 slab layout. The snapshot follows the documented state-then-remote lock order and reports queued remote blocks as remote rather than active.

M5 verification uses assert-style focused tests, a deterministic allocator-corruption fuzz smoke target, a threaded debug-resource stress target, public-header compilation, a smoke benchmark, and explicit expected-failure sanitizer visibility probes. Custom resources keep payload memory ordinary and do not replace ASan/UBSan/TSan diagnostics with private allocator correctness.

## Alternatives Considered

A process-exit leak reporter was rejected because VMem resources are caller-owned and tests need deterministic snapshot points. Pull-based snapshots make leak checks composable inside long-running processes and test binaries.

Embedding M5 diagnostics into every existing resource was rejected because it would complicate hot-path allocators and alter M0-M4 behavior. A wrapper keeps hardening opt-in and lets downstream code wrap the exact resource graph it wants to inspect.

Completing Windows `VirtualAlloc` and macOS page-source guard behavior in M5 was rejected because platform completion is M6 scope. M5 records requested, guarded, and fallback counts so unsupported page sources remain visible.

## Consequences

Debug allocations use additional metadata and padding and are not hot-path replacements. Redzone corruption is reported as `errc::wrong_owner`; the block remains active so callers can inspect snapshots and so the resource can release live blocks during destruction.

Guard-page diagnostics depend on the existing `os_page_source` contract. Linux can use real page reservations; unsupported platforms fall back to upstream allocation with explicit counters.

Slab fragmentation snapshots are observational. They do not drain remote queues or compact slabs.

## Verification

M5 is verified by tests for poisoning, redzone corruption, double free, wrong resource, stale generation, guard-page fallback accounting, explicit leak diffs, slab size-class snapshots, threaded debug-resource stress, public-header compilation, deterministic allocator-corruption fuzz smoke, debug observability benchmark builds/runs, and explicit ASan+UBSan/TSan visibility probes where the compiler supports them.
