# TODO — Luma port (deferred)

Tracked here because GitHub Issues are disabled on this repo.

## 1. Phase 3e: SMAA draw passes

**Status:** Phase 3 stubs SMAA. Trigger detection is wired (MLAA-mask hash
`0x6B0219A1`, SupportedAA hashes), `Luma_SMAA_Linearize` CS compiles, but the
3 draw passes (edge detection → blending weight calculation → neighborhood
blending) are not implemented. `RunSMAA` returns false.

**Task:** Port Luma's `DrawSMAA` (main.cpp ~line 648 callsite + the helper).
Needs the SMAA area/search textures (Luma ships them as `SMAA_AreaTex.h` /
`SMAA_SearchTex.h` byte arrays in `Source/Core/texture_data/`). Requires 6
SMAA VS+PS shaders compiled from `Luma_SMAA_impl.hlsl`.

**Files:** `src/LumaPasses.cpp` (`RunSMAA`), `tools/compile_shaders.ps1`
(compile the 6 SMAA shader variants), `shaders/dxhr/Luma_SMAA_impl.hlsl`.

## 2. Phase 4: overlap dedup with DXHRVR stereo corrections — DONE

**Status:** Implemented. `ShaderSwap::TrySubstitutePS` now accepts a
`dxhrvrCorrected` flag (computed in `RenderStateHook` via
`EffectShader::UsesCentreViewMatrix`) and skips substitution when
`DedupWithDXHRVR=1` (default). Also auto-skips SSAO generation hashes when
XeGTAO is enabled. Manual skip-list via `AddSkipHash()`. Skips logged to
`DeusExHRVR-shaderswap.log`.

**Needs in-headset verification:** The auto-skip covers projected light/shadow
(`UsesCentreViewMatrix`) + SSAO gen. Other shaders (sky, additive transparent
geom) may also need skipping — test with `DedupWithDXHRVR=0` vs `=1` and
add to `skipHashes` if stereo breaks.

## Done

- ~~Wire LumaSettings cbuffer (LightingColor etc.)~~ — Done. `src/LumaSettingsCB.h`
  mirrors the HLSL cbuffer layout (global settings + DXHR GameSettings at b13).
  Created as a dynamic cbuffer, updated via Map(DISCARD), bound before Phase 2
  substitution (PS) and Phase 3 injected passes (CS for XeGTAO, PS for
  ModulateLighting). Ini-configurable intensities with Luma's defaults.
