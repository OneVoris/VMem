# ADR 0003 — M2 Arena, Pool, and Slab Resources

- Status: Accepted
- Date: 2026-06-24
- Owners: repository maintainers
- Related tasks: VMEM-M2-001, VMEM-M2-002, VMEM-M2-003, VMEM-M2-004, VMEM-M2-005, VMEM-M2-006, VMEM-M2-007, VMEM-M2-008, VMEM-M2-009, VMEM-M2-010

## Context

M2 introduces the first hot-path resources above `system_resource`: a monotonic arena, a fixed-block pool, and a small-object slab resource. These resources must keep the M0/M1 contracts: checked size and capacity arithmetic, explicit thread-safety traits, `noexcept` release, and wrong-owner validation before provider deallocation.

The milestone also needs a remote-free path without making a lock-free queue part of the first implementation. A lock-free queue would require documented ABA prevention, memory reclamation, and memory-ordering tests, which are not necessary for the M2 smoke-scale resource contracts.

## Decision

`arena_resource` is a caller-owned, shard-confined monotonic resource over `resource_ref`. It grows chunks geometrically up to `max_chunk_size`, validates alignment and overflow before carving from a chunk, and treats individual non-empty deallocation as validated no-op release. `reset()` rewinds retained chunks and releases chunks above `retained_bytes_limit`.

`fixed_block_pool` is a caller-owned, shard-confined pool with configurable block size, block alignment, and capacity. It allocates one checked backing span from the upstream resource, computes stride with `align_up`, and validates pointer range, stride alignment, size, alignment, allocation state, generation, and owner before returning a block to the freelist. The resource-like `allocate()` and `deallocate()` methods keep the plain `allocation` contract. The generation-aware `allocate_block()` and `deallocate_block()` methods use `fixed_block_allocation { allocation block; std::size_t generation; }` so M2 callers can detect stale descriptors.

The M2 slab size-class table is:

| Class | Block size | Minimum alignment |
|---:|---:|---:|
| 0 | 8 | 8 |
| 1 | 16 | 16 |
| 2 | 32 | 16 |
| 3 | 64 | 16 |
| 4 | 128 | 16 |
| 5 | 256 | 16 |
| 6 | 512 | 16 |
| 7 | 1024 | 16 |

`slab_resource` is caller-owned and shard-confined. A slab page is an upstream allocation split into fixed-size blocks for one size class. The M2 layout stores owner metadata in resource-side tables rather than in user payload bytes: block size, alignment, bucket index, allocation state, and generation counter. This preserves simple validation and avoids exposing private headers or allocator metadata through the public allocation descriptor. The resource-like `allocate()` and `deallocate()` methods keep the plain `allocation` contract. The generation-aware `allocate_block()` and `deallocate_block()` methods use `slab_allocation { allocation block; std::size_t generation; }` so M2 callers can detect stale descriptors.

Remote free uses a bounded mutex-protected queue plus `drain_remote_frees()`. `remote_deallocate_block()` queues `slab_allocation` descriptors and validates generation before queueing; stale remote descriptors return `errc::wrong_owner` without touching the current owner. Plain `remote_deallocate(allocation)` remains for `resource_ref` compatibility and derives the current generation internally, so it cannot validate a caller-held stale generation.

Slab metadata, bucket free lists, slab vectors, and usage counters are protected by one state mutex. Upstream slab allocation and deallocation occur outside that mutex; publishing metadata for a new slab happens after reacquiring the state mutex and checking reserved-byte arithmetic. The remote queue mutex protects only the queue and remote counters; code swaps or observes queue state under that mutex and then releases it before acquiring the state mutex. When the queue is full or queue insertion cannot be completed, the resource uses a synchronous locked slow path after releasing the queue mutex. The path is leak-free and observable through `slab_remote_snapshot` counters for queued, drained, saturated, and slow-path releases. `slab_options::force_remote_queue_push_failure` exists only to exercise this fallback deterministically in tests.

`synchronized_resource` wraps a `resource_ref` with a mutex and reports thread-safe traits. It is not the default hot-path resource; the default arena, pool, and slab resources remain shard-confined. The wrapper is non-reentrant and must not wrap an upstream that calls back into the same wrapper or resource graph while locked. PMR interop is provided in both directions through `pmr_memory_resource` and `pmr_resource_adapter`; the adapter from PMR protects upstream calls, its active registry, and counters with one mutex and reports thread-safe traits. The PMR adapter is also non-reentrant. If PMR `deallocate` throws, adapter release returns `errc::wrong_owner` and leaves local tracking unchanged.

## Alternatives Considered

A lock-free remote-free queue was rejected for M2 because a correct design needs ABA, reclamation, and memory-ordering work that would expand the milestone. The bounded mutex queue is sufficient for observable correctness and avoids unbounded memory growth.

Embedding all owner metadata in block headers was rejected for M2 because it would complicate capacity arithmetic and user alignment. Resource-side metadata is easier to validate with the current `allocation` shape. Future hardening work may add payload-adjacent debug metadata in M5.

Making the synchronized wrapper the default small-object resource was rejected because the architecture calls for shard-local hot paths. Thread-safe adaptation remains explicit.

## Consequences

M2 resources are usable through `resource_ref` and compile independently as public headers. Shard-confined resources are fast-path defaults but require callers to use the documented remote-free or synchronized wrapper when crossing threads.

Generation counters are maintained in owner metadata and validated by the resource-specific descriptor APIs. Plain `allocation` remains generation-free for `resource_ref` compatibility, so callers that require stale-descriptor detection should use `fixed_block_allocation` or `slab_allocation`.

The benchmark target is a smoke-scale reproducibility aid, not a release threshold. It reports simple timing and counter lines for arena fragmentation-like churn, producer-thread slab remote-free saturation/drain behavior, and synchronized-resource contention. Remote-free attempts, drain releases, queue capacity, saturation, and slow-path counts are reported as separate fields.

## Verification

M2 is verified by public-header compilation tests, arena growth/reset/retention tests, fixed-pool full/double-free/wrong-owner and stale-generation tests, slab size-class, stale-generation, generation-aware remote release, deterministic remote-free saturation, forced queue-push-failure fallback, grow rollback, and remote-drain tests, synchronized wrapper tests, PMR adapter threaded serialization and throwing-deallocate tests, `SIZE_MAX` overflow checks, Debug and Release test runs, and the M2 benchmark binary.
