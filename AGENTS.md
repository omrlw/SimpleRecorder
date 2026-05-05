# SimpleRecorder Agent Guide

## Purpose
SimpleRecorder is a minimal, high-performance Windows recorder. Keep the product simple on the surface and professional inside: compact HUD, no watermark, no artificial recording limit, H.264 MP4 output, GPU-first native recording, and clear telemetry.

## Product Rules
- Record display, window, and region sources.
- Do not add camera recording.
- Do not add screenshot capture as a product feature.
- Keep H.264 as the active product codec for compatibility.
- Prefer GPU capture/processing and hardware encode when Windows exposes a compatible NVIDIA, AMD, Intel, or platform encoder.
- Audio is part of the product direction: desktop audio, microphone audio, both, or neither. The current native audio path is not complete, so keep audio work behind contracts and stable interfaces until implemented.
- UI work follows `SimpleRecorder.pen` for design, components, and typography.

## Project Shape
- `SimpleRecorder.App`: app startup, window lifetime, dependency composition.
- `SimpleRecorder.Presentation`: HUD, settings UI, viewmodels, reducer/state, theme, motion.
- `SimpleRecorder.Contracts`: canonical shared enums, DTOs, models, and service interfaces.
- `SimpleRecorder.Infrastructure`: settings, tray, source selection, audio device discovery, native adapter.
- `SimpleRecorder.Engine.Native`: versioned C ABI, native capture, GPU frame processing, H.264 encoding, manifest telemetry.

## Boundaries
- Keep canonical shared models in `Contracts`.
- Keep Views and ViewModels away from the native ABI.
- Keep Win32, WGC, DXGI, D3D11, Media Foundation, WASAPI, and `sr_*` structs out of `Presentation`.
- Keep managed native interop under `Infrastructure/Native`.
- Keep native media implementation inside `Engine.Native`.
- Do not collapse the five projects.

## Documentation Map
- `README.md`: product summary and current state.
- `docs/ARCHITECTURE.md`: module boundaries and pipeline shape.
- `docs/native-abi.md`: C ABI, native behavior, and manifest notes.
- `docs/ui-hud-state.md`: HUD state, motion, and design source.
- `docs/DEVELOPMENT.md`: setup, build, run, and verification.

Keep nearby docs updated when behavior, architecture, ABI, or product direction changes.

## Verification
From the solution root, prefer:
- Docs only: review Markdown and run `git diff --check`.
- Managed/UI changes: build `Debug | x64`.
- Native engine, ABI, manifest, or adapter changes: build `Debug | x64` and `Release | x64`.
- Startup, HUD, tray, settings, or recording behavior: run the app when local Windows tooling is available.

If Visual Studio, Windows App SDK tooling, Desktop C++, or Windows SDK is missing, report the missing prerequisite clearly.
