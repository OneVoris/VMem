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

## Required Configurations

- Debug.
- Release.
- ASan + UBSan.
- TSan for concurrent code.
- Fuzz configuration for parser/format targets.
- Tier-1 compiler and operating-system matrix.

## Benchmark Record

Every reported result includes commit, compiler, standard library, flags, CPU, operating system/kernel, workload, throughput, latency percentiles, peak RSS, allocations per operation, and errors/timeouts. A single RPS number is not a release argument.

## Completion Rule

A TODO item is not complete until its failure paths, documentation, and required measurements are part of the same change.
