# v0.1.0 Release Evidence

This record describes the evidence required before publishing `v0.1.0`. It complements the release tag, source archive checksum, and GitHub Actions run records.

## Release Identity

- Version: `0.1.0`
- Package: `voris-vmem`
- License: GPLv3 (`GPL-3.0-only`)
- Commercial licensing: separate commercial licenses are available by private agreement with the project owner.
- XMake policy: repository metadata does not constrain XMake; CI installs the current XMake release.

## Required Evidence

The release must include all of the following:

- Repository validator: `python tools/check_repository.py`
- Release readiness validator: `python tools/check_release_readiness.py`
- Debug and Release builds and tests on Ubuntu, Windows, and macOS.
- ASan+UBSan build, normal tests, fuzz smoke, and expected-failure sanitizer visibility probe on Ubuntu.
- TSan build, normal tests, fuzz smoke, and expected-failure sanitizer visibility probe on Ubuntu.
- Fuzz smoke: `vmem_m3_buffer_chain_fuzz` and `vmem_m5_allocator_corruption_fuzz`.
- Stress test: `vmem_m5_debug_stress_test` through `xmake test`.
- Public header compilation: `vmem_public_headers_test` through `xmake test`.
- Example build and run: `vmem_basic_usage_example`.
- Benchmark smoke: `vmem_m2_resources_benchmark`, `vmem_m3_buffers_benchmark`, `vmem_m4_budgets_benchmark`, `vmem_m5_debug_observability_benchmark`, and `vmem_m6_release_benchmark`.
- M6 benchmark threshold check: `python tools/check_release_benchmark_thresholds.py m6-release-benchmark.txt`.

## Local Windows Evidence

The maintainer workspace uses Windows x64 with MSVC. Local environment details:

- Operating system: `Microsoft Windows NT 10.0.26200.0`
- CPU identifier: `Intel64 Family 6 Model 198 Stepping 2, GenuineIntel`
- XMake: `v3.0.9+HEAD.2b184e178`

Local command evidence:

- The local run covers benchmark smoke and public header evidence through the benchmark targets and `vmem_public_headers_test`.
- `python tools/check_repository.py`: passed.
- `python tools/check_m6_spec_claims.py`: passed.
- `python tools/check_release_readiness.py`: passed.
- `xmake f -m debug --build_tests=y && xmake && xmake test`: 14/14 tests passed.
- `xmake f -m release --build_tests=y && xmake && xmake test`: 14/14 tests passed.
- `xmake f -m release --build_examples=y && xmake && xmake run vmem_basic_usage_example`: passed.
- `xmake f -m release --build_fuzzers=y && xmake`: passed.
- `xmake run vmem_m3_buffer_chain_fuzz`: `buffer_chain_fuzz_smoke,seeds=32,status=ok`.
- `xmake run vmem_m5_allocator_corruption_fuzz`: `allocator_corruption_fuzz_smoke,seeds=32,status=ok`.
- `xmake f -m release --build_benchmarks=y && xmake`: passed.
- `xmake run vmem_m6_release_benchmark`: `m6_page_source_roundtrip` 581 us, `m6_huge_page_prefer_fallback` 27 us, `m6_system_resource_aligned_64` 383 us.
- `python tools/check_release_benchmark_thresholds.py C:\tmp\vmem-m6-release-benchmark.txt`: passed.
- `xmake f -m debug --build_sanitizer_probes=y --sanitize=address-undefined && xmake`: passed.
- `xmake run vmem_m5_asan_ubsan_visibility_probe`: `m5_asan_visibility_probe,status=not_instrumented`.
- `xmake run vmem_m5_asan_ubsan_visibility_probe ubsan`: `m5_ubsan_visibility_probe,status=completed_without_ubsan_abort`.
- `xmake f -m debug --build_sanitizer_probes=y --sanitize=thread && xmake`: passed.
- `xmake run vmem_m5_tsan_visibility_probe`: `m5_tsan_visibility_probe,status=not_instrumented`.

Windows/MSVC ASan and TSan visibility probe modes are expected to print `status=not_instrumented`, while the UBSan probe mode completes without a sanitizer abort. ASan+UBSan and TSan release evidence comes from the Ubuntu CI sanitizer jobs.

## CI Evidence

The `CI` workflow contains:

- `ubuntu-latest`, `windows-latest`, and `macos-latest` matrix jobs for repository validation plus Debug and Release build/test.
- `ubuntu-latest-asan-ubsan` for ASan+UBSan tests, fuzz smoke, and sanitizer diagnostics.
- `ubuntu-latest-tsan` for TSan tests, fuzz smoke, and sanitizer diagnostics.

Release publication must use a green CI run on the `v0.1.0` tag or the exact `main` commit being tagged.
