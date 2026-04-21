# Architecture

## Target solution shape
The repository should converge on this structure:

```text
SimpleRecorder.sln
Directory.Build.props
Directory.Packages.props
README.md
build/
  setup-dev-env.ps1
  restore.ps1
Audio/
  SimpleRecorderClose.wav
  SimpleRecorderStart.wav
src/
  SimpleRecorder.App/
  SimpleRecorder.Presentation/
  SimpleRecorder.Contracts/
  SimpleRecorder.Infrastructure/
  SimpleRecorder.Engine.Native/
```

## Module responsibilities
### SimpleRecorder.App
- Packaged WinUI 3 entry point.
- App lifetime, window creation, composition root, bootstrap.
- No business logic or low-level interop details.

### SimpleRecorder.Presentation
- HUD views and settings UI.
- ViewModels, commands, reducer/state.
- Theme resources, styles, motion tokens.
- No direct Win32, WGC, D3D11, MF, or WASAPI calls.

### SimpleRecorder.Contracts
- Canonical enums, DTOs, and interfaces.
- The public model layer shared across app, presentation, and infrastructure.
- No WinUI references and no Win32/native details.

### SimpleRecorder.Infrastructure
- JSON settings persistence in LocalState.
- Tray integration via Win32 shell APIs.
- Device discovery and C# adapter to the native DLL.
- Display/window discovery plus precision region selection plumbing that resolves physical virtual-desktop coordinates into canonical contracts.
- Mapping from canonical contracts to native POD structs.
- No screenshot feature surface; recording is the only product capture mode.

### SimpleRecorder.Engine.Native
- Native x64 DLL.
- Stable exported C ABI.
- Internal place for WGC, D3D11, Media Foundation, and WASAPI as the roadmap advances.
- The current slice owns real frame capture plus a live MP4/H.264 pipeline with bounded capture/encode queues and session telemetry; audio and preview remain future work behind the same ABI.
- Internally, the recording core should stay split along these seams even when implemented in the same native project:
  - `CaptureBackend`: source-facing WGC primary path, DXGI fallback, GDI compatibility fallback.
  - `FrameGraph`: D3D11 crop/scale/color-convert work over fixed resources and bounded queues.
  - `EncoderBackend`: H.264 writer-facing path that prefers D3D11/NV12-friendly Media Foundation input.
  - `Writer`: live `.mp4` output plus `.srrec` manifest/telemetry persistence.
  - `Telemetry + CapabilityProbe`: backend, output, drop, queue, and latency reporting that remains honest when falling back.

## Canonical project rules
- `Contracts` is the only shared model source of truth.
- Mapping to `sr_*` native structs belongs in `Infrastructure.NativeStructMapper`.
- Only `Infrastructure` talks to the native DLL from managed code.
- `App` wires services together; it should not absorb infrastructure logic.

## Future-facing defaults already decided
- Target app type: packaged WinUI 3 desktop app.
- Platform focus: x64 first.
- Save path default: `Videos\SimpleRecorder`.
- Preview remains off by default in early phases.
- Region capture remains a UX crop/select layer over the same WGC-based pipeline.
- Screenshot capture is not a product mode.

## Precision selection rules
- `ScreenRegion` represents physical pixels in the Windows virtual desktop coordinate space.
- Negative `X`/`Y` values are valid for monitors that sit left/up from the primary display.
- `Presentation` consumes canonical `CaptureSourceDescriptor` and `ScreenRegion` values only; monitor/window discovery and coordinate normalization stay below it.
- Encoder-facing output dimensions may be scaled and even-normalized without changing the selected capture bounds; the UI should report both the selected bounds and the effective output size honestly.
- The native `sr_capture_source.region` field remains physical virtual-desktop coordinates; under the current slice it may also carry normalized bounds hints for display/window sources so the engine can preserve source selection fidelity without extending the ABI.
