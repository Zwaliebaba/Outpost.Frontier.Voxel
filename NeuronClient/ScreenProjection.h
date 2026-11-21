#pragma once

#include "RayBox.h"

#include <DirectXMath.h>

#include <array>
#include <cstdint>

namespace Neuron
{

enum class ScreenVisibility : std::uint8_t
{
  OnScreen,    // some of the projected bounds overlap the viewport
  OffScreen,   // in front of the camera, but entirely outside the viewport
  BehindCamera // every corner is behind the near plane
};

struct ScreenRect
{
  float left;
  float top;
  float right;
  float bottom;
};

struct ProjectedBounds
{
  ScreenVisibility visibility = ScreenVisibility::BehindCamera;
  bool clippedByNearPlane = false;
  ScreenRect rect{0.0f, 0.0f, 0.0f, 0.0f};      // pixels, top-left origin; meaningless when BehindCamera
  DirectX::XMFLOAT2 edgeDirection{0.0f, -1.0f}; // screen-space unit vector from the viewport centre toward the object
};

[[nodiscard]] std::array<DirectX::XMFLOAT3, 8> BoxCorners(const OrientedBox& _box) noexcept;

/// Projects a box's eight corners into a pixel rectangle. Corners behind the near plane are not
/// projected — dividing by a negative w mirrors them across the screen — and instead every edge
/// that crosses the near plane contributes its crossing point, so a box the camera is inside or
/// beside still gets a correct, if unbounded, rectangle.
[[nodiscard]] ProjectedBounds ProjectBox(const std::array<DirectX::XMFLOAT3, 8>& _cornersWorld, DirectX::FXMMATRIX _viewProjection,
                                         float _viewportWidth, float _viewportHeight) noexcept;

/// The world-space ray through a pixel centre. Direction is normalized.
[[nodiscard]] Ray PickRay(float _pixelX, float _pixelY, DirectX::FXMMATRIX _inverseViewProjection, float _viewportWidth,
                          float _viewportHeight) noexcept;

} // namespace Neuron
