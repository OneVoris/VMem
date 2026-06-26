# VMem — Voris Memory

Low-level memory resources, byte buffers, allocator diagnostics, and hierarchical memory budgets for the Voris stack.

> Status: v0.1.0 initial release. The public API and ABI remain unstable during the 0.x line.

## Design Priorities

1. Correctness and security boundaries.
2. Diagnosability and deterministic failure behavior.
3. Tail latency and bounded resource usage.
4. Throughput and allocation efficiency.
5. API convenience.

## Goals

- Provide explicit and testable ownership for hot-path memory.
- Reduce small-object and buffer allocation overhead with shard-local resources.
- Make cross-shard release safe, bounded, and observable.
- Expose memory budgets and pressure signals without depending on a logging framework.
- Preserve sanitizer visibility and deterministic fault injection.

## Non-Goals

- Replacing the process-wide allocator by default.
- Providing coroutine, network, HTTP, cache, or database semantics.
- Implementing garbage collection or a cross-process shared-memory protocol.

## Repository Isolation

This is an independent repository. It must not use relative includes into another Voris checkout, Git submodules for Voris libraries, copied upstream source, or private upstream headers. Required Voris packages are resolved through **VXrepo** and XMake package requirements.

Required internal packages: none.

Optional internal packages: none.

When an upstream defect or missing capability blocks work, contributors must first refresh VXrepo/upstream state and reproduce against the newest compatible upstream release. A downstream workaround is not the default. See [Repository Isolation](docs/REPOSITORY_ISOLATION.md) and [Dependencies](docs/DEPENDENCIES.md).

## Repository Layout

```text
VMem/
├── include/voris/mem/
├── src/
├── tests/
├── fuzz/
├── benchmarks/
├── examples/
├── docs/
│   └── adr/
├── tools/
├── xmake.lua
├── voris-package.toml
├── TODO.md
└── AGENTS.md              # local Chinese instructions; ignored by Git
```

## Documentation

- [Architecture](ARCHITECTURE.md)
- [Roadmap](ROADMAP.md)
- [TODO List](TODO.md)
- [API Conventions](docs/API.md)
- [Building](docs/BUILDING.md)
- [Testing](docs/TESTING.md)
- [Dependencies](docs/DEPENDENCIES.md)
- [Migration Notes](docs/MIGRATION.md)
- [Repository Isolation](docs/REPOSITORY_ISOLATION.md)
- [Release Process](docs/RELEASES.md)
- [Release Evidence](docs/RELEASE_EVIDENCE.md)
- [Release Benchmark Thresholds](docs/RELEASE_BENCHMARKS.md)
- [Security Policy](SECURITY.md)
- [Contributing](CONTRIBUTING.md)

## Build VMem

```bash
xmake f -m debug
xmake
xmake test
```

VMem builds with the current XMake release and does not constrain a minimum XMake version in repository metadata. The repository currently has no internal upstream package dependencies, so normal builds do not require VXrepo registration.

## Package Identity

- VXrepo package: `voris-vmem`
- C++ namespace: `voris::mem`
- Primary XMake target: `voris_vmem`
- Language baseline: C++23
- Version: `0.1.0`

## Licensing

VMem is licensed under the GNU General Public License version 3 (`GPL-3.0-only`). See [LICENSE](LICENSE) and [docs/LICENSING.md](docs/LICENSING.md). Separate commercial licenses are available by private agreement with the project owner.
