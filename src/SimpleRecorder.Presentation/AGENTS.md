# Presentation module guide

## Own this layer
This project owns:
- HUD and settings XAML
- ViewModels and commands
- reducer/state transitions
- theme resources, styles, and motion tokens

## Hard boundaries
Never call Win32, WGC, D3D11, Media Foundation, WASAPI, or native exports from this layer.
Never define duplicate DTOs that belong in `SimpleRecorder.Contracts`.

## Behavioral rules
- Keep UI state driven by service callbacks and reducer actions.
- Separate session state from transient feedback state.
- Respect reduced-motion settings.
- Prefer compact, stable HUD interactions over flashy motion.
