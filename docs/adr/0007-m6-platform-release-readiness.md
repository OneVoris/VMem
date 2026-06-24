# ADR 0007 - M6 Platform and Release Readiness

- Status: Accepted
- Date: 2026-06-24
- Owners: repository maintainers
- Related tasks: VMEM-M6-001, VMEM-M6-002, VMEM-M6-003, VMEM-M6-004, VMEM-M6-005

## Context

M6 completes the first release-readiness pass after M1-M5 established the resource, buffer, budget, and debug layers. The remaining gaps are platform page-source completion on Windows and macOS, explicit cache-line assumptions for the supported CPU families, optional huge-page behavior, public migration material, and reproducible benchmark alerts.

The M1 `os_page_source` intentionally returned `errc::unsupported_platform` for page operations outside Linux. M5 guard-page diagnostics also reported fallback counts on those platforms because the page source was not yet complete.

## Decision

`os_page_source` now implements real reserve, commit, decommit, and release operations on Windows with `VirtualAlloc`, `VirtualProtect`-compatible page protection through commit state, and `VirtualFree`. macOS uses `mmap`, `mprotect`, advisory discard where available, and `munmap`. Linux keeps the existing `mmap`, `mprotect`, `madvise`, and `munmap` behavior.

Huge pages are opt-in through `page_source_options`. The default `reserve(size)` behavior continues to request ordinary pages. When callers set `prefer_huge_pages`, the implementation may attempt a platform huge-page allocation. If the attempt is unsupported or unavailable and `allow_huge_page_fallback` is true, the call safely falls back to ordinary pages. If fallback is disabled, failure is reported as an explicit `errc` value. Huge-page allocations that require committed-at-reserve semantics are tracked internally so `commit`, `decommit`, and `release` preserve the public `page_span` contract.

Cache-line assumptions are exposed in `platform.hpp`. The repository provides and statically checks a 64-byte cache line only for x86_64 and arm64 branches, including preprocessor simulation for both branches. Unknown architectures report `cache_line_size == 0` and must not use VMem cache-line constants as a layout promise. Runtime validation remains release evidence that requires a target runner for the CPU/OS pair.

Release benchmark readiness uses a smoke-scale `vmem_m6_release_benchmark` plus `tools/check_release_benchmark_thresholds.py`. The thresholds are intentionally conservative local regression alerts. They are not portable marketing claims and must be recalibrated before being used as hard CI gates on a new platform.

## Alternatives Considered

Enabling huge pages by default was rejected because large-page privileges, pool configuration, and decommit semantics vary by operating system. A default-on policy could turn ordinary allocations into surprising failures.

Publishing cache-line constants for every target was rejected because the repository only establishes x86_64 and arm64 assumptions statically. Unknown targets must remain explicit instead of inheriting a false 64-byte claim.

Replacing release benchmarks with one fixed throughput number was rejected because local CPU, OS, and compiler differences dominate smoke workloads. Conservative upper-bound alerts catch obvious regressions while still requiring full release records for real performance claims.

## Consequences

Windows and macOS are no longer placeholder page-source platforms. Unknown platforms still compile the public API and return `errc::unsupported_platform` for page operations.

Huge pages are safe to request opportunistically but are not guaranteed. Linux queries `/proc/meminfo` for `Hugepagesize` and falls back to the common 2 MiB request size when that query is unavailable or malformed. Callers that require huge pages must disable fallback and handle `unsupported_platform`, `out_of_memory`, or `invalid_alignment`.

The M6 release benchmark adds another optional target. Release owners must store benchmark output with environment metadata and run the threshold script when using the smoke alert.

## Verification

M6 is verified by platform page-source contract tests, public-header compilation for `platform.hpp`, preprocessor-simulated cache-line static assertions for x86_64 and arm64, huge-page fallback tests, repository validation, and Debug/Release build and test runs. Release verification additionally builds examples and runs the M6 release benchmark smoke target plus the benchmark threshold script.
