# Changelog

All notable changes will be documented here.

## [Unreleased]

- Repository architecture and implementation-planning scaffold.
- Added M0 public memory contracts, checked arithmetic helpers, allocation descriptors, resource classification traits, memory tags, usage snapshots, and allocation-free `resource_ref` type erasure.
- Added public header compilation tests, ABI/static contract tests, and M0 resource contract tests.
- Added M1 Linux OS page source, portable `system_resource`, page-backed chunk manager, counting and fault-injection resources, and Windows/macOS placeholder contracts.
- Added M1 resource contract tests, new public-header compilation tests, and a GitHub Actions build/test matrix for Ubuntu, Windows, and macOS.
- Added M2 arena, fixed-block pool generation descriptors, slab resource generation descriptors with bounded synchronized remote-free handling, generation-aware remote release, deterministic remote slow-path test coverage, synchronized wrapper, exception-safe thread-safe PMR adapters, M2 contract tests, public-header tests, size-class ADR, and smoke benchmarks.
- Added M3 byte buffer views, move-only unique buffers, explicitly cloned intrusive shared buffers, small-inline buffer chains, bounded coalescing, private scatter/gather adapters, cross-segment parser helpers, deterministic property/fuzz coverage, public-header tests, and append/consume/gather smoke benchmarks.
- Added M4 standalone hierarchical memory budgets with move-only reservation tokens, process/shard/subsystem/tag snapshots, soft and hard limits, watermark events, callback-based export, concurrent contract tests, public-header tests, and reservation smoke benchmarks.
- Added M5 debug hardening with `debug_resource`, configurable redzones and poisoning, ASan redzone preservation hooks, double-free/wrong-owner/stale-generation detection, debug guard-page requests with platform-specific allocation/fallback assertions, explicit leak snapshot diffing, slab per-size-class observability snapshots, allocator-corruption fuzz coverage, sanitizer visibility probes, stress tests, public-header tests, and debug observability smoke benchmarks.
- Added M6 Windows and macOS page-source implementations, opt-in huge-page requests with safe ordinary-page fallback, Linux huge-page size discovery from `/proc/meminfo`, x86_64/arm64 cache-line assumption APIs with static branch checks, platform contract tests, public migration notes, a buildable basic usage example, a release benchmark smoke target, and conservative benchmark threshold alerts.
