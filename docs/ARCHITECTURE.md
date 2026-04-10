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
- Mapping from canonical contracts to native POD structs.

### SimpleRecorder.Engine.Native
- Native x64 DLL.
- Stable exported C ABI.
- Internal place for WGC, D3D11, Media Foundation, and WASAPI as the roadmap advances.
- In Phase 1, only stub implementations that compile and emit realistic state changes.

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
