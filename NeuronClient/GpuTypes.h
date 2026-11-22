#pragma once

#include <DirectXMath.h>

#include <cstdint>

namespace Neuron
{

// C++ mirrors of the HLSL constant and structured-buffer layouts in Shaders/Common.hlsli. Matrices
// are row-major on both sides (HLSL declares them row_major) and multiply row vectors, the same
// convention as DirectXMath, so nothing is transposed on upload. Every member is placed so that
// the HLSL 16-byte packing rules lay it out identically; the static_asserts pin the sizes.

inline constexpr std::uint32_t MAX_POINT_LIGHTS = 16;

struct GpuPointLight
{
  DirectX::XMFLOAT3 position;
  float radius;
  DirectX::XMFLOAT3 color;
  float intensity;
};

struct FrameConstants
{
  DirectX::XMFLOAT4X4 viewProjection;
  DirectX::XMFLOAT4X4 inverseViewProjection;
  DirectX::XMFLOAT4X4 shadowViewProjection;
  DirectX::XMFLOAT3 cameraPosition;
  float time;
  DirectX::XMFLOAT2 viewportOrigin;
  DirectX::XMFLOAT2 viewportSize;
  DirectX::XMFLOAT3 keyLightDirection; // toward the light
  float shadowEnabled;
  DirectX::XMFLOAT3 keyLightColor;
  float pointLightCount;
  DirectX::XMFLOAT3 ambientSky;
  float exposure;
  DirectX::XMFLOAT3 ambientGround;
  float shadowTexelSize;
  DirectX::XMFLOAT3 cameraForward;
  float isOrthographic;
  float nearZ;
  float renderTargetHeight;
  float padding0;
  float padding1;
  GpuPointLight pointLights[MAX_POINT_LIGHTS];
};
static_assert(sizeof(FrameConstants) % 16 == 0);

struct VoxelDrawConstants
{
  DirectX::XMFLOAT4X4 voxToWorld;
  DirectX::XMFLOAT4X4 voxToClip;
  DirectX::XMFLOAT4X4 clipToVox;
  DirectX::XMFLOAT3 cameraForwardVox;  // unit length in VOX space
  float voxelWorldSize;                // world units per VOX unit, after instance scale
  DirectX::XMFLOAT3 cameraPositionVox; // perspective eye in VOX space (unused when orthographic)
  float emissionScale;                 // 0 in the shadow pass and for flat thumbnails
  DirectX::XMFLOAT4 tint;              // rgb multiplies the lit colour
  DirectX::XMFLOAT3 rimColor;          // selection rim light
  float rimStrength;
};
static_assert(sizeof(VoxelDrawConstants) % 16 == 0);

/// One surface voxel as the GPU reads it: x | y << 8 | z << 16 | colour << 24, then the exposed
/// face mask in the low byte and two bits of occlusion per face above it.
struct GpuVoxel
{
  std::uint32_t positionAndColor;
  std::uint32_t facesAndOcclusion;
};
static_assert(sizeof(GpuVoxel) == 8);

/// The per-palette-index material table: linear albedo and emission.
struct GpuMaterial
{
  DirectX::XMFLOAT4 albedo;   // rgb linear color, w = coverage
  DirectX::XMFLOAT4 emission; // rgb radiance, w = strobe period in seconds (0 = steady)
};
static_assert(sizeof(GpuMaterial) == 32);

/// A world-space line segment with a width in pixels.
struct GpuLineSegment
{
  DirectX::XMFLOAT3 a;
  float widthPixels;
  DirectX::XMFLOAT3 b;
  float padding;
  DirectX::XMFLOAT4 color;
};
static_assert(sizeof(GpuLineSegment) == 48);

/// An additive billboard: a soft glow when a == b, otherwise a beam from a to b.
struct GpuEffect
{
  DirectX::XMFLOAT3 a;
  float widthA;
  DirectX::XMFLOAT3 b;
  float widthB;
  DirectX::XMFLOAT4 colorA;
  DirectX::XMFLOAT4 colorB;
  std::uint32_t flags;
  float animation[3]; // x = strobe period in seconds (0 = steady), y/z reserved
};
static_assert(sizeof(GpuEffect) == 80);

inline constexpr std::uint32_t EFFECT_WIDTH_IN_PIXELS = 1u;
inline constexpr std::uint32_t EFFECT_GLOW = 2u;

/// How a HUD vertex samples (ADR-023): the low byte is the HUD texture slot (0 is the font atlas),
/// and HUD_SAMPLE_COLOR asks for the texel's RGBA rather than its first channel as coverage.
inline constexpr std::uint32_t HUD_SLOT_MASK = 0xffu;
inline constexpr std::uint32_t HUD_SAMPLE_COLOR = 0x100u;

/// A HUD vertex in pixels. Colour is sRGB-encoded RGBA8, alpha straight. Coverage sampling (text,
/// solid fills, mask sprites) multiplies the colour's alpha by the texel's red channel; colour
/// sampling (colour sprites, sRGB-decoded by the texture) multiplies the colour by the texel.
struct GpuHudVertex
{
  DirectX::XMFLOAT2 position;
  DirectX::XMFLOAT2 uv;
  std::uint32_t color;
  std::uint32_t sampling; // slot | HUD_SAMPLE_COLOR
};
static_assert(sizeof(GpuHudVertex) == 24);

} // namespace Neuron
