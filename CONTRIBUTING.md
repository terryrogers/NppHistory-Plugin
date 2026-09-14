# Contributing to NppHistory

Thank you for helping improve NppHistory.

## Before submitting a change

1. Open an issue for substantial behavioural or interface changes.
2. Keep changes focused and preserve existing history/configuration compatibility.
3. Add or update direct core tests for logic and storage changes.
4. Build the x64 Release plugin and test executable.
5. Run the core suite and, for UI or Notepad++ integration changes, the live runtime test.

```powershell
msbuild .\vs.proj\NppHistory.vcxproj /t:Rebuild /m /p:Configuration=Release /p:Platform=x64
msbuild .\vs.proj\NppHistory.Tests.vcxproj /t:Rebuild /m /p:Configuration=Release /p:Platform=x64
.\build\tests\NppHistory.Tests.exe
.\tests\runtime_smoke.ps1
```

Do not include private note contents, history data, local paths, credentials or runtime test artefacts in a commit or issue.

## Pull requests

Describe the behaviour changed, tests performed and any manual-UAT boundary. Compilation alone is not evidence that a visual or live Notepad++ workflow works.
