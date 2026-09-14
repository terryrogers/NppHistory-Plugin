# NppHistory Roadmap

## Confirmed Historical Milestones

| Version | State | Evidence |
| --- | --- | --- |
| `0.2.0-beta.20` | Historical prerelease, locally archived | Former Git tag and GitHub Release; retained release assets, release notes and test report dated 29 August 2026 |
| `0.2.0-beta.24` | Historical prerelease, locally archived | Former Git tag and GitHub Release; retained release assets and test report dated 30 August 2026 |
| `0.2.0-beta.25` | Local release candidate | Source version, changelog, release notes, build and test reports; no tag or GitHub Release |
| `1.0.0` | Proposed stable milestone | OpenProject version record only; scope and dates are not approved |

## Current Beta 25 Gate

**Confirmed:** The Public repository was recreated from a sanitised current-source snapshot, project records and durable documentation were reconciled, and the initial automated workflow passed.

**Testing:** A fresh Release x64 build completes with zero warnings and 1,264 core checks pass. The full verifier currently fails at the live Notepad++ gate: restore action/logging, aggregate/display-version logging, and exclusion settings/panel/command-state checks need investigation and retest. Later suite stages, installed-environment UAT, hidden common-history migration verification, post-installation checks, and rollback checks remain open. Automated and isolated results do not substitute for these checks.

**Approval required:** Any `v0.2.0-beta.25` tag and GitHub Release require their own later approval.

## Proposed Stable Work

- Agree measurable `1.0.0` acceptance criteria and compatibility baseline.
- Resolve all beta 25 UAT findings and confirm recovery procedures.
- Review whether bounded retention or pruning is required.
- Improve recovery/export guidance where user evidence shows a need.
- Publish stable artifacts only when the canonical version agrees across source, OpenProject, Git tag, GitHub Release and delivered binaries.

No proposed item is scheduled or authorised for release by this roadmap.
