# Changelog

All notable changes will be documented here.

## [Unreleased]

- Repository architecture and implementation-planning scaffold.
- Added M0 public memory contracts, checked arithmetic helpers, allocation descriptors, resource classification traits, memory tags, usage snapshots, and allocation-free `resource_ref` type erasure.
- Added public header compilation tests, ABI/static contract tests, and M0 resource contract tests.
- Added M1 Linux OS page source, portable `system_resource`, page-backed chunk manager, counting and fault-injection resources, and Windows/macOS placeholder contracts.
- Added M1 resource contract tests, new public-header compilation tests, and a GitHub Actions build/test matrix for Ubuntu, Windows, and macOS.
- Added M2 arena, fixed-block pool generation descriptors, slab resource generation descriptors with bounded synchronized remote-free handling, generation-aware remote release, deterministic remote slow-path test coverage, synchronized wrapper, exception-safe thread-safe PMR adapters, M2 contract tests, public-header tests, size-class ADR, and smoke benchmarks.
