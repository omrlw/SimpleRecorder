# Native engine module guide

## Own this layer
This project owns the native x64 DLL and exported C ABI.
It is the only place where future WGC, D3D11, Media Foundation, and WASAPI implementation details should live.

## Current slice rule
Preserve the existing ABI and real video capture/export path. Extend audio, preview, and other low-level recording work deliberately; do not regress the engine back to stub-only behavior or introduce half-integrated subsystems.

Screenshot capture is not a product feature. Keep legacy screenshot exports only for ABI compatibility.

## Source layout
`src/engine.cpp` owns the ABI/export translation unit. Implementation details are partitioned under `src/engine/*.inl` and included by `engine.cpp` inside the anonymous namespace. Add new native recording work to the closest existing partition unless the change intentionally introduces a separately compiled module with explicit linkage boundaries.

## ABI rules
- Keep exports coarse-grained and versionable.
- Keep structs POD-friendly.
- Make memory ownership explicit.
- Avoid leaking C++/WinRT or COM details through the C ABI.

## Collaboration rule
Assume C# only talks to this project through the ABI described in `docs/native-abi.md`.
