# Building Voris Memory

## Requirements

- The current XMake release. VMem does not pin a minimum XMake version in repository metadata.
- A C++23 compiler and standard library.
- Python 3.11 or newer for repository validation tools.

## Basic Builds

```bash
xmake f -m debug
xmake
xmake test

xmake f -m release
xmake
xmake test
```

## IDE and LSP

Generate `compile_commands.json` in the repository root for VS Code clangd:

```bash
xmake project -k compile_commands --lsp=clangd .
```

VMem's source baseline is C++23. The checked-in `.clangd` points clangd at the root compilation database and adds a C++23 parser flag through `-Xclang -std=c++23`. This keeps normal XMake project generation on `set_languages("c++23")` while avoiding false standalone-header diagnostics when clangd's header command inference drops MSVC preview standard flags.

For Microsoft C/C++ cpptools, regenerate a compatible database with:

```bash
xmake project -k compile_commands --lsp=cpptools .
```

Benchmarks and fuzz smoke targets are optional non-default targets and are built separately by group:

```bash
xmake f -m debug
xmake -g benchmarks
xmake -g fuzz
xmake run vmem_m2_resources_benchmark
xmake run vmem_m3_buffers_benchmark
xmake run vmem_m4_budgets_benchmark
xmake run vmem_m5_debug_observability_benchmark
xmake run vmem_m6_release_benchmark > m6-release-benchmark.txt
python tools/check_release_benchmark_thresholds.py m6-release-benchmark.txt
xmake run vmem_m3_buffer_chain_fuzz
xmake run vmem_m5_allocator_corruption_fuzz
```

The M2 benchmark uses producer threads for the remote-free workload and keeps the workload smoke-scale for reproducible local validation. Its output is descriptive metadata and counters, not a release threshold.

The M3 buffer benchmark prints deterministic comma-separated lines for append, consume, and neutral gather conversion with 1, 2, 4, and 16 segments. The M3 fuzz target is a dependency-free deterministic smoke binary that exercises randomized append/prepend/consume/trim/coalesce/parser operations with fixed seeds.

The M4 budget benchmark prints deterministic comma-separated lines for standalone hierarchical reservation, commit, release, and concurrent rollback smoke workloads. It validates that snapshots are empty after each workload.

The M5 debug observability benchmark prints deterministic comma-separated lines for debug allocation/deallocation, leak snapshot diffing, and slab size-class snapshot pulls. The M5 allocator-corruption fuzz target is a dependency-free deterministic smoke binary that corrupts redzones and verifies `debug_resource` reports ownership failure without losing live-block observability.

The M6 release benchmark prints deterministic comma-separated lines for page-source round trips, huge-page preference with fallback, and aligned system-resource allocation. Redirect its output to a file before running `tools/check_release_benchmark_thresholds.py`. The thresholds are conservative release-readiness alerts, not portable performance claims.

Examples are optional and are enabled separately:

```bash
xmake f -m debug
xmake -g examples
xmake run vmem_basic_usage_example
```

Sanitizer visibility probes are opt-in and are not registered as normal tests because instrumented runs are expected to emit sanitizer diagnostics:

```bash
xmake f -m debug --policies=build.sanitizer.address,build.sanitizer.undefined
xmake build vmem_m5_asan_ubsan_visibility_probe

# On Clang/GCC ASan builds, expected to fail with an ASan use-after-poison report.
ASAN_OPTIONS=halt_on_error=1 xmake run vmem_m5_asan_ubsan_visibility_probe

# On Clang/GCC UBSan builds, expected to fail with a signed-overflow report when halt_on_error is set.
UBSAN_OPTIONS=halt_on_error=1 xmake run vmem_m5_asan_ubsan_visibility_probe ubsan

xmake f -m debug --policies=build.sanitizer.thread
xmake build vmem_m5_tsan_visibility_probe

# On Clang/GCC TSan builds, expected to fail with a data-race report when halt_on_error is set.
TSAN_OPTIONS=halt_on_error=1 xmake run vmem_m5_tsan_visibility_probe
```

On unsupported local toolchains, such as this repository's Windows/MSVC default, the ASan and TSan probe modes print `status=not_instrumented`; the UBSan mode completes without a sanitizer abort.

VMem is expected to build on Ubuntu, Windows, and macOS. Linux, Windows, and macOS exercise the real OS page-source contract for reserve, commit, decommit, and release. Unknown platforms keep the same public headers and return `errc::unsupported_platform` for page operations.

## Resolve Voris Dependencies

VMem currently has no internal upstream package dependencies. The repository never assumes sibling source checkouts. Development overrides must be local, explicit, and uncommitted.

## XMake Controls

| Control | Meaning |
|---|---|
| `-k static` | Build the primary library as a static library. This is the default. |
| `-k shared` | Build the primary library as a shared library and expose `VORIS_VMEM_SHARED`. |
| `-m debug` | Debug build. |
| `-m release` | Release build. |
| `--policies=build.sanitizer.address,build.sanitizer.undefined` | Enable ASan and UBSan policies on supported toolchains. |
| `--policies=build.sanitizer.thread` | Enable TSan policy on supported toolchains. |

Non-default runnable targets are organized by XMake group:

| Group | Build command |
|---|---|
| `tests` | `xmake test` |
| `examples` | `xmake -g examples` |
| `benchmarks` | `xmake -g benchmarks` |
| `fuzz` | `xmake -g fuzz` |
| `sanitizer-probes` | `xmake -g sanitizer-probes` |

## Sanitizers

Use separate build directories or CI workspaces for ASan+UBSan and TSan. Do not combine TSan with ASan.

Where the compiler supports sanitizer flags, run the M5 debug, stress, fuzz, and explicit visibility probe targets under ASan+UBSan and TSan. The debug wrapper keeps payload bytes visible to sanitizers; its redzone and generation checks are additional diagnostics, not replacements for compiler instrumentation.

XMake persists `--policies` in the local `.xmake` configuration cache. After sanitizer experiments, run `xmake f -c -m debug` before regenerating daily IDE metadata.

## Validation

```bash
python tools/check_repository.py
python tools/check_release_readiness.py
```

The repository validator checks required files, documentation language, ignored Agent documents, TODO identifiers, and relative Markdown links. The release readiness validator checks v0.1.0 version metadata, GPLv3 licensing, commercial licensing notes, CI sanitizer coverage, release evidence, and Definition of Done status.
