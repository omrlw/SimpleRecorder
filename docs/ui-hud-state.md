# HUD state and motion

## Visual source of truth
Use `SimpleRecorder.pen` as the active visual source of truth for the current HUD and settings surface.
Legacy `.fig` exports are archival references only and should not drive new UI decisions.

For the current validation slice, prefer a simple workbench-style operator panel over a compact/polished HUD when usability and observability conflict.

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
- `NonBlockingError`

Do not overload one enum to represent both concepts.

## Reducer rules
- A selected or restored valid source moves the HUD from `Idle` to `SourceSelected`.
- `Record` moves to `Countdown` only when countdown is enabled.
- `Record` moves directly to `Recording` when countdown is off.
- `Pause` and `Resume` only affect the session layer.
- `Stop` moves through `StoppingSaving`, then returns to `SourceSelected`.
- Non-fatal errors appear as overlays or banners without replacing the session state.
- The secondary HUD action is pause/resume only; it is hidden outside recording and paused states.
- Core source selection, start/stop/pause actions, and recording options may be shown inline when that improves operator usability.

## Motion rules
Favor compositor-first motion and avoid layout-heavy animations.
During recording, keep animation minimal and functional.
Respect reduced-motion settings.

## Motion tokens
Use these as the default timing budget unless a task explicitly changes the design language:
- HUD enter/exit: 180 ms
- Settings reveal: 160 ms
- Recording ↔ paused transitions: 120 ms

## Design guardrail
The HUD should feel compact, clear, and stable under recording load. Avoid decorative animation loops or effects that add churn without conveying state.
