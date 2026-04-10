# Native ABI

## Why this boundary exists
Use a **versioned C ABI** between C# and the native engine.
This keeps the managed UI clean, limits build complexity, and avoids projecting native/media concepts directly into the UI layer.

## Managed side rules
- Use `LibraryImport` for native entry points.
- Keep all native interop declarations inside `SimpleRecorder.Infrastructure/Native`.
- Map managed contract models to native POD structs in one place.
- Never expose `sr_*` structs above `Infrastructure`.

## Expected exported functions
The native DLL should expose coarse-grained commands such as:
- create and destroy engine
- initialize engine
- start session
- pause session
- resume session
- stop session
- take screenshot
- enumerate audio inputs
- set callback

## Phase 1 behavior
Phase 1 native code should:
- compile cleanly as x64
- accept calls through the ABI
- simulate realistic state transitions
- return stub results for stop and screenshot flows
- provide callback-driven status updates

It should not pretend to do real capture or encode work.

## ABI design rules
- Keep structs POD-friendly and versionable.
- Prefer explicit fields over hidden allocation contracts.
- Keep ownership rules obvious.
- Avoid leaking Windows Runtime or COM-specific shapes across the ABI.
- Add new functionality by extension, not by breaking existing signatures.
