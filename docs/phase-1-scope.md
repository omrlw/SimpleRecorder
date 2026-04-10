# Phase 1 scope

## In scope
- Scaffold the full solution and projects from scratch if needed.
- Packaged WinUI 3 app shell.
- Floating HUD window.
- Tray icon with minimal commands.
- Persisted settings model.
- Real contracts and service interfaces.
- Managed-to-native wiring through a stable adapter.
- Native engine stubs for initialize, start, pause, resume, stop, and screenshot.
- HUD state transitions driven by service callbacks, not fake UI-only timers except for the explicit countdown UX.

## Out of scope
Do not implement real screen capture, real MP4 encode, real AAC audio, real microphone/system mix, or production screenshot pipeline in Phase 1 unless the task explicitly moves the roadmap forward.

## Acceptance bar
Phase 1 should leave the repo able to satisfy these checks once the Windows toolchain is present:
- Restore and build Debug x64.
- Open the app without crashing.
- Show a compact floating HUD.
- Show a tray icon and basic tray menu.
- Persist settings across relaunch.
- Move HUD state through record, pause, resume, and stop using the native stub path.
- Keep `Presentation` free from direct Win32/media/native references.

## Implementation posture
When unsure, prefer:
- compile-safe scaffolding over incomplete media code
- explicit contracts over hidden coupling
- stable boundaries over convenience shortcuts
- simple stub behavior over premature low-level complexity
