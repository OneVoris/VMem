# Building Voris Memory

## Requirements

- XMake 3.0.0 or newer.
- A C++23 compiler and standard library.
- Python 3.11 or newer for repository validation tools.
- VXrepo registration when `--with_voris_dependencies=y` is enabled.

## Basic Builds

```bash
xmake f -m debug --build_tests=y
xmake
xmake test

xmake f -m release --build_tests=y
xmake
xmake test
```

Benchmarks are optional and are enabled separately:

```bash
xmake f -m debug --build_tests=y --build_benchmarks=y --build_fuzzers=y
xmake
xmake run vmem_m2_resources_benchmark
xmake run vmem_m3_buffers_benchmark
xmake run vmem_m4_budgets_benchmark
xmake run vmem_m5_debug_observability_benchmark
xmake run vmem_m3_buffer_chain_fuzz
xmake run vmem_m5_allocator_corruption_fuzz
```

The M2 benchmark uses producer threads for the remote-free workload and keeps the workload smoke-scale for reproducible local validation. Its output is descriptive metadata and counters, not a release threshold.

The M3 buffer benchmark prints deterministic comma-separated lines for append, consume, and neutral gather conversion with 1, 2, 4, and 16 segments. The M3 fuzz target is a dependency-free deterministic smoke binary that exercises randomized append/prepend/consume/trim/coalesce/parser operations with fixed seeds.

The M4 budget benchmark prints deterministic comma-separated lines for standalone hierarchical reservation, commit, release, and concurrent rollback smoke workloads. It validates that snapshots are empty after each workload.

The M5 debug observability benchmark prints deterministic comma-separated lines for debug allocation/deallocation, leak snapshot diffing, and slab size-class snapshot pulls. The M5 allocator-corruption fuzz target is a dependency-free deterministic smoke binary that corrupts redzones and verifies `debug_resource` reports ownership failure without losing live-block observability.

Sanitizer visibility probes are opt-in and are not registered as normal tests because instrumented runs are expected to emit sanitizer diagnostics:

```bash
xmake f -m debug --build_sanitizer_probes=y
xmake build vmem_m5_asan_ubsan_visibility_probe
xmake build vmem_m5_tsan_visibility_probe

# On Clang/GCC ASan builds, expected to fail with an ASan use-after-poison report.
ASAN_OPTIONS=halt_on_error=1 xmake run vmem_m5_asan_ubsan_visibility_probe

# On Clang/GCC UBSan builds, expected to fail with a signed-overflow report when halt_on_error is set.
UBSAN_OPTIONS=halt_on_error=1 xmake run vmem_m5_asan_ubsan_visibility_probe ubsan

# On Clang/GCC TSan builds, expected to fail with a data-race report when halt_on_error is set.
TSAN_OPTIONS=halt_on_error=1 xmake run vmem_m5_tsan_visibility_probe
```

On unsupported local toolchains, such as this repository's Windows/MSVC default, the probe binaries build and print `status=not_instrumented`.

M1 is expected to build on Ubuntu, Windows, and macOS. Linux exercises the real OS page source. Windows and macOS exercise the same public headers, portable resources, and fake-page-source chunk contracts while `os_page_source` page operations return `errc::unsupported_platform` until the M6 platform implementations are completed.

## Resolve Voris Dependencies

```bash
xrepo add-repo vxrepo <VXREPO_GIT_URL>
xmake f -m debug --with_voris_dependencies=y --build_tests=y
xmake
```

The repository never assumes sibling source checkouts. Development overrides must be local, explicit, and uncommitted.

## Options

| Option | Default | Meaning |
|---|---:|---|
| `build_shared` | `false` | Build the primary library as a shared library. |
| `build_tests` | `false` | Build and register test targets. |
| `build_examples` | `false` | Build examples when implementation examples exist. |
| `build_benchmarks` | `false` | Build benchmark targets. |
| `build_fuzzers` | `false` | Build fuzz targets with the selected toolchain. |
| `build_sanitizer_probes` | `false` | Build explicit expected-failure M5 sanitizer visibility probes. |
| `with_voris_dependencies` | `false` | Resolve internal packages through VXrepo. |

Project-specific component options are documented in `xmake.lua` comments and the architecture document.

## Sanitizers

Use separate build directories or CI workspaces for ASan+UBSan and TSan. Do not combine TSan with ASan.

Where the compiler supports sanitizer flags, run the M5 debug, stress, fuzz, and explicit visibility probe targets under ASan+UBSan and TSan. The debug wrapper keeps payload bytes visible to sanitizers; its redzone and generation checks are additional diagnostics, not replacements for compiler instrumentation.

## Validation

```bash
python tools/check_repository.py
```

The validator checks required files, documentation language, ignored Agent documents, TODO identifiers, and relative Markdown links.
