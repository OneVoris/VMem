# Roadmap

Milestones are capability gates rather than calendar promises. A later milestone may be explored in a branch, but it must not become the default until earlier release gates pass.

## M0 — Contracts and Repository Skeleton

Complete every `VMEM-M0-*` task in [TODO.md](TODO.md).

**Exit gate:** tests for the milestone pass in Debug and Release; public behavior is documented; sanitizer/fuzz/benchmark requirements relevant to the milestone are recorded.

## M1 — OS Pages and Base Resources

Complete every `VMEM-M1-*` task in [TODO.md](TODO.md).

**Exit gate:** tests for the milestone pass in Debug and Release; public behavior is documented; sanitizer/fuzz/benchmark requirements relevant to the milestone are recorded.

## M2 — Arena, Pool, and Slab Resources

Complete every `VMEM-M2-*` task in [TODO.md](TODO.md).

**Exit gate:** tests for the milestone pass in Debug and Release; public behavior is documented; sanitizer/fuzz/benchmark requirements relevant to the milestone are recorded.

## M3 — Buffer Infrastructure

Complete every `VMEM-M3-*` task in [TODO.md](TODO.md).

**Exit gate:** tests for the milestone pass in Debug and Release; public behavior is documented; sanitizer/fuzz/benchmark requirements relevant to the milestone are recorded.

## M4 — Budgets and Pressure Feedback

Complete every `VMEM-M4-*` task in [TODO.md](TODO.md).

**Exit gate:** tests for the milestone pass in Debug and Release; public behavior is documented; sanitizer/fuzz/benchmark requirements relevant to the milestone are recorded.

## M5 — Debugging, Hardening, and Observability

Complete every `VMEM-M5-*` task in [TODO.md](TODO.md).

**Exit gate:** tests for the milestone pass in Debug and Release; public behavior is documented; sanitizer/fuzz/benchmark requirements relevant to the milestone are recorded.

## M6 — Platforms and Release Readiness

Complete every `VMEM-M6-*` task in [TODO.md](TODO.md).

**Exit gate:** tests for the milestone pass in Debug and Release; public behavior is documented; sanitizer/fuzz/benchmark requirements relevant to the milestone are recorded.

## Cross-Repository Gate

A milestone that depends on a new upstream capability may start only after that capability is released by the owning repository and published in VXrepo. Downstream repositories do not implement private copies of upstream behavior.

## First Release Gate

- Public headers compile independently.
- Required dependency versions are published in VXrepo.
- CI passes on the declared Tier-1 platform/compiler matrix.
- Security-sensitive parsers and state machines have fuzz or adversarial tests.
- The changelog and compatibility notes describe every public behavior change.
- A project license has been selected and added.
