#include "FileSys.h"
#include "LoopbackTransport.h"
#include "Messages.h"
#include "Observer.h"
#include "SceneDefinition.h"
#include "Universe.h"

#include <optional>
#include <string>

#include "CppUnitTest.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

constexpr std::string_view OBSERVED_SCENE = R"(camera target=0,0,0 yaw=0 pitch=20 distance=300 fov=45
model name=Box file=Models/TrussBeam.vox
instance name=A model=Box position=0,0,0 owner=player
instance name=B model=Box position=50,0,0 rotation=0,90,0
instance name=C model=Box position=0,0,80 owner=hostile
ship instance=B callsign=X-2 class=Escort hull=100
objective text="Hold the line" done=true
resource name=Ore amount=100 perminute=120
resource name=Crew amount=36 capacity=60
)";

int g_moduleAnchor = 0;

[[nodiscard]] Outpost::Universe ObservedUniverse()
{
  Outpost::SceneDefinition scene;
  std::string error;
  Assert::IsTrue(Outpost::ParseScene(OBSERVED_SCENE, scene, error), std::wstring(error.begin(), error.end()).c_str());
  Outpost::Universe universe;
  universe.Load(scene);
  return universe;
}

/// Every message waiting for the client, applied to its picture of the universe.
void Receive(Neuron::LoopbackTransport& _transport, Outpost::UniverseSnapshot& _client)
{
  while (std::optional<Neuron::Packet> packet = _transport.Receive(Neuron::Peer::Client))
  {
    const auto message = Outpost::Decode(packet->bytes);
    Assert::IsTrue(message.has_value(), L"every message the observer sends decodes");
    Outpost::ApplyMessage(*message, _client);
  }
}

void AssertSameUniverse(const Outpost::UniverseSnapshot& _server, const Outpost::UniverseSnapshot& _client)
{
  Assert::AreEqual(_server.tick, _client.tick);
  Assert::IsTrue(_server.objects == _client.objects, L"objects");
  Assert::IsTrue(_server.ships == _client.ships, L"ships");
  Assert::IsTrue(_server.objectives == _client.objectives, L"objectives");
  Assert::IsTrue(_server.resources == _client.resources, L"resources");
}

} // namespace

TEST_CLASS(ObserverTests)
{
public:
  TEST_METHOD(TheClientLearnsTheWholeUniverseFromMessages)
  {
    const Outpost::Universe universe = ObservedUniverse();
    Outpost::Observer observer;
    Neuron::LoopbackTransport transport;
    Assert::IsTrue(observer.Publish(universe, transport));
    Outpost::UniverseSnapshot client;
    Receive(transport, client);
    AssertSameUniverse(universe.State(), client);
  }

  TEST_METHOD(EveryObjectIsItsOwnMessage)
  {
    // One tick, three spawns, one ship, one objective, two resources: no message holds the universe.
    const Outpost::Universe universe = ObservedUniverse();
    Outpost::Observer observer;
    Neuron::LoopbackTransport transport;
    Assert::IsTrue(observer.Publish(universe, transport));
    Assert::AreEqual(std::size_t{8}, observer.LastMessageCount());
  }

  TEST_METHOD(NothingIsSentWhenNothingChanged)
  {
    const Outpost::Universe universe = ObservedUniverse();
    Outpost::Observer observer;
    Neuron::LoopbackTransport transport;
    Assert::IsTrue(observer.Publish(universe, transport));
    Assert::IsTrue(observer.Publish(universe, transport));
    Assert::AreEqual(std::size_t{0}, observer.LastMessageCount());
  }

  TEST_METHOD(ATickSendsTheTickAndWhatChanged)
  {
    Outpost::Universe universe = ObservedUniverse();
    Outpost::Observer observer;
    Neuron::LoopbackTransport transport;
    Outpost::UniverseSnapshot client;
    Assert::IsTrue(observer.Publish(universe, transport));
    Receive(transport, client);

    universe.Tick(); // 120 ore a minute is 2 a second: no whole ore after one tick
    Assert::IsTrue(observer.Publish(universe, transport));
    Assert::AreEqual(std::size_t{1}, observer.LastMessageCount(), L"only the tick");

    for (int i = 0; i < 29; ++i)
    {
      universe.Tick();
    }
    Assert::IsTrue(observer.Publish(universe, transport));
    Assert::AreEqual(std::size_t{2}, observer.LastMessageCount(), L"the tick and the ore that became whole");
    Receive(transport, client);
    AssertSameUniverse(universe.State(), client);
  }

  TEST_METHOD(ObjectsTakeIdentifiersInPlacementOrder)
  {
    const Outpost::Universe first = ObservedUniverse();
    const Outpost::Universe second = ObservedUniverse();
    Assert::IsTrue(first.State().objects == second.State().objects, L"the same content gives the same objects");
    const auto& objects = first.State().objects;
    Assert::AreEqual(std::size_t{3}, objects.size());
    for (std::size_t i = 0; i < objects.size(); ++i)
    {
      Assert::AreEqual(static_cast<Outpost::ObjectId>(i + 1), objects[i].id);
    }
    Assert::IsTrue(objects[0].owner == Outpost::Owner::Player);
    Assert::IsTrue(objects[1].owner == Outpost::Owner::Neutral);
    Assert::IsTrue(objects[2].owner == Outpost::Owner::Hostile);
    Assert::AreEqual(Outpost::ObjectId{2}, first.State().ships[0].object, L"the ship names its object");
  }

  TEST_METHOD(TheServerLoadsItsOwnContent)
  {
    const auto root = Neuron::FindAssetRoot(Neuron::ModuleDirectory(&g_moduleAnchor), L"Scene.txt");
    Assert::IsTrue(root.has_value(), L"Assets/Scene.txt above the test DLL");
    Outpost::Universe universe;
    std::string error;
    Assert::IsTrue(universe.LoadContent(*root, error), std::wstring(error.begin(), error.end()).c_str());
    Outpost::SceneDefinition scene;
    Assert::IsTrue(Outpost::LoadScene(*root / "Scene.txt", scene, error));
    Assert::AreEqual(scene.instances.size(), universe.State().objects.size(), L"every placed instance is an object");
    for (const Outpost::ShipState& ship : universe.State().ships)
    {
      Assert::IsTrue(ship.object != Outpost::NO_OBJECT, L"every ship is an object");
    }
  }
};

} // namespace GameLogicTests
