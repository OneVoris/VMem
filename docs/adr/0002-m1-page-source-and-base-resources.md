# ADR 0002 — M1 Page Source and Base Resources

- Status: Accepted
- Date: 2026-06-24
- Owners: repository maintainers
- Related tasks: VMEM-M1-001, VMEM-M1-002, VMEM-M1-003, VMEM-M1-004, VMEM-M1-005, VMEM-M1-006, VMEM-M1-007

## Context

M1 needs the first concrete allocation backends without expanding into arena, slab, buffer, budget, or debug-resource milestones. The Linux page source must be usable now, while Windows `VirtualAlloc` and macOS complete page-source behavior are scheduled for M6. The resource layer must preserve the M0 contracts: `std::expected` errors, `noexcept` deallocation, checked arithmetic, explicit thread-safety traits, and allocation-free `resource_ref` adaptation.

## Decision

`os_page_source` exposes page-size discovery plus reserve, commit, decommit, and release over a plain `page_span`. Linux implements these operations with private `mmap`, `mprotect`, `madvise`, and `munmap` calls. Non-Linux platforms compile the same public API and return `errc::unsupported_platform` for page operations in M1. Windows page-size discovery may use the platform API because it is not a completed page-source implementation.

`basic_page_chunk_manager<PageSource>` is a small template over the page-source contract. This lets tests exercise reserve, commit, cleanup, page alignment, and direct-allocation behavior with a fake page source on every platform. A concrete `page_chunk_manager` alias binds the template to `os_page_source`.

`system_resource` is a caller-owned, thread-safe resource over the platform aligned allocation APIs. It validates alignment, handles zero-sized requests without allocating storage, uses checked arithmetic before allocation, returns immutable usage snapshots, and keeps `deallocate` `noexcept`. A mutex-protected active allocation registry records pointer, size, and alignment so invalid release attempts are rejected before the platform free routine is called.

`counting_resource` and `fault_injection_resource` wrap `resource_ref`. Their counters are atomic and snapshots are returned by value. Their thread-safety traits follow the wrapped resource because forwarding still depends on the underlying resource's synchronization contract.

Usage byte accounting must use checked arithmetic, including `checked_sub` for subtracting active bytes during deallocation. Allocation-side accounting overflow returns `errc::size_overflow` and rolls back any local accounting for the failed call. Deallocation-side accounting underflow returns `errc::wrong_owner`. Wrappers reject invalid block shapes and validate local accounting before calling the wrapped resource so an invalid release cannot be forwarded first.

## Alternatives Considered

Completing all operating-system page sources in M1 was rejected because Windows and macOS platform semantics are explicitly M6 scope. Returning a clear `unsupported_platform` value gives CI coverage for headers and contracts without claiming those implementations are complete.

Hiding chunk management entirely in a Linux-only source file was rejected because reserve and commit failure cleanup would then be hard to test on the current Windows workspace. A page-source template keeps the policy testable without exposing operating-system types.

Using a third-party allocator for `system_resource` was rejected because M1 must not add runtime or development dependencies and VMem core correctness cannot be delegated to a third-party allocator.

## Consequences

Linux has a real page-source path in M1. Windows and macOS callers can compile against the API and receive deterministic `errc::unsupported_platform` from page operations until M6.

The page chunk manager is intentionally small. It reserves and commits whole page spans and distinguishes normal chunks from direct large spans, but it does not implement arena, slab, freelist, guard-page, or budget behavior.

Resource wrappers provide deterministic accounting and failure injection for later tests. They do not make an externally synchronized underlying resource thread-safe; they report traits based on the wrapped resource.

Accounting checks add a small compare-exchange loop around byte counters. This keeps release builds from silently wrapping usage snapshots without changing the public resource ownership model. The active allocation registry adds minimum wrong-owner detection for `system_resource`; it is not a general debug allocator and does not implement M5 corruption diagnostics.

## Verification

M1 is verified by public-header compilation tests, fake-page-source reserve and commit failure tests, alignment and zero-size system-resource tests, `SIZE_MAX` checked-arithmetic tests, checked accounting overflow and underflow tests, fault-injection trigger tests, counting-resource snapshot tests, and the repository build/test matrix.
