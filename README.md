# SimpleRecorder

Current recording/export vertical slice for a packaged WinUI 3 desktop recorder on Windows.

For the current HUD and settings visual work, `SimpleRecorder.pen` is the active source of truth.
Google Sans is the target typeface; when it is not installed locally, the app falls back to `Segoe UI Variable Display` and then `Segoe UI`.

What is already in this repo:

- `SimpleRecorder.sln` with the five required modules
- x64-first root build settings
- a compact HUD shell in `SimpleRecorder.Presentation`
- tray and settings persistence in `SimpleRecorder.Infrastructure`
- a versioned native DLL ABI with a real recording slice behind `Infrastructure`

What is real in the current slice:

- display, region, and best-effort window capture through the native DLL
- live H.264/MP4 recording through `capture -> bounded queue -> encode -> live mp4 output`
- `.srrec` session folders kept for `manifest.json` metadata and telemetry, not per-frame BMP storage
- pause/resume/stop state changes driven by the same contract-facing interfaces used by the HUD and tray

What is intentionally not implemented yet:

- real microphone or loopback audio capture
- screenshot capture; SimpleRecorder is a recorder-only product
- preview rendering
- advanced source-picker polish beyond the current deterministic display/window picker and precision region overlay

## Structure

- `src/SimpleRecorder.App`: app bootstrap, window lifetime, dependency composition
- `src/SimpleRecorder.Presentation`: HUD views, state, viewmodels, theme
- `src/SimpleRecorder.Contracts`: shared enums, models, interfaces
- `src/SimpleRecorder.Infrastructure`: settings store, tray integration, native adapter
- `src/SimpleRecorder.Engine.Native`: x64 DLL with the versioned C ABI and the minimum real capture slice
- `build/`: setup and restore helpers
- `docs/`: scope and architecture references

## Prerequisites

- Windows 10/11
- .NET 8 SDK
- Visual Studio with WinUI / Windows App SDK tooling
- Desktop development with C++
- Windows 10/11 SDK

Run the environment check first:

```powershell
.\build\setup-dev-env.ps1
```

Restore the managed projects from the repo root:

```powershell
.\build\restore.ps1
```

Build the full solution with Visual Studio MSBuild. Do not use `dotnet build` for the full solution, because the repo includes a native `.vcxproj`.

If MSBuild and the VC++ workload are available, build the full solution in `Debug|x64`:

```powershell
& "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" .\SimpleRecorder.sln /restore /p:Configuration=Debug /p:Platform=x64
```

## Slice Notes

- `Presentation` does not talk directly to Win32, Media Foundation, D3D11, or the native ABI.
- The native engine captures frames into a bounded native queue and encodes into the MP4/H.264 output during recording.
- The `.srrec` folder remains on disk for manifest metadata and telemetry; the contract-facing output path is the live `.mp4`.
- Screenshot is intentionally not exposed by the app, HUD, tray, managed contracts, or managed native adapter.
- Precision region selection is expressed in physical virtual-desktop pixels so DPI scaling, mixed-monitor layouts, and negative coordinates stay explicit at the contract boundary.
- Audio and preview remain intentionally stubbed.
- Settings are persisted to `%LocalAppData%\Packages\SimpleRecorder.App\LocalState\settings.json`, with migration from the older `%LocalAppData%\SimpleRecorder\settings.json` path when present.
