# Simple Recorder agent guide

## Project intent
Build and evolve **Simple Recorder** as a packaged **WinUI 3 desktop app** for Windows with a compact floating HUD, tray integration, persisted settings, and a native recording engine behind a stable C ABI. In the current slice, preserve and improve the existing recording/export path while continuing to add missing capabilities deliberately behind the same architecture.

## Current reality
Treat the repository as an in-progress recording/export vertical slice. The five-project solution already exists, the native engine already owns a real video capture/export path, and some areas still remain stubbed or partial. If expected files are missing, create them instead of bending the architecture to fit the gap.

## Source of truth
Use these files deliberately:
- `docs/current-scope.md` for what is in and out of scope in the active slice.
- `docs/architecture.md` for module boundaries and the target tree.
- `docs/native-abi.md` when touching interop or the native engine.
- `docs/ui-hud-state.md` when changing HUD states, reducer logic, or motion.
- `docs/execution-plan-template.md` when a task is multi-step or risky.

## Required architecture
Keep the solution split into these modules:
- `SimpleRecorder.App`
- `SimpleRecorder.Presentation`
- `SimpleRecorder.Contracts`
- `SimpleRecorder.Infrastructure`
- `SimpleRecorder.Engine.Native`

Do not collapse these layers just to move faster.

## Non-negotiable boundaries
- `Presentation` owns XAML views, viewmodels, reducer/state, theme, and UI motion.
- `Contracts` is the canonical public model layer shared by UI and services.
- `Infrastructure` owns settings persistence, tray integration, device discovery, and the adapter to the native engine.
- `Engine.Native` exposes a **versioned C ABI** and may use Windows native/media APIs internally.
- `App` composes dependencies and app/window lifetime.

Never reference Win32, Media Foundation, D3D11, WASAPI, or native structs from `Presentation`.
Never let Views or ViewModels know about the C ABI.
Never duplicate canonical models that already belong in `Contracts`.

## Current slice guardrails
- Preserve the packaged WinUI 3 shell, HUD, tray, persisted settings, contracts, and the existing native recording/export slice.
- Keep incomplete areas such as audio capture, native screenshot, preview, and production source picking behind stable interfaces until the roadmap explicitly expands them.
- Prefer compile-safe, incremental changes over speculative rewrites.
- Do not bypass the C ABI or collapse module boundaries to move faster.

## Build and verification expectations
When the scaffold exists, prefer verifying from the solution root.
Typical checks:
- restore solution dependencies
- build Debug x64
- run the app if the requested change affects startup, HUD behavior, tray behavior, or settings persistence

If the local environment is missing required Windows/Visual Studio tooling, report the missing prerequisite clearly instead of claiming success.

## How to work on larger tasks
For multi-step changes, create or update a temporary `PLANS.md` in the repo root using `docs/execution-plan-template.md`. Keep `AGENTS.md` stable; put task-specific reasoning in the plan file, not here.

## Change discipline
Prefer small, reviewable edits.
Keep naming consistent with the docs.
Preserve x64-first assumptions.
Preserve the packaged-app direction.
Do not replace the C ABI with C#/WinRT projection work unless the task explicitly changes the architecture.

## Done means
A task is not done until the change respects the module boundaries, updates the nearest relevant docs when behavior or architecture changes, and leaves the repo closer to the current product slice and target architecture than before.
