#pragma once

#include "UniverseObject.h"

#include <DirectXMath.h>

#include <cstdint>
#include <span>
#include <vector>

namespace Outpost
{

struct SceneDefinition;
struct ShipArchetype;
struct ShipContent;

// Minimal collisions (plan task 1.11; blueprint ch. 02 §3.11): every body is the box of its model's
// whole grid, a ship is the sphere around its own box, and a ship's motion in a tick is swept
// against the boxes so that no speed carries it through one. All bodies share one material.
inline constexpr float COLLISION_ELASTICITY = 0.9f;
inline constexpr float COLLISION_FRICTION = 0.1f;
// A contact between the same pair is recorded at most once in this long.
inline constexpr float CONTACT_REPEAT_SECONDS = 0.5f;

/// A body as collisions see it: an oriented box placed at its object's pose. The centre is double,
/// so that a body far from the origin is as precise as one near it (ADR-007).
struct CollisionBox
{
  ObjectId object = NO_OBJECT;
  UniversePosition center;
  DirectX::XMFLOAT4 orientation{0.0f, 0.0f, 0.0f, 1.0f}; // box axes to world
  DirectX::XMFLOAT3 halfExtentsMetres{};
  DirectX::XMFLOAT3 velocityMetresPerSecond{};
  float massKilograms = 0.0f; // 0: immovable, as heavy as anything that hits it
};

/// Where a sweep first touched a box: the share of the motion travelled, the outward normal of the
/// face it met, and the body.
struct SweepHit
{
  float fraction = 1.0f;
  DirectX::XMFLOAT3 normal{0.0f, 1.0f, 0.0f};
  const CollisionBox* body = nullptr;
};

/// A sphere moving by _motion from _start, against one box grown by the radius. A sphere that
/// starts inside the grown box does not hit it, so a ship placed overlapping a body can leave it.
[[nodiscard]] bool SweepSphere(const UniversePosition& _start, const DirectX::XMFLOAT3& _motion, float _radiusMetres,
                               const CollisionBox& _box, SweepHit& _outHit) noexcept;

/// The first box the sweep meets, skipping the body _self. Ties go to the earlier box.
[[nodiscard]] bool SweepSphere(const UniversePosition& _start, const DirectX::XMFLOAT3& _motion, float _radiusMetres,
                               std::span<const CollisionBox> _boxes, ObjectId _self, SweepHit& _outHit) noexcept;

/// The box around an object's model grid, scaled and placed at its pose. The model's size must be
/// set (LoadSceneContent). An object whose model is unknown gets an empty box at its position.
[[nodiscard]] CollisionBox BoxForObject(const SceneDefinition& _scene, const UniverseObject& _object) noexcept;

/// The sphere that bounds a box: what a ship sweeps as. Half the box's diagonal, so that no part of
/// the ship's model reaches through a wall it stopped at.
[[nodiscard]] float BoundingRadius(const CollisionBox& _box) noexcept;

/// The archetype an object flies with: the ship record whose model draws it, or null for an object
/// that does not fly (Ships.txt, ADR-009). Both halves use this one rule.
[[nodiscard]] const ShipArchetype* FlyingArchetype(const ShipContent& _content, const UniverseObject& _object) noexcept;

/// The immovable bodies: every object that does not fly, except a blueprint-styled one, which is
/// planned construction and not yet solid. Both halves build these from the same content and the
/// same objects, so the client's prediction meets the same walls as the server.
[[nodiscard]] std::vector<CollisionBox> StaticBodies(const SceneDefinition& _scene, const ShipContent& _content,
                                                     std::span<const UniverseObject> _objects);

/// A bounce off a face of outward normal _normal: the velocity's part into the face is reversed and
/// scaled by the elasticity; the part along it loses up to the friction times the normal impulse.
/// A velocity already leaving the face is unchanged.
void Bounce(DirectX::XMFLOAT3& _velocity, const DirectX::XMFLOAT3& _normal) noexcept;

/// The damage A deals to B in a contact (ch. 02 §3.11): half the relative speed, times A's mass over
/// B's capped at 1. A mass of 0 is an immovable body, heavier than anything.
[[nodiscard]] float CollisionDamage(float _relativeSpeedMetresPerSecond, float _massA, float _massB) noexcept;

} // namespace Outpost
