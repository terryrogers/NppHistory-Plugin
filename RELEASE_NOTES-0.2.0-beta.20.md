# NppHistory 0.2.0 beta 20

This is a **prerelease for controlled beta testing**, not the final v1.0.0 release.

NppHistory is an open-source 64-bit Notepad++ plugin providing automatic saving, continuous local revision history, revision comments, restoration and a WinMerge-inspired side-by-side comparison interface.

## Verification

- 180 core behavioural checks passed with 0 failures.
- Release plugin and test projects rebuilt with 0 warnings and 0 errors.
- All six required Notepad++ plugin exports were verified.
- The isolated live Notepad++ integration workflow passed.
- Detailed results are in `TEST_REPORT-0.2.0-beta.20.md`.

Verified DLL SHA-256:

```text
64CAE9B42B47C5E23AD0818CBF1BEBE52CD3AFCBBEAC7740685F1D0797F621CF
```

## Installation

1. Close Notepad++.
2. Extract the binary package.
3. Copy its `NppHistory` folder into the Notepad++ `plugins` directory.
4. Start Notepad++ and open **Plugins > NppHistory**.

For a portable installation, use the `plugins` folder beside `notepad++.exe`.

## Beta limitations

- Windows x64 only.
- New untitled tabs must be saved once before they can be versioned.
- History retention is currently unlimited.
- History is local and is not encrypted by NppHistory.
- The update settings are placeholders; update checking is not implemented in this release.
- Manual UAT remains outstanding for the full release-readiness matrix documented in the test report.
