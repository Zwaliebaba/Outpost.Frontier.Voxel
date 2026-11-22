#pragma once

#include "WorldObject.h"

#include <DirectXMath.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace Outpost
{

struct SceneDefinition;

// Docking points (plan task 2.2, blueprint ch. 04 §2). A docking point is a record on a model that
// names three of its `dock`-class sockets: the entry, where the approach begins outside the station;
// the approach point; and the pad, where the ship ends up. A docked ship sits with its pivot on the
// pad, its nose along the pad socket's outward normal and its top along the socket's up.

/// The kind of docking point, which is also the docking class a hull carries: a point accepts the
/// ship whose class is its kind (ch. 04 §2's table, without the jump and airlock kinds).
enum class DockKind : std::uint8_t
{
  Berth,  // an interior bay of a station
  Medium, // a medium mooring: the ship stays outside
  Large   // a large mooring
};

[[nodiscard]] std::string_view DockKindName(DockKind _kind) noexcept;
[[nodiscard]] std::optional<DockKind> ParseDockKind(std::string_view _text) noexcept;

struct DockingPoint
{
  std::string name;
  DockKind kind = DockKind::Berth;
  std::string entry; // socket names on the same model
  std::string approach;
  std::string pad;
  float radiusMetres = 4.0f; // the ship is docked once its pivot is this close to the pad (ch. 04 §3.4)
};

/// A docking point in the world: where its three sockets are, and how a docked ship is turned.
struct DockingPath
{
  WorldPosition entry;
  WorldPosition approach;
  WorldPosition pad;
  DirectX::XMFLOAT4 orientation{0.0f, 0.0f, 0.0f, 1.0f}; // a docked ship's, ship frame to world
  float radiusMetres = 4.0f;
};

/// The docking point's path for an instance of its model. The model's size must be set and the
/// sockets must exist (ValidateScene).
[[nodiscard]] DockingPath PathFor(const SceneDefinition& _scene, std::size_t _instanceIndex, const DockingPoint& _point) noexcept;

} // namespace Outpost
