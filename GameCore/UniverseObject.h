#pragma once

#include <DirectXMath.h>

#include <cstdint>
#include <string>

namespace Outpost
{

/// A runtime handle for one object in one running universe (ADR-009). The server assigns it and never
/// reuses it while the universe lives; it is not content, and nothing persists it.
using ObjectId = std::uint32_t;
inline constexpr ObjectId NO_OBJECT = 0;

/// Whose an object is: the MVP's stand-in for factions (plan section 11).
enum class Owner : std::uint8_t
{
  Neutral,
  Player,
  Hostile
};

/// A universe position in double precision (ADR-007), so that a system's rim is as precise as its
/// centre. Everything smaller than a system — velocities, offsets inside a model — stays float.
struct UniversePosition
{
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;

  friend bool operator==(const UniversePosition&, const UniversePosition&) = default;
};

/// Where an object is and which way it faces.
struct Pose
{
  UniversePosition position;
  DirectX::XMFLOAT4 orientation{0.0f, 0.0f, 0.0f, 1.0f}; // unit quaternion, model space to world

  friend bool operator==(const Pose& _a, const Pose& _b) noexcept
  {
    return _a.position == _b.position && _a.orientation.x == _b.orientation.x && _a.orientation.y == _b.orientation.y &&
           _a.orientation.z == _b.orientation.z && _a.orientation.w == _b.orientation.w;
  }
};

/// One object of the universe, as the server holds it and as the client learns of it.
struct UniverseObject
{
  ObjectId id = NO_OBJECT;
  std::string name;  // its placement name in Scene.txt (content, ADR-009)
  std::string model; // the model that draws it (content)
  Owner owner = Owner::Neutral;
  float scale = 1.0f;
  Pose pose;
  std::uint64_t poseTick = 0; // the server tick the pose is from

  friend bool operator==(const UniverseObject&, const UniverseObject&) = default;
};

/// Model space to world for an object: its scale, then its orientation, then its position. The
/// position is converted to float here, so this is for presentation near the origin; slice 1
/// builds transforms relative to the camera instead (ADR-007).
[[nodiscard]] DirectX::XMMATRIX ObjectToWorld(const UniverseObject& _object) noexcept;

} // namespace Outpost
