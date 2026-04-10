# App module guide

## Own this layer
Keep this project focused on packaged WinUI 3 app startup and lifetime:
- `App.xaml` and app bootstrap
- window creation and floating HUD shell
- dependency composition
- app lifetime and shutdown flow

## Do not move these concerns here
Do not put tray implementation, settings persistence, native interop, or canonical models here. Consume services from `Infrastructure` and interfaces from `Contracts`.

## Practical rules
- Keep startup predictable and debuggable.
- Preserve the packaged app direction.
- Keep x64-first assumptions intact.
- When adding startup logic, prefer composition-root wiring over logic inside XAML code-behind.
