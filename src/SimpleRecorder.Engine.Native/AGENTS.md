# Native engine module guide

## Own this layer
This project owns the native x64 DLL and exported C ABI.
It is the only place where future WGC, D3D11, Media Foundation, and WASAPI implementation details should live.

## Phase 1 rule
In Phase 1, prefer compile-safe stubs that honor the ABI and simulate realistic state changes. Do not half-implement real capture or encode paths unless the task explicitly advances the roadmap.

## ABI rules
- Keep exports coarse-grained and versionable.
- Keep structs POD-friendly.
- Make memory ownership explicit.
- Avoid leaking C++/WinRT or COM details through the C ABI.

## Collaboration rule
Assume C# only talks to this project through the ABI described in `docs/native-abi.md`.
