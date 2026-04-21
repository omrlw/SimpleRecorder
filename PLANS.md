# Task
Reestructurar el núcleo nativo de grabación hacia un pipeline GPU-first sin romper el ABI ni la arquitectura de cinco módulos.

## Goal
Dejar `SimpleRecorder.Engine.Native` con una arquitectura interna clara para `CaptureBackend`, `FrameGraph`, `EncoderBackend`, `Writer` y `Telemetry`, donde el hot path principal capture superficies GPU, procese crop/scale/convert en GPU, entregue superficies encoder-friendly al encode path, y reporte honestamente el backend real y el rendimiento observado.

## Constraints
- Mantener intacta la separación entre `App`, `Presentation`, `Contracts`, `Infrastructure` y `Engine.Native`.
- `Infrastructure` sigue siendo la única capa gestionada que habla con el DLL nativo.
- Mantener ABI `2` salvo necesidad estrictamente justificada y documentada.
- Evitar readback a CPU y allocs por frame en el hot path principal.
- Mantener `.mp4` live y `.srrec` con manifest/telemetry.
- No agregar audio real, preview ni screenshot.
- Mantener el repo usable y compilable en cada fase relevante.

## Files likely to change
- C:\Users\omr_t\Desktop\SimpleRecorder\PLANS.md
- C:\Users\omr_t\Desktop\SimpleRecorder\docs\current-scope.md
- C:\Users\omr_t\Desktop\SimpleRecorder\docs\architecture.md
- C:\Users\omr_t\Desktop\SimpleRecorder\docs\native-abi.md
- C:\Users\omr_t\Desktop\SimpleRecorder\src\SimpleRecorder.Engine.Native\SimpleRecorder.Engine.Native.vcxproj
- C:\Users\omr_t\Desktop\SimpleRecorder\src\SimpleRecorder.Engine.Native\src\*
- C:\Users\omr_t\Desktop\SimpleRecorder\src\SimpleRecorder.Contracts\Models\RecordingSessionTelemetry.cs
- C:\Users\omr_t\Desktop\SimpleRecorder\src\SimpleRecorder.Infrastructure\Native\NativeRecordingManifestReader.cs
- C:\Users\omr_t\Desktop\SimpleRecorder\src\SimpleRecorder.Infrastructure\Native\NativeRecorderController.cs
- C:\Users\omr_t\Desktop\SimpleRecorder\src\SimpleRecorder.Presentation\ViewModels\HudViewModel.cs

## Plan
1. Auditar el pipeline actual y fijar el mapa de piezas a conservar, reemplazar y encapsular detrás del ABI existente.
2. Introducir primero las abstracciones internas del engine: contexto D3D11, tipos de sesión, `CaptureBackend`, `FrameGraph`, `EncoderBackend`, `Writer`, `Telemetry`.
3. Implementar el path GPU-first principal con `Windows.Graphics.Capture` cuando sea viable para la fuente seleccionada, dejando `DXGI Desktop Duplication` y `GDI` como fallbacks explícitos y reportables.
4. Migrar la etapa de procesamiento a recursos D3D11 prealocados y colas acotadas, con crop/scale/convert en GPU y salida `NV12`.
5. Integrar el encode H.264 live con preferencia por surfaces/D3D manager y hardware transforms; si no hay path hardware usable, caer a un fallback controlado y documentado.
6. Ampliar la telemetry/manifest y el contrato gestionado para exponer backend real, encode hardware/software, adapter, output efectivo, FPS real y drops/latencias desglosadas.
7. Actualizar docs cercanas para reflejar el backend real, los límites restantes y la estrategia de fallback.
8. Verificar desde la raíz: restore, build Debug x64, smoke run de la app y al menos una grabación real con telemetry observada.

## Verification
- `powershell -File .\build\restore.ps1`
- `& "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" .\SimpleRecorder.sln /restore /p:Configuration=Debug /p:Platform=x64`
- Abrir la app desde el build Debug x64 sin crash
- Ejecutar al menos una grabación real y revisar `manifest.json`/`.mp4`
- Confirmar que el manifest reporta backend real, output efectivo y métricas no asumidas
- Revisar que `Presentation` no introduce referencias nativas y que el ABI público no cambió sin justificación

## Notes
- Estado actual auditado: `stub_engine.cpp` concentra todo el pipeline con `GDI -> DIBSection(RGB32) -> MFCreateMemoryBuffer + memcpy -> IMFSinkWriter(H.264/MP4)`.
- Ya existe un ring fijo de slots y una salida live `.mp4`, pero el hot path depende de CPU memory y el manifest hardcodea `captureBackend: gdi-live`.
- El contrato gestionado ya consume `manifest.json` post-stop; ampliar telemetry ahí evita un cambio de ABI para esta fase.
- Riesgo principal: compatibilidad real de `WGC + D3D11/NV12 + sink writer` en el entorno local. Si alguna parte no puede quedar plenamente hardware-first, documentar el límite y mantener fallback honesto.
