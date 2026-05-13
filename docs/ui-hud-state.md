# HUD State And Motion

`SimpleRecorder.pen` is the active visual source for HUD and settings work. Legacy `.fig` files are archival only.

## UI Direction
- Minimal recorder HUD, not a dashboard.
- Fast source selection and recording controls.
- Clear quality, resolution, countdown, and output state.
- Audio controls expose Off, Computer, Microphone, and Both. Microphone selection is enabled only when the mode includes microphone.
- No camera controls.
- No screenshot controls.

## State Model
Session states:
- `Idle`
- `SourceSelected`
- `Countdown`
- `Recording`
- `Paused`
- `StoppingSaving`

Feedback states:
- `None`
- `NonBlockingError`

## Reducer Rules
- Selecting or restoring a valid source moves to `SourceSelected`.
- Record moves to `Countdown` only when countdown is enabled.
- Record moves directly to `Recording` when countdown is off.
- Pause and resume only affect session state.
- Stop moves through `StoppingSaving`, then returns to `SourceSelected`.
- Non-fatal errors appear as feedback without replacing session state.

## Motion
- Keep motion short and functional.
- Respect reduced-motion settings.
- HUD enter/exit: 180 ms.
- Settings reveal: 160 ms.
- Recording/paused transition: 120 ms.
