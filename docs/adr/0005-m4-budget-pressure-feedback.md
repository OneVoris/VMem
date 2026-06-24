# ADR 0005 — M4 Budget Pressure Feedback

- Status: Accepted
- Date: 2026-06-24
- Owners: repository maintainers
- Related tasks: VMEM-M4-001, VMEM-M4-002, VMEM-M4-003, VMEM-M4-004, VMEM-M4-005, VMEM-M4-006

## Context

M4 introduces memory pressure feedback above the resource and buffer layers. The budget API must support hierarchical reservations, deterministic limit behavior, tag-aware accounting, and telemetry export without depending on a logging framework. Existing allocators are already implemented and tested; M4 must not force allocator integration before a later milestone.

The same accounting operation can touch a child and all ancestors, so lock ordering and rollback behavior need to be explicit. Reservation tokens also need move semantics that cannot double-account or leak reservations.

## Decision

Add `include/voris/mem/budget.hpp` with `budget_node` and the alias `memory_budget`. Nodes are caller-owned and thread-safe. A parent node must outlive each child and every live token created through that child. Allocators are not wired to budgets in M4.

Each node has process, shard, and subsystem dimensions. Reservations and releases add a `memory_tag`; snapshots copy the tag into value-owned dimensions. Reserving on a child supports at most `max_budget_hierarchy_depth` root-to-leaf nodes. Deeper reservation attempts return `errc::size_overflow` before token creation so destructor rollback can always use its fixed-size allocation-free fallback. Accepted reservations walk from the root to the child, lock nodes in that order, validate checked arithmetic and hard limits on every node, and then update every node. This root-to-leaf ordering avoids parent/child deadlock. If any validation fails, no node is modified.

`reservation_token` is move-only RAII. A live reserved token rolls back on destruction. `commit()` converts reserved bytes to active bytes exactly once, returns `errc::out_of_memory` if traversal allocation fails before mutation, and then leaves the token in a committed state for introspection. Repeated `commit()` and `rollback()` calls after transition are no-op successes. Move construction transfers token state. Move assignment rolls back the destination's current uncommitted reservation before taking the source state; if that cleanup cannot complete, source and destination remain unchanged.

Committed bytes are released explicitly through `budget_node::release(bytes, tag)`. Release checks aggregate and per-tag active bytes on every node before mutating state. Underflow returns `errc::wrong_owner`.

Soft limits, hard limits, high watermarks, and low watermarks compare against total reserved plus active bytes. Soft-limit and high-watermark events fire only on upward crossings. Low-watermark events fire only on downward crossings. Low watermarks default to the disabled `std::numeric_limits<std::size_t>::max()` sentinel. Hard-limit failures return `errc::budget_exceeded` and do not emit logging.

Events use an optional `budget_event_sink`, a narrow callback reference requiring nothrow invocation. Snapshots can be pulled with `snapshots()` or pushed with `export_snapshots(sink)`. Event callbacks and export sinks are invoked after internal locks have been released. `snapshots()` and `export_snapshots()` allocate value-owned snapshot data and may throw `std::bad_alloc`.

Mutation phases do not allocate. Reservation first validates every ancestor, ensures tag entries, and pre-creates event records. Active release pre-creates event records before mutation. Reserved rollback prioritizes accounting cleanup: if event-record allocation fails, the rollback still releases accounting and drops the event while still updating crossing counters.

## Alternatives Considered

Integrating budgets directly into existing resources was rejected for M4 because it would enlarge the milestone and risk changing allocator behavior that M0-M3 already cover. A standalone layer gives downstream code a stable contract while leaving allocator adaptation for a later task.

Using a logging dependency for pressure events was rejected because VMem must remain low-level and independent. A callback reference keeps telemetry optional and caller-owned.

Using a committed RAII handle was considered, but explicit `release(bytes, tag)` keeps committed lifetime independent from allocation handles and avoids imposing a new ownership object on existing resources. The API can add adapter handles later without changing the core accounting contract.

## Consequences

Snapshots copy dimension strings, so snapshot export is simple and independent of the caller's original `memory_tag` lifetime. Budget operations may allocate while preparing tag maps and event vectors, but not during the accounting mutation phase; the layer is intended as the M4 contract surface and smoke benchmark target, not yet the allocator fast path.

Callers must keep parent budgets alive longer than children and tokens. Destroying a budget with live committed accounting is a contract violation; M4 exposes release checks but does not add M5 debug hardening.

## Verification

M4 is verified by public-header compilation tests, focused budget contract tests for hierarchy, limits, watermarks, token movement, release underflow, concurrency, and export dimensions, Debug and Release test runs, and the M4 budget smoke benchmark.
