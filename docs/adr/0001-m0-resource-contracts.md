# ADR 0001 — M0 Resource Contracts

- Status: Accepted
- Date: 2026-06-24
- Owners: repository maintainers
- Related tasks: VMEM-M0-001, VMEM-M0-002, VMEM-M0-003, VMEM-M0-004, VMEM-M0-005, VMEM-M0-006, VMEM-M0-007

## Context

VMem needs a small public contract layer before implementing concrete page sources, arenas, slabs, buffers, or budgets. The first contracts must be allocation-free, independently compilable, explicit about failure, and strict about ownership because later resources will rely on them in security-sensitive paths.

The required decisions are zero-sized allocation behavior, out-of-memory behavior, wrong-shard release behavior, and resource destruction while blocks remain live.

## Decision

Zero-sized allocation requests are valid when the requested alignment is valid. A resource returns `allocation{nullptr, 0, alignment}` and deallocating that block is a no-op success. Concrete resources must not allocate metadata solely to represent a zero-sized allocation.

Allocation failures are values, not exceptions. Public allocation APIs return `std::expected<allocation, errc>`. Out-of-memory returns `errc::out_of_memory`; hard budget failure returns `errc::budget_exceeded`; invalid power-of-two alignment returns `errc::invalid_alignment`; arithmetic overflow returns `errc::size_overflow`.

`deallocate` is `noexcept` and returns `std::expected<void, errc>`. Releasing a block through the wrong resource, wrong shard, or wrong owner generation returns `errc::wrong_owner`. A shard-confined resource may support remote release only through an explicitly documented bounded queue or handoff path.

`resource_ref` is a copyable, allocation-free, non-owning type-erased reference. The resource object must outlive every `resource_ref` and every block allocated through it. Destroying a resource with live blocks is a caller contract violation. Diagnostic implementations may report or trap, but normal APIs do not attempt to keep the backend alive implicitly.

Every resource exposes `resource_traits` with ownership, thread-safety, and remote-release classification. Minimum usage accounting is represented by `usage_snapshot`, and resources adapted by `resource_ref` provide a `usage()` method returning that snapshot.

## Alternatives Considered

Returning a non-null sentinel for zero-sized allocations was rejected because it would force resources to manufacture ownership for a block that has no storage and would complicate wrong-owner checks.

Throwing `std::bad_alloc` was rejected because VMem must keep ordinary runtime failure explicit across subsystem boundaries and preserve deterministic fault injection.

Making `resource_ref` own or reference-count resources was rejected because hot-path resources need predictable size, copy cost, and allocation behavior. Owning wrappers can be added later as separate types.

Silently accepting wrong-shard release was rejected because it hides ownership bugs and can turn bounded resources into unbounded remote queues.

## Consequences

Callers must retain clear resource ownership and cannot treat `resource_ref` as a lifetime handle. This keeps the M0 reference type two pointers wide and trivially copyable, but it places lifetime responsibility on resource owners.

Concrete resources must special-case zero-sized requests and must validate alignments and arithmetic before touching provider state. Release paths must remain `noexcept` even when reporting wrong-owner errors.

Later shard-local resources must document their thread-safety classification and either reject remote release with `errc::wrong_owner` or implement a bounded observable path.

## Verification

M0 is verified by standalone public-header compilation tests, ABI/static-assert tests, checked arithmetic boundary tests, `resource_ref` forwarding tests, repository validation, and Debug/Release builds. Later milestones add sanitizer, stress, fuzz, and benchmark evidence for concrete allocation paths.
