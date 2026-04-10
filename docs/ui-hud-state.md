# HUD state and motion

## State model
Keep session state separate from transient feedback.

### Session state
Use a durable session layer such as:
- `Idle`
- `SourceSelected`
- `Countdown`
- `Recording`
- `Paused`
- `StoppingSaving`

### Feedback state
Use a separate transient feedback layer such as:
- `None`
- `ScreenshotSuccess`
- `NonBlockingError`

Do not overload one enum to represent both concepts.

## Reducer rules
- A selected or restored valid source moves the HUD from `Idle` to `SourceSelected`.
- `Record` moves to `Countdown` only when countdown is enabled.
- `Record` moves directly to `Recording` when countdown is off.
- `Pause` and `Resume` only affect the session layer.
- `Stop` moves through `StoppingSaving`, then returns to `SourceSelected`.
- Screenshot success and non-fatal errors appear as overlays or banners without replacing the session state.

## Motion rules
Favor compositor-first motion and avoid layout-heavy animations.
During recording, keep animation minimal and functional.
Respect reduced-motion settings.

## Motion tokens
Use these as the default timing budget unless a task explicitly changes the design language:
- HUD enter/exit: 180 ms
- Settings reveal: 160 ms
- Recording ↔ paused transitions: 120 ms
- Screenshot success: 140 ms in, 900 ms hold, 160 ms out

## Design guardrail
The HUD should feel compact, clear, and stable under recording load. Avoid decorative animation loops or effects that add churn without conveying state.
