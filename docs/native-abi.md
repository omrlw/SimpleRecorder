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
- prepare recording output path
- start session
- pause session
- resume session
- stop session
- enumerate audio inputs
- set callback

## Current behavior
The current ABI remains on version `2`. The lifecycle exports and callback signature stay intact, so this step does not require a breaking ABI change.

The native code now:
- compile cleanly as x64
- accept calls through the ABI
- capture real desktop/window/region frames into a live pipeline that prefers GPU surfaces when the source geometry fits the current slice
- stream those frames directly into a playable `.mp4` file with H.264 video while recording
- keep a `.srrec` session folder for `manifest.json` and telemetry instead of per-frame BMP artifacts
- provide callback-driven status updates
- report real capture backend, encode backend, effective FPS, dropped frames, queue depth, and capture/queue/convert/encode latency through the session manifest so managed contracts can stay honest even when the machine cannot hit the requested FPS
- report requested capture backend, explicit fallback reason/HRESULT when `WGC` degrades to `DXGI`, explicit `WGC` failure reason/HRESULT with a null fallback target when `Window` capture cannot continue without fallback, and `WGC` first-frame startup latency through the session manifest so managed contracts can explain backend selection honestly
- keep legacy screenshot exports present only for ABI compatibility; they are unsupported by product code and return a failure result

For precision source selection, `sr_capture_source.region` continues to mean:
- physical pixels in Windows virtual-desktop coordinates
- clipping against the virtual desktop before capture begins
- support for negative origins when monitors live left/up from the primary display
- encoder output sizing that may be scaled and even-normalized independently from the requested capture bounds

Within ABI `2`, the same POD field now also carries normalized physical bounds hints for display/window sources when `Infrastructure` has those bounds available. This keeps the struct stable while allowing the native engine to preserve selected monitor/window geometry more accurately than a kind-only payload.

The export set and coarse lifecycle calls did not need a breaking change for this step; the live MP4/H.264 encoder still sits behind the same managed/native boundary. The engine resolves backend choice internally:
- `Windows.Graphics.Capture` first when the source resolves to a capturable window or a single display-backed region
- `DXGI Desktop Duplication` next for single-output desktop capture, with explicit fallback reporting instead of silent backend swaps
- `GDI` only as the compatibility fallback, including the current spanning-region limitation

For this slice, `DXGI` preserves an `updates-only` cadence policy. The engine does not synthesize duplicate frames to chase the target FPS when the desktop is static, so achieved FPS under `DXGI` can legitimately be lower than the requested frame rate.

It still does not provide audio capture or preview. Screenshot capture is explicitly out of product scope.

## ABI design rules
- Keep structs POD-friendly and versionable.
- Prefer explicit fields over hidden allocation contracts.
- Keep ownership rules obvious.
- Avoid leaking Windows Runtime or COM-specific shapes across the ABI.
- Add new functionality by extension, not by breaking existing signatures.
