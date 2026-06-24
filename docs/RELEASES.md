# Release Process

## Versioning

VMem uses semantic versions. During `0.x`, minor versions may contain source-incompatible public API changes; patch versions must remain compatible within the minor line.

## Release Sequence

1. Close the milestone's required TODO items and ADRs.
2. Run repository validation, tests, sanitizers, fuzz regressions, and required benchmarks.
3. Run release benchmark threshold alerts documented in [Release Benchmark Thresholds](RELEASE_BENCHMARKS.md) when the milestone has benchmark gates.
4. Update `CHANGELOG.md`, compatibility notes, and `voris-package.toml`.
5. Create and sign the source tag `vX.Y.Z`.
6. Publish an immutable source archive and SHA-256.
7. Submit the new version to VXrepo.
8. Verify a clean VXrepo install in Debug/Release and supported feature combinations.
9. Notify downstream repositories only after the VXrepo recipe is available.

## Upstream/Downstream Order

An upstream release must be available through VXrepo before a downstream release can depend on it. Downstream repositories must not release against an unreleased upstream commit.

## Compatibility

- Public error identifiers are not reused.
- Disk or wire formats require explicit version/compatibility policy.
- Removed APIs require migration notes.
- Provider interfaces document minimum and maximum supported versions.

## Licensing Gate

Public releases use the GNU General Public License version 3 (`GPL-3.0-only`) unless a later release explicitly changes the license through the same review process. Package metadata must reflect the GPLv3 license before tagging.

Separate commercial licenses are available by private agreement with the project owner. Commercial licensing is not implied by a public source tag, package recipe, or binary artifact.
