# Architecture Overview

## Design Goals

- high-performance Windows-first recorder
- premium but minimal HUD UX
- GPU-first pipeline
- maintainable modular monolith

## Modules

### SimpleRecorder.App

Owns process startup, dependency composition, and the main WinUI window.

### SimpleRecorder.Presentation

Owns the HUD view, settings surface, state transitions, and viewmodels.

### SimpleRecorder.Contracts

Owns shared enums, models, and service interfaces. This is the canonical managed boundary.

### SimpleRecorder.Infrastructure

Implements settings persistence, tray integration, device discovery, and the adapter between managed code and the recorder backend.

### SimpleRecorder.Engine.Native

Scaffold for the native engine that will eventually host:

- Windows.Graphics.Capture
- Direct3D 11 processing
- Media Foundation encoding
- WASAPI audio capture and mixing

## Interop Strategy

The solution uses one interop strategy consistently:

- native side exposes a C ABI
- managed side consumes it through P/Invoke
- contract mapping lives only in `SimpleRecorder.Infrastructure`

This keeps the UI clean and prevents WinRT/native details from leaking into application code.

## Current Phase

Phase 1 wires the product shell around stable contracts and a stubbed recorder implementation. It intentionally avoids real capture and encode logic until the shell, state model, settings, and repository workflow are stable.
