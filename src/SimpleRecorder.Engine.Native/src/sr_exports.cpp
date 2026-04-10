#include "pch.h"

// Phase 1 keeps the native surface intentionally small. The engine is scaffolded as a
// DLL boundary so we can add the real C++/WinRT, WGC, D3D11, Media Foundation, and
// WASAPI implementation in Phase 2 without changing the managed interop strategy.
