# NppHistory Project Discovery

## Classification And Location

- **Confirmed:** Project category: Software Development.
- **Confirmed:** The project is held in an established local working copy.
- **Confirmed:** This repository's `Source` directory is the authoritative source checkout.
- **Confirmed:** The source folder is a Git repository on `main`; generated build output is ignored.

## Purpose And Users

**Confirmed:** NppHistory is a GPL-3.0 Notepad++ plugin for 64-bit Windows. It automatically saves ordinary files and maintains local revision history that users can browse, compare, comment on, delete, and restore. Its intended users are Notepad++ users who need recoverable local editing history and configurable autosave behaviour.

## Reconstructed Scope

**Confirmed included:** Native Notepad++ integration; configurable autosave triggers and exclusions; adjacent or common hidden `.npphistory` storage; catalogue reconciliation and migration; revision capture, comparison, comments, deletion and safe restore; settings, logging, command placement, hotkeys and tab indicators; update discovery, verified download and restart installation; x64 build, tests, documentation and release packaging.

**Confirmed limitations:** Windows x64 only; a new tab needs an initial save; retention is unlimited; revision content is not encrypted by the plugin; comparison is designed for text; cloud synchronisation is outside the product.

## Repository Reconstruction

- **Confirmed:** GitHub repository: `https://github.com/terryrogers/NppHistory-Plugin`.
- **Confirmed:** The former repository published prereleases `v0.2.0-beta.20` and `v0.2.0-beta.24`; its full history and release assets are retained only in a verified local rollback archive.
- **Confirmed:** The repository was recreated as Public from a sanitised current-source snapshot. The replacement contains no legacy commits, tags or GitHub Releases.
- **Confirmed:** Local and remote `main` match, and the initial GitHub Actions workflow passed.

## OpenProject Reconstruction

**Confirmed:** An OpenProject project named NppHistory Plugin, identifier `NPPHIST`, was copied from the current Software Development template. Template memberships, phases, work-package types, saved views, Status board and Versions board were preserved. Project metadata, versions and 56 inherited work packages were reconstructed from repository evidence.

**Confirmed:** OpenProject links to GitHub. A direct OpenProject URL is intentionally omitted from this Public repository because publishing a private infrastructure address would violate the publication boundary.

## Evidence Classification

- **Confirmed:** Directly supported by source, Git data, published releases, reports, build artifacts, or live project records.
- **Inferred:** A reconstruction that best fits the evidence but is not explicitly recorded as an original decision.
- **Proposed:** Future work, release scope, dates, or decisions that are not yet approved.

## Unresolved Decisions

1. Complete installed-environment beta 25 UAT and migration verification.
2. Resolve or explicitly accept the documented security findings before a release.
3. Decide whether and when to publish `v0.2.0-beta.25`; tagging and releasing require separate exact approval.
4. Define and approve stable `1.0.0` scope and schedule.
