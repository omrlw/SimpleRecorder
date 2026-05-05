# Contributing

Keep changes small, buildable, and aligned with the current native recording/export architecture.

## Setup
```powershell
.\build\setup-dev-env.ps1
.\build\restore.ps1
& "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\amd64\MSBuild.exe" .\SimpleRecorder.sln /restore /p:Configuration=Debug /p:Platform=x64
```

Use Visual Studio MSBuild for the full solution because the repo includes `SimpleRecorder.Engine.Native.vcxproj`.

## Architecture Rules
- Keep the five modules separate: `App`, `Presentation`, `Contracts`, `Infrastructure`, `Engine.Native`.
- Keep canonical enums, DTOs, and service interfaces in `SimpleRecorder.Contracts`.
- Keep native interop and `sr_*` struct mapping inside `SimpleRecorder.Infrastructure/Native`.
- Keep Win32, WGC, DXGI, D3D11, Media Foundation, WASAPI, and ABI structs out of `Presentation`.
- Keep capture/encode internals inside `SimpleRecorder.Engine.Native`.
- Evolve the C ABI additively and update `docs/native-abi.md` when structs, exports, or manifest fields change.

## Current Product Guardrails
- Preserve display, window, and region recording through the native DLL.
- Preserve GPU-first capture and H.264/MP4 export.
- Preserve conservative hardware encode attribution; only report verified hardware when the engine can justify it.
- Do not add screenshot as a product feature.
- Keep audio capture, preview, HDR, and vendor-direct encoder SDKs behind explicit future work.

## Validation
- Docs only: review Markdown and run `git diff --check`.
- Managed or UI changes: build `Debug | x64`.
- Native engine, ABI, or adapter changes: build `Debug | x64` and `Release | x64`.
- Startup/HUD/tray/settings changes: launch the app in a configured Windows App Runtime environment.
- Recording changes: record, pause, resume, stop, then inspect the `.mp4` and `.srrec/manifest.json`.

## Commit Style
Keep one concern per change. Prefer Conventional Commit prefixes such as `feat:`, `fix:`, `docs:`, `refactor:`, and `chore:`.
