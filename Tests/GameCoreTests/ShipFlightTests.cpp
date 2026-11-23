#include "Collision.h"
#include "SceneDefinition.h"
#include "ShipArchetype.h"
#include "ShipFlight.h"
#include "TestSupport.h"

#include "CppUnitTest.h"

#include <DirectXMath.h>

#include <cmath>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameCoreTests
{

namespace
{

constexpr std::uint32_t TICKS_PER_SECOND = 60;
constexpr float RADIUS = 20.0f;

/// A wall 200 m square and 4 m thick, its near face at z = 1,000.
[[nodiscard]] Outpost::CollisionBox Wall()
{
  Outpost::CollisionBox wall;
  wall.object = 9;
  wall.center = Outpost::UniversePosition{0.0, 0.0, 1002.0};
  wall.halfExtentsMetres = DirectX::XMFLOAT3{100.0f, 100.0f, 2.0f};
  return wall;
}

[[nodiscard]] Outpost::ShipBody Fighter()
{
  Outpost::ShipContent content;
  std::string error;
  Assert::IsTrue(Outpost::LoadShipContent(AssetRoot() / "Content" / "Ships.txt", content, error),
                 std::wstring(error.begin(), error.end()).c_str());
  Outpost::ShipBody body;
  body.object = 1;
  body.spec = Outpost::FlightSpecFor(content, *content.FindShip("EscortLancer"));
  body.radiusMetres = RADIUS;
  return body;
}

/// A ship drifting at the wall with its engine killed, so that only hull drag slows it.
[[nodiscard]] Outpost::ShipCommand Drift()
{
  Outpost::ShipCommand command;
  command.wishes.engineOn = false;
  return command;
}

} // namespace

TEST_CLASS(ShipFlightTests)
{
public:
  TEST_METHOD(ASweptSphereStopsAtTheFaceFromAnySpeed)
  {
    // Cruise speed and the global limit cover 5 and 17 m in a tick, more than the wall is thick: an
    // unswept step would pass through.
    const std::vector<Outpost::CollisionBox> bodies{Wall()};
    for (const float speed : {80.0f, 200.0f, Outpost::CRUISE_SPEED_METRES_PER_SECOND, Outpost::MAX_SPEED_METRES_PER_SECOND})
    {
      Outpost::FlightState state = Outpost::RestingFlightState(Fighter().spec, Outpost::Pose{});
      state.position.z = 900.0;
      state.velocityMetresPerSecond = DirectX::XMFLOAT3{0.0f, 0.0f, speed};
      bool contact = false;
      for (int tick = 0; tick < 2 * static_cast<int>(TICKS_PER_SECOND); ++tick)
      {
        const Outpost::ShipTickResult result = Outpost::StepShip(Fighter(), Drift(), bodies, TICKS_PER_SECOND, state);
        contact = contact || result.contact;
        Assert::IsTrue(state.position.z <= 1000.0 - RADIUS + 1.0e-3, (L"stopped outside at " + std::to_wstring(speed)).c_str());
      }
      Assert::IsTrue(contact, (L"met the wall at " + std::to_wstring(speed)).c_str());
      Assert::IsTrue(state.velocityMetresPerSecond.z < 0.0f, L"and bounced back");
    }
  }

  TEST_METHOD(ABounceKeepsNineTenthsAndFrictionTakesALittle)
  {
    DirectX::XMFLOAT3 velocity{0.0f, 0.0f, -100.0f};
    Outpost::Bounce(velocity, DirectX::XMFLOAT3{0.0f, 0.0f, 1.0f});
    Assert::AreEqual(90.0f, velocity.z, 1.0e-4f, L"elasticity 0.9");

    // A glancing blow: 10 m/s into the face, 50 along it. The normal impulse is 19 m/s, so friction
    // takes 1.9 m/s off the 50.
    DirectX::XMFLOAT3 glancing{50.0f, 0.0f, -10.0f};
    Outpost::Bounce(glancing, DirectX::XMFLOAT3{0.0f, 0.0f, 1.0f});
    Assert::AreEqual(9.0f, glancing.z, 1.0e-4f);
    Assert::AreEqual(48.1f, glancing.x, 1.0e-4f);

    DirectX::XMFLOAT3 leaving{0.0f, 0.0f, 5.0f};
    Outpost::Bounce(leaving, DirectX::XMFLOAT3{0.0f, 0.0f, 1.0f});
    Assert::AreEqual(5.0f, leaving.z, L"a ship already leaving the face is not turned");
  }

  TEST_METHOD(CollisionDamageFollowsTheMassRatio)
  {
    // At cruise speed a starter hull takes 150 (ch. 02 §3.11).
    Assert::AreEqual(150.0f, Outpost::CollisionDamage(300.0f, 0.0f, 100.0f), L"an immovable body hurts at the full rate");
    Assert::AreEqual(0.0f, Outpost::CollisionDamage(300.0f, 100.0f, 0.0f));
    Assert::AreEqual(15.0f, Outpost::CollisionDamage(300.0f, 100.0f, 1000.0f), L"the light rammer barely hurts the heavy");
    Assert::AreEqual(150.0f, Outpost::CollisionDamage(300.0f, 1000.0f, 100.0f), L"capped at the full rate");
  }

  TEST_METHOD(ASphereStartingInsideABoxMayLeaveIt)
  {
    const Outpost::CollisionBox wall = Wall();
    Outpost::SweepHit hit;
    Assert::IsFalse(
      Outpost::SweepSphere(Outpost::UniversePosition{0.0, 0.0, 1002.0}, DirectX::XMFLOAT3{0.0f, 0.0f, -50.0f}, RADIUS, wall, hit));
    Assert::IsTrue(
      Outpost::SweepSphere(Outpost::UniversePosition{0.0, 0.0, 900.0}, DirectX::XMFLOAT3{0.0f, 0.0f, 200.0f}, RADIUS, wall, hit));
    Assert::AreEqual((1000.0f - RADIUS - 900.0f) / 200.0f, hit.fraction, 1.0e-5f);
    Assert::AreEqual(-1.0f, hit.normal.z, L"the near face faces the ship");
  }

  TEST_METHOD(ARotatedBoxIsMetOnItsOwnAxes)
  {
    // The wall turned a quarter about Y: now thin along X, its near face at x = 1,000.
    Outpost::CollisionBox wall = Wall();
    wall.center = Outpost::UniversePosition{1002.0, 0.0, 0.0};
    DirectX::XMStoreFloat4(&wall.orientation, DirectX::XMQuaternionRotationRollPitchYaw(0.0f, DirectX::XM_PIDIV2, 0.0f));
    Outpost::SweepHit hit;
    Assert::IsTrue(
      Outpost::SweepSphere(Outpost::UniversePosition{900.0, 0.0, 0.0}, DirectX::XMFLOAT3{200.0f, 0.0f, 0.0f}, RADIUS, wall, hit));
    Assert::AreEqual((1000.0f - RADIUS - 900.0f) / 200.0f, hit.fraction, 1.0e-4f);
    Assert::AreEqual(-1.0f, hit.normal.x, 1.0e-5f);
  }

  TEST_METHOD(CruiseIsRefusedOnlyWhileAnImmovableBodyIsAhead)
  {
    Outpost::ShipCommand cruise;
    cruise.wishes.throttle = 1.0f;
    cruise.wishes.cruise = true;
    Outpost::FlightState state = Outpost::RestingFlightState(Fighter().spec, Outpost::Pose{});
    state.position.z = 1000.0 - RADIUS - Outpost::CRUISE_CLEARANCE_METRES - 1.0;
    std::vector<Outpost::CollisionBox> bodies{Wall()};
    Outpost::ShipTickResult result = Outpost::StepShip(Fighter(), cruise, bodies, TICKS_PER_SECOND, state);
    Assert::IsFalse(result.obstructed, L"just beyond the clearance");
    Assert::IsTrue(result.controls.cruise);

    state.position.z += 10.0;
    result = Outpost::StepShip(Fighter(), cruise, bodies, TICKS_PER_SECOND, state);
    Assert::IsTrue(result.obstructed, L"within it");
    Assert::IsFalse(result.controls.cruise, L"the arbiter drops the wish");

    bodies.front().massKilograms = 100.0f; // a ship ahead: the prediction could not see it
    result = Outpost::StepShip(Fighter(), cruise, bodies, TICKS_PER_SECOND, state);
    Assert::IsFalse(result.obstructed, L"only immovable bodies obstruct");
  }

  TEST_METHOD(StaticBodiesAreEveryObjectThatDoesNotFlyAndIsNotABlueprint)
  {
    Outpost::SceneDefinition scene;
    std::string error;
    Assert::IsTrue(Outpost::LoadSceneContent(AssetRoot(), scene, error), std::wstring(error.begin(), error.end()).c_str());
    Outpost::ShipContent ships;
    Assert::IsTrue(Outpost::LoadShipContent(AssetRoot() / "Content" / "Ships.txt", ships, error));
    std::vector<Outpost::UniverseObject> objects;
    std::size_t flying = 0;
    std::size_t blueprints = 0;
    for (std::size_t i = 0; i < scene.instances.size(); ++i)
    {
      objects.push_back(Outpost::ObjectFromInstance(scene, i, static_cast<Outpost::ObjectId>(i + 1)));
      flying += Outpost::FlyingArchetype(ships, objects.back()) != nullptr ? 1 : 0;
      blueprints += scene.instances[i].style == Outpost::InstanceStyle::Blueprint ? 1 : 0;
    }
    Assert::AreEqual(std::size_t{3}, flying, L"the two escorts and the raider fly; the frigate and the tug have no archetype yet");
    const std::vector<Outpost::CollisionBox> bodies = Outpost::StaticBodies(scene, ships, objects);
    Assert::AreEqual(objects.size() - flying - blueprints, bodies.size());
    for (const Outpost::CollisionBox& body : bodies)
    {
      Assert::IsTrue(body.halfExtentsMetres.x > 0.0f && body.halfExtentsMetres.y > 0.0f && body.halfExtentsMetres.z > 0.0f);
      Assert::AreEqual(0.0f, body.massKilograms);
    }
  }
};

} // namespace GameCoreTests
