# Benchmarks

Benchmarks record environment, workload, throughput, tail latency, memory, allocation, and system-call data. Results are comparative evidence, not marketing claims.

Current smoke targets:

- `vmem_m2_resources_benchmark`
- `vmem_m3_buffers_benchmark`
- `vmem_m4_budgets_benchmark`
- `vmem_m5_debug_observability_benchmark`
- `vmem_m6_release_benchmark`

The M6 release smoke output can be checked with `tools/check_release_benchmark_thresholds.py`. The thresholds are conservative regression alerts and are documented in [Release Benchmark Thresholds](../docs/RELEASE_BENCHMARKS.md).
