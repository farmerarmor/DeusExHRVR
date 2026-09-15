#include "Includes/Common.hlsl"

cbuffer DrawableBuffer : register(b1)
{
  float4 FogColor : packoffset(c0);
  float4 DebugColor : packoffset(c1);
  float MaterialOpacity : packoffset(c2);
  float AlphaThreshold : packoffset(c3);
}

cbuffer SceneBuffer : register(b2)
{
  row_major float4x4 View : packoffset(c0);
  row_major float4x4 ScreenMatrix : packoffset(c4);
  float2 DepthExportScale : packoffset(c8);
  float2 FogScaleOffset : packoffset(c9);
  float3 CameraPosition : packoffset(c10);
  float3 CameraDirection : packoffset(c11);
  float3 DepthFactors : packoffset(c12);
  float2 ShadowDepthBias : packoffset(c13);
  float4 SubframeViewport : packoffset(c14);
  row_major float3x4 DepthToWorld : packoffset(c15);
  float4 DepthToView : packoffset(c18);
  float4 OneOverDepthToView : packoffset(c19);
  float4 DepthToW : packoffset(c20);
  float4 ClipPlane : packoffset(c21);
  float2 ViewportDepthScaleOffset : packoffset(c22);
  float2 ColorDOFDepthScaleOffset : packoffset(c23);
  float2 TimeVector : packoffset(c24);
  float3 HeightFogParams : packoffset(c25);
  float3 GlobalAmbient : packoffset(c26);
  float4 GlobalParams[16] : packoffset(c27);
  float DX3_SSAOScale : packoffset(c43);
  float4 ScreenExtents : packoffset(c44);
  float2 ScreenResolution : packoffset(c45);
  float4 PSSMToMap1Lin : packoffset(c46);
  float4 PSSMToMap1Const : packoffset(c47);
  float4 PSSMToMap2Lin : packoffset(c48);
  float4 PSSMToMap2Const : packoffset(c49);
  float4 PSSMToMap3Lin : packoffset(c50);
  float4 PSSMToMap3Const : packoffset(c51);
  float4 PSSMDistances : packoffset(c52);
  row_major float4x4 WorldToPSSM0 : packoffset(c53);
  float StereoOffset : packoffset(c25.w);
}

cbuffer MaterialBuffer : register(b3)
{
  float4 MaterialParams[32] : packoffset(c0);
}

SamplerState p_default_Material_2A938EA45902502_DepthBufferTexture_sampler_s : register(s0);
SamplerState p_default_Material_2A938F645990781_Param_sampler_s : register(s1);
Texture2D<float4> p_default_Material_2A938EA45902502_DepthBufferTexture_texture : register(t0);
Texture2D<float4> p_default_Material_2A938F645990781_Param_texture : register(t1);

#define cmp

float3 GetWorldPos(float2 uv, float depth, float4x4 invViewProj)
{
    // Convert UV + depth to NDC space
    float4 ndc;
    ndc.xy = uv * 2.0 - 1.0;          // UV -> [-1, 1]
    ndc.z = depth * 2.0 - 1.0;        // Depth -> NDC.z (assumes linear depth)
    ndc.w = 1.0;

    // Transform to world space
    float4 worldPos = mul(invViewProj, ndc);
    worldPos /= worldPos.w;

    return worldPos.xyz;
}

float4x4 InverseMatrix(float4x4 m)
{
    float4x4 inv;

    float
        a00 = m[0][0], a01 = m[0][1], a02 = m[0][2], a03 = m[0][3],
        a10 = m[1][0], a11 = m[1][1], a12 = m[1][2], a13 = m[1][3],
        a20 = m[2][0], a21 = m[2][1], a22 = m[2][2], a23 = m[2][3],
        a30 = m[3][0], a31 = m[3][1], a32 = m[3][2], a33 = m[3][3];

    float b00 = a00 * a11 - a01 * a10;
    float b01 = a00 * a12 - a02 * a10;
    float b02 = a00 * a13 - a03 * a10;
    float b03 = a01 * a12 - a02 * a11;
    float b04 = a01 * a13 - a03 * a11;
    float b05 = a02 * a13 - a03 * a12;
    float b06 = a20 * a31 - a21 * a30;
    float b07 = a20 * a32 - a22 * a30;
    float b08 = a20 * a33 - a23 * a30;
    float b09 = a21 * a32 - a22 * a31;
    float b10 = a21 * a33 - a23 * a31;
    float b11 = a22 * a33 - a23 * a32;

    float det = b00 * b11 - b01 * b10 + b02 * b09 + b03 * b08 - b04 * b07 + b05 * b06;

    inv[0][0] = +a11 * b11 - a12 * b10 + a13 * b09;
    inv[0][1] = -a01 * b11 + a02 * b10 - a03 * b09;
    inv[0][2] = +a31 * b05 - a32 * b04 + a33 * b03;
    inv[0][3] = -a21 * b05 + a22 * b04 - a23 * b03;

    inv[1][0] = -a10 * b11 + a12 * b08 - a13 * b07;
    inv[1][1] = +a00 * b11 - a02 * b08 + a03 * b07;
    inv[1][2] = -a30 * b05 + a32 * b02 - a33 * b01;
    inv[1][3] = +a20 * b05 - a22 * b02 + a23 * b01;

    inv[2][0] = +a10 * b10 - a11 * b08 + a13 * b06;
    inv[2][1] = -a00 * b10 + a01 * b08 - a03 * b06;
    inv[2][2] = +a30 * b04 - a31 * b02 + a33 * b00;
    inv[2][3] = -a20 * b04 + a21 * b02 - a23 * b00;

    inv[3][0] = -a10 * b09 + a11 * b07 - a12 * b06;
    inv[3][1] = +a00 * b09 - a01 * b07 + a02 * b06;
    inv[3][2] = -a30 * b03 + a31 * b01 - a32 * b00;
    inv[3][3] = +a20 * b03 - a21 * b01 + a22 * b00;

    inv /= det;

    return inv;
}

float2 WorldTo2D(float3 worldPos)
{
    float2 uv;
    uv.x = dot(worldPos, float3(0.4123, 0.6572, 0.1124));
    uv.y = dot(worldPos, float3(0.7251, 0.1397, 0.8432));
    return uv;
}

float Hash(float x)
{
    return frac(sin(x) * 43758.5453);
}

// "scale" controls the “precision / frequency” of the noise
float3 WorldNoise3D(float3 worldPos, float scale = 0.001)
{
    return float3(
        Hash(worldPos.x * scale + 12.9898),
        Hash(worldPos.y * scale + 78.233),
        Hash(worldPos.z * scale + 37.719)
    );
}

float RandomFromIndex(int i)
{
    // Convert index to float, apply some constants and sine hash
    return frac(sin(float(i) * 12.9898 + 78.233) * 43758.5453);
}

float RandomFromIndex2(int i)
{
    // Convert index to float, apply some constants and sine hash
    return frac(sin(float(i) * 14.9898 + 72.233) * 48758.5453);
}

void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  // Noise array
  const float4 icb[] = { { -0.655770, -0.445200, -0.066994, 0},
                              { -0.443228, -0.264544, 0.336928, 0},
                              { -0.762338, -0.149505, 0.159947, 0},
                              { 0.266496, -0.952335, 0.058640, 0},
                              { 0.117699, -0.951451, 0.102497, 0},
                              { -0.163156, -0.494412, 0.137107, 0},
                              { -0.476872, -0.422284, 0.605497, 0},
                              { -0.519343, 0.201724, -0.120977, 0},
                              { -0.077179, -0.665571, -0.209977, 0},
                              { -0.929456, 0.135050, 0.241845, 0},
                              { 0.591962, -0.776257, 0.084786, 0},
                              { -0.785565, -0.252891, -0.289192, 0},
                              { 0.797177, 0.150118, -0.288033, 0},
                              { 0.417213, -0.817262, 0.184389, 0},
                              { -0.799127, -0.110247, 0.477771, 0},
                              { 0.633339, -0.588268, -0.340128, 0} };

  float4 r0,r1,r2,r3,r4,r5,r6,r7,r8;
  uint4 bitmask;
  r0.x = p_default_Material_2A938EA45902502_DepthBufferTexture_texture.Sample(p_default_Material_2A938EA45902502_DepthBufferTexture_sampler_s, v1.xy).x;
  float depth = r0.x;
  r0.x = r0.x * DepthToW.x + DepthToW.y; // Inverted 0-1 depth
  r0.x = max(9.99999997e-007, r0.x);
  r0.z = 1 / r0.x; // Linear depth?
  float linearDepth = r0.z;
  r1.xy = v1.xy * DepthToView.xy + DepthToView.zw;
  r0.xy = r1.xy * r0.z; // NCD depth
  //o0 = r0.y * 1; return; // Quick test
  r1.xyz = ddx_fine(r0.zxy);
  r2.xyz = ddy_fine(r0.yzx);
  r3.xyz = r2.xyz * r1.xyz;
  r1.xyz = r1.zxy * r2.yzx + -r3.xyz;
  r0.w = 1 / MaterialParams[0].y;

#if 0 // Disable noise

  r2.xyz = 0.5;

#elif 1 // Luma: World space noise. This fixes the AO noise pattern being clearly visible on screen when panning or rotating the camera

  float2 uv = v0.xy * ScreenExtents.zw + ScreenExtents.xy;
#if 0 // Old attempt
  float4x4 ViewProj = mul(ScreenMatrix, View); // World -> Clip
  float4x4 InvViewProj = InverseMatrix(ViewProj);    // Clip -> World
  float3 worldPos2 = GetWorldPos(uv, r0.z, InvViewProj);
  r2.xyz = frac(sin(dot(worldPos2.xyz, float3(12.9898,78.233,37.719))) * 43758.5453);
  r2.xyz = WorldNoise3D(worldPos.xyz, DVS5 * 0.001);
#else
  float4 screenVec;
  screenVec.z = linearDepth;
  screenVec.xy = linearDepth * uv;
  screenVec.w = 1;
  float3 worldPos;
  worldPos.x = dot(DepthToWorld._m00_m01_m02_m03, screenVec.xyzw);
  worldPos.y = dot(DepthToWorld._m10_m11_m12_m13, screenVec.xyzw);
  worldPos.z = dot(DepthToWorld._m20_m21_m22_m23, screenVec.xyzw);

  //r2.xyz = frac(sin(dot(worldPos.xyz, float3(12.9898,78.233,37.719))) * 43758.5453); // Doesn't seem to work, probably cuz the randomization pattern isn't good for the following AO pass

  // Use the world position to generate a "slowly" moving UV to be fed into the noise texture
  float2 noiseCoords = WorldTo2D(worldPos.xyz * 0.075);
  //float2 noiseCoords = WorldTo2D(worldPos.xyz * 0.075 * DVS3 * 10) / (DVS4 * 10);
  r2.xyz = p_default_Material_2A938F645990781_Param_texture.Sample(p_default_Material_2A938F645990781_Param_sampler_s, noiseCoords).xyz;
#endif

#else // Screen space noise

  // The output of this is persistently blending with a history so we could afford some temporal
  float2 noiseCoords = v0.xy / 128.0; // The noise texture is 128x128, this is already applied per pixel, so it's not stretched by resolution
#if DEVELOPMENT && 0 // Fix Aspect Ratio
  float aspectRatio = ScreenResolution.x / ScreenResolution.y;
  // if (aspectRatio >= 1.0)
  //   noiseCoords.x *= aspectRatio;
  // else
  //   noiseCoords.y /= aspectRatio;
  noiseCoords *= float2(LumaSettings.DevSetting01, LumaSettings.DevSetting02);
  //noiseCoords = MirrorUV(noiseCoords);
#endif
#if 0
  noiseCoords *= ScreenResolution.y / DevelopmentVerticalResolution; // Scale to 720p/1080p, so the noise isn't tiny at 4k, as it ends up being more noticeable
  
  noiseCoords += float2(RandomFromIndex(LumaSettings.FrameIndex), RandomFromIndex2(LumaSettings.FrameIndex));
#endif

  r2.xyz = p_default_Material_2A938F645990781_Param_texture.Sample(p_default_Material_2A938F645990781_Param_sampler_s, noiseCoords).xyz;

#endif

  r2.xyz = r2.xyz * float3(2,2,2) + float3(-1,-1,-1); // From 0|1 to -1|1
  r1.w = dot(r2.xyz, r2.xyz);
  r1.w = r1.w != 0.0 ? (1.0 / sqrt(r1.w)) : 0.0;
  r2.w = saturate(r0.z / MaterialParams[0].w);
  r3.xy = float2(0,0);
  int4 r3i = 0;
  while (true) {
    if (r3i.y >= 16) break;

    bitmask.z = ((~(-1 << 2)) << 0) & 0xffffffff;
    r3i.z = (((uint)1 << 0) & bitmask.z) | ((uint)r3i.y & ~bitmask.z);
    bitmask.w = ((~(-1 << 2)) << 0) & 0xffffffff;
    r3i.w = (((uint)2 << 0) & bitmask.w) | ((uint)r3i.y & ~bitmask.w);
    bitmask.x = ((~(-1 << 2)) << 0) & 0xffffffff;
    r3i.x = (((uint)3 << 0) & bitmask.x) | ((uint)r3i.y & ~bitmask.x);

    float3 noiseSpread = 1.0;
#if 0 // Disable noise/spread
    noiseSpread = 0;
#elif DEVELOPMENT && 0
    noiseSpread = float3(LumaSettings.DevSetting08, LumaSettings.DevSetting09, LumaSettings.DevSetting10);
#endif
    r4.yzw = r2.xyz * r1.w + icb[r3i.y+0].xyz * noiseSpread;
    r5.xyz = r2.xyz * r1.w + icb[r3i.z+0].xyz * noiseSpread;
    r6.xyz = r2.xyz * r1.w + icb[r3i.w+0].xyz * noiseSpread;
    r7.xyz = r2.xyz * r1.w + icb[r3i.x+0].xyz * noiseSpread;

    r3.z = dot(r4.yzw, r1.xyz);
    r3.z = cmp(r3.z < 0);
    r4.xyz = r3.z ? -r4.yzw : r4.yzw;
    r3.z = dot(r5.xyz, r1.xyz);
    r3.z = cmp(r3.z < 0);
    r5.xyz = r3.z ? -r5.xyz : r5.xyz;
    r3.z = dot(r6.xyz, r1.xyz);
    r3.z = cmp(r3.z < 0);
    r6.xyz = r3.z ? -r6.xyz : r6.xyz;
    r3.z = dot(r7.xyz, r1.xyz);
    r3.z = cmp(r3.z < 0);
    r7.xyz = r3.z ? -r7.xyz : r7.xyz;
    r4.xyz = r4.xyz * MaterialParams[0].x + r0.xyz;
    r5.xyz = r5.xyz * MaterialParams[0].x + r0.xyz;
    r6.xyz = r6.xyz * MaterialParams[0].x + r0.xyz;
    r7.xyw = r7.xyz * MaterialParams[0].x + r0.xyz;
    r3.zw = r4.xy / r4.z;
    r3.zw = -DepthToView.zw + r3.zw;
    r3.zw = OneOverDepthToView.xy * r3.zw;
    r4.xy = r5.xy / r5.z;
    r4.xy = -DepthToView.zw + r4.xy;
    r4.xy = OneOverDepthToView.xy * r4.xy;
    r5.xy = r6.xy / r6.z;
    r5.xy = -DepthToView.zw + r5.xy;
    r5.xy = OneOverDepthToView.xy * r5.xy;
    r6.xy = r7.xy / r7.w;
    r6.xy = -DepthToView.zw + r6.xy;
    r6.xy = OneOverDepthToView.xy * r6.xy;
    r3.z = p_default_Material_2A938EA45902502_DepthBufferTexture_texture.SampleLevel(p_default_Material_2A938EA45902502_DepthBufferTexture_sampler_s, r3.zw, 0).x;
    r3.z = r3.z * DepthToW.x + DepthToW.y;
    r3.z = max(9.99999997e-007, r3.z);
    r8.x = 1 / r3.z;
    r3.z = p_default_Material_2A938EA45902502_DepthBufferTexture_texture.SampleLevel(p_default_Material_2A938EA45902502_DepthBufferTexture_sampler_s, r4.xy, 0).x;
    r3.z = r3.z * DepthToW.x + DepthToW.y;
    r3.z = max(9.99999997e-007, r3.z);
    r8.y = 1 / r3.z;
    r3.z = p_default_Material_2A938EA45902502_DepthBufferTexture_texture.SampleLevel(p_default_Material_2A938EA45902502_DepthBufferTexture_sampler_s, r5.xy, 0).x;
    r3.z = r3.z * DepthToW.x + DepthToW.y;
    r3.z = max(9.99999997e-007, r3.z);
    r8.z = 1 / r3.z;
    r3.z = p_default_Material_2A938EA45902502_DepthBufferTexture_texture.SampleLevel(p_default_Material_2A938EA45902502_DepthBufferTexture_sampler_s, r6.xy, 0).x;
    r3.z = r3.z * DepthToW.x + DepthToW.y;
    r3.z = max(9.99999997e-007, r3.z);
    r8.w = 1 / r3.z;
    r7.x = r4.z;
    r7.y = r5.z;
    r7.z = r6.z;
    r4.xyzw = r7.xyzw + -r8.xyzw;
    r4.xyzw = -MaterialParams[0].z * r2.w + r4.xyzw;
    r4.xyzw = r4.xyzw * r0.w;
    r5.xyzw = cmp(r4.xyzw >= float4(0,0,0,0));
    r4.xyzw = saturate(r4.xyzw);
    r4.xyzw = r4.xyzw * r4.xyzw;
    r4.xyzw = r4.xyzw * r4.xyzw;
    r4.xyzw = r4.xyzw * r4.xyzw;
    r4.xyzw = r5.xyzw ? r4.xyzw : float4(1,1,1,1);
    r3.z = dot(float4(1,1,1,1), r4.xyzw);
    r3.x = r3.z + r3.x;
    r3i.y += 4;
  }
  o0.xyz = r3.x * 0.0625; // All channels are equal
  o0.w = MaterialOpacity; // This doesn't seem to ever be used?
}