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
xmake run vmem_m3_buffer_chain_fuzz
```

The M2 benchmark uses producer threads for the remote-free workload and keeps the workload smoke-scale for reproducible local validation. Its output is descriptive metadata and counters, not a release threshold.

The M3 buffer benchmark prints deterministic comma-separated lines for append, consume, and neutral gather conversion with 1, 2, 4, and 16 segments. The M3 fuzz target is a dependency-free deterministic smoke binary that exercises randomized append/prepend/consume/trim/coalesce/parser operations with fixed seeds.

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
| `with_voris_dependencies` | `false` | Resolve internal packages through VXrepo. |

Project-specific component options are documented in `xmake.lua` comments and the architecture document.

## Sanitizers

Use separate build directories or CI workspaces for ASan+UBSan and TSan. Do not combine TSan with ASan.

## Validation

```bash
python tools/check_repository.py
```

The validator checks required files, documentation language, ignored Agent documents, TODO identifiers, and relative Markdown links.
