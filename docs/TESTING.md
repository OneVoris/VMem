# Testing Strategy

## Test Layers

1. Unit tests for pure algorithms and value types.
2. Contract tests for public behavior and interchangeable backends/providers.
3. Integration tests for real operating-system, file, network, or dependency behavior.
4. Differential or interoperability tests against independent implementations where applicable.
5. Fuzz tests for parsers, codecs, formats, and state transitions.
6. Stress tests for cancellation, close, shutdown, concurrency, and resource pressure.
7. Benchmarks for throughput, p50/p95/p99/p99.9 latency, memory, allocations, and system calls.

## Repository-Specific Focus

- Boundary sizes around alignments, pages, and `SIZE_MAX`.
- Randomized allocate/free models, remote-release saturation, and fragmentation.
- Buffer-chain property tests and parser helpers across segment boundaries.
- ASan/UBSan/TSan visibility through every custom resource.
- Fault injection for metadata and payload allocation failures.
- M1 page-source contract tests cover Linux page operations when available and fake-source reserve/commit failure cleanup on every platform.
- M2 resource contract tests cover arena growth/reset/retention, fixed-pool alignment/full/double-free/wrong-owner paths, fixed-pool and slab stale generation descriptors, slab generation-aware remote release, slab size-class selection, remote-free drain, deterministic full-queue and forced queue-push-failure slow paths, slab grow rollback on reserved-byte overflow, synchronized wrapper behavior, PMR adapter thread-safe traits, serialized threaded PMR adapter allocation against a detecting upstream, throwing PMR deallocate behavior, and overflow checks near `SIZE_MAX`.
- M3 buffer contract tests cover borrowed view slicing, unique-buffer move/reset/resize ownership, shared-buffer final release on the releasing thread, owner generation checks, deterministic reference-count overflow, remote final release dispatch, small-inline chain append/prepend/consume/trim/slice/coalesce, bounded coalesce failure, parser helpers across segment boundaries, private gather conversion, and deterministic randomized chain properties.
- M4 budget contract tests cover child reservations exhausting parent hard limits, hard-limit failure atomicity across dimensions, depth-limit rejection before token creation, soft-limit events without rejection, high/low watermark crossings, disabled default low watermarks, callbacks that re-enter the same budget after locks are released, move construction and move assignment of reservation tokens, commit/rollback idempotence, release underflow, concurrent reservations, and snapshot export dimensions.

## Required Configurations

- Debug.
- Release.
- ASan + UBSan.
- TSan for concurrent code.
- Fuzz configuration for parser/format targets.
- Tier-1 compiler and operating-system matrix.

## Benchmark Record

Every reported result includes commit, compiler, standard library, flags, CPU, operating system/kernel, workload, throughput, latency percentiles, peak RSS, allocations per operation, and errors/timeouts. A single RPS number is not a release argument.

The M2 smoke benchmark target is `vmem_m2_resources_benchmark`. It prints deterministic comma-separated lines for arena fragmentation-like churn, producer-thread slab remote-free saturation/drain behavior, and synchronized-resource contention. The remote-free line reports remote attempts, drain releases, producer thread count, queue capacity, queued count, drained total, saturation count, and slow-path count separately.

The M3 smoke benchmark target is `vmem_m3_buffers_benchmark`. It reports append, consume, and neutral gather conversion workloads for 1, 2, 4, and 16 segments. The M3 fuzz smoke target is `vmem_m3_buffer_chain_fuzz`.

The M4 smoke benchmark target is `vmem_m4_budgets_benchmark`. It prints deterministic comma-separated lines for reserve/commit/release and concurrent reserve/rollback workloads, and terminates if either workload leaves non-empty snapshots behind. The output is a local validation aid, not a release threshold.

## Completion Rule

A TODO item is not complete until its failure paths, documentation, and required measurements are part of the same change.
