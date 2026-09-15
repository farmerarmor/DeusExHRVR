# Third-party notices

The AMD stereo and D3D11 adapter proxy sources in `vendor` derive from [effcol/wiz3D](https://github.com/effcol/wiz3D), revision `981de7bf5414fad307754c6db03fd04970a25afb`. The upstream LGPL 2.1 license is retained as `LICENSE`, and the iZ3D notice is retained in `licenses/LICENSE-iZ3D.txt`. Original source notices remain in the files. This combined source project is distributed under LGPL 2.1; retain the corresponding modified source when distributing its binaries.

Local modifications replace the upstream stereo compositor with the DeusExHRVR transport, preserve original per-eye swapchain dimensions while providing a double-height native stereo buffer, avoid retaining discarded adapter-probe devices, and create DXGI 1.1 factories to permit keyed-mutex resources. The companion and transport sources are new code.

OpenXR SDK Source: KhronosGroup/OpenXR-SDK-Source revision `c07ad64839653712190e05dbd8cf460e1d239513`, Apache 2.0, notice in `licenses/OpenXR.txt`.

MinHook: TsudaKageyu/minhook revision `c3fcafdc10146beb5919319d0683e44e3c30d537`, BSD-style license, notice in `licenses/MinHook.txt`.

## Luma Framework — DXHR replacement shaders

The replacement HLSL shaders in `shaders/dxhr/` and the shared includes in `shaders/Includes/` are derived from [Filoppi/Luma-Framework](https://github.com/Filoppi/Luma-Framework), specifically the `Shaders/Deus Ex Human Revolution Director's Cut/` tree. They are redistributed and adapted under Luma's Custom MIT License (see below), which requires that any reuse include the names of the authors or project.

**Author / project credit:** Filippo Tarpini — Luma Framework (https://github.com/Filoppi/Luma-Framework)

The native shader-swap integration (`src/ShaderSwap.h`, `src/ShaderSwap.cpp`, `src/ShaderHash.h`) is new code written for DeusExHRVR to apply Luma's shader fixes without ReShade (ReShade's D3D11 device wrapping collides with this mod's hooks, and ReShade is stereo-blind). It re-implements ReShade's `compute_crc32` shader-hash algorithm to key into Luma's replacement table.

Luma Custom MIT License:

```
Custom MIT License

Copyright (c) 2024+ Filippo Tarpini

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

Addenda:
Any reuse of this code shall include the names of the authors or of the project.
Commercial usage is possible but only after asking permission to the authors.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

[rrika/cdcEngineDXHR](https://github.com/rrika/cdcEngineDXHR) was consulted as a reverse-engineering reference for engine class names and layouts. Camera addresses and layouts must be checked against the supported executable; the reference is not a drop-in SDK for this version.
