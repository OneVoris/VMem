# Release Benchmark Thresholds

Release benchmarks are regression evidence, not absolute product claims. Every release record must include commit, compiler, standard library, flags, CPU, operating system, workload, elapsed time, and any local thermal or virtualization constraints.

## Smoke Alert

M6 adds `vmem_m6_release_benchmark`, which prints deterministic comma-separated lines:

- `m6_page_source_roundtrip`
- `m6_huge_page_prefer_fallback`
- `m6_system_resource_aligned_64`

Run the smoke benchmark and threshold check with:

```bash
xmake f -m release
xmake -g benchmarks
xmake run vmem_m6_release_benchmark > m6-release-benchmark.txt
python tools/check_release_benchmark_thresholds.py m6-release-benchmark.txt
```

The current conservative thresholds are:

| Workload | Alert threshold |
|---|---:|
| `m6_page_source_roundtrip` | 10,000,000 us |
| `m6_huge_page_prefer_fallback` | 2,000,000 us |
| `m6_system_resource_aligned_64` | 2,000,000 us |

These thresholds are broad smoke alerts intended to catch hangs, pathological slowdowns, or accidental debug-only paths in release readiness checks. They are guidance unless a CI job explicitly adopts them. A release manager may tighten them only with recorded before/after data from the target platform class.

## Regression Policy

If a threshold fails, keep the raw output, rerun once on an otherwise idle machine, and compare against the previous accepted release record. Do not raise thresholds without documenting the platform reason, the previous result, and the new result.
