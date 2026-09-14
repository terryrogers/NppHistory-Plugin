# NppHistory Release Process

## Release Controls

Every release must use one canonical version across `src/Version.h`, OpenProject, the exact Git commit, Git tag, GitHub Release, checksums and binaries. A successful build or local installation does not authorise publication or deployment.

## Prepare

1. Confirm the intended version and release scope in OpenProject.
2. Confirm that release notes and changelog describe the same version.
3. Build Release x64 and run the direct, smoke, isolated and applicable installed-environment tests.
4. Record failures and verification boundaries; do not convert source-only or isolated evidence into UAT claims.
5. Review dependencies, licences, secrets, generated content, user data and the complete outgoing Git history.
6. Produce SHA-256 checksums for the exact DLL and updater artifacts.

## Approval Checkpoints

- **Push:** Present repository, owner, visibility, branch, exact commits/files, checks performed, exclusions and risks. Push only after explicit approval.
- **Tag:** Present the exact tag name and commit. Create or push it only after separate explicit approval.
- **GitHub Release:** Present the title, body, prerelease state and exact assets. Publish only after separate explicit approval.
- **Installation or deployment:** Confirm rollback state and the exact artifact hash before putting a version into use.

## Publish And Verify

1. Push the approved branch without rewriting history.
2. Independently verify the remote commit and repository visibility.
3. After tag approval, create the exact annotated tag on the approved commit and verify it remotely.
4. After release approval, publish only the reviewed assets and checksum manifest.
5. Verify the GitHub Release, asset hashes and update feed independently.
6. Synchronise the deployed version, OpenProject version and Versions board only after deployment is proven.

## Rollback

Retain the previously installed DLL and configuration/catalogue backup. If replacement or restart verification fails, restore the known-good DLL, preserve logs and user history, record the failure, and leave the failed candidate unpublished or clearly marked.
