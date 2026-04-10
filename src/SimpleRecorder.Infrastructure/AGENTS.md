# Infrastructure module guide

## Own this layer
This project owns:
- settings persistence
- tray integration
- audio device discovery
- managed-to-native interop adapter
- mapping between canonical contracts and native structs

## Hard boundaries
Do not push UI behavior into this layer.
Do not expose native structs or raw ABI details above this layer.
Do not move canonical shared models out of `SimpleRecorder.Contracts`.

## Practical rules
- Keep interop centralized under `Native/`.
- Keep tray behavior robust and explicit.
- Persist settings in a simple, debuggable JSON shape.
- If native functionality is stubbed, still emit realistic status callbacks for the HUD.
