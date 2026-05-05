# HUD state and motion

`SimpleRecorder.pen` is the active visual source for HUD and settings work. Legacy `.fig` files are archival only.

## State model
Keep durable session state separate from transient feedback.

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

## Reducer rules
- Selecting or restoring a valid source moves to `SourceSelected`.
- Record moves to `Countdown` only when countdown is enabled.
- Record moves directly to `Recording` when countdown is off.
- Pause/resume only affect session state.
- Stop moves through `StoppingSaving`, then returns to `SourceSelected`.
- Non-fatal errors appear as feedback without replacing session state.
- Secondary action is pause/resume and is hidden outside recording/paused states.

## HUD behavior
- Prefer a compact operator panel over decorative presentation.
- Show source, recording options, and telemetry when they improve observability.
- During recording, keep motion minimal and functional.
- Respect reduced-motion settings.

## Motion tokens
- HUD enter/exit: 180 ms.
- Settings reveal: 160 ms.
- Recording/paused transition: 120 ms.
