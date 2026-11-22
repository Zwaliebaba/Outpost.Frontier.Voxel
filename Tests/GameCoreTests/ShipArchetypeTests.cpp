#include "TestSupport.h"

#include "ShipArchetype.h"

#include <string>

#include "CppUnitTest.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameCoreTests
{

namespace
{

constexpr std::string_view EQUIPMENT = R"(engine name=E thrust=48000 drag=599 reverse=0.2 chargetime=5 cruisepower=20
powerplant name=P capacity=1000 charge=100 thrustcapacity=1000 thrustcharge=100
thruster name=T force=72000 drain=165
)";

constexpr std::string_view SHIP_KEYS =
  "model=EscortLancer mass=100 inertia=2800,2800,1000 drag=1 torque=24000,24000,58000 angulardrag=15000,15000,35000 strafe=20000 "
  "strafedrain=2 maxbank=30 engine=E powerplant=P thruster=T docking=berth";

/// The error parsing fails with; fails the test if it parses.
[[nodiscard]] std::string ErrorFor(std::string_view _text)
{
  Outpost::ShipContent content;
  std::string error;
  Assert::IsFalse(Outpost::ParseShipContent(_text, "Ships.txt", content, error));
  return error;
}

[[nodiscard]] std::string Ship(std::string_view _name, std::string_view _keys = SHIP_KEYS)
{
  return "ship name=" + std::string(_name) + " " + std::string(_keys) + "\n";
}

void AssertMentions(const std::string& _error, std::string_view _part)
{
  const std::wstring message(_error.begin(), _error.end());
  Assert::IsTrue(_error.find(_part) != std::string::npos, message.c_str());
}

} // namespace

TEST_CLASS(ShipArchetypeTests)
{
public:
  TEST_METHOD(TheShippedContentLoadsAndResolves)
  {
    Outpost::ShipContent content;
    std::string error;
    Assert::IsTrue(Outpost::LoadShipContent(AssetRoot() / "Content" / "Ships.txt", content, error),
                   std::wstring(error.begin(), error.end()).c_str());
    Assert::IsNotNull(content.FindShip("EscortLancer"));
    Assert::IsNotNull(content.FindShip("EscortTalon"));
    const std::vector<std::string> problems = Outpost::ResolveShipModels(content, LoadRealScene());
    Assert::IsTrue(problems.empty(), problems.empty() ? L"" : std::wstring(problems[0].begin(), problems[0].end()).c_str());
  }

  TEST_METHOD(AShipIsReadWithItsUnits)
  {
    Outpost::ShipContent content;
    std::string error;
    Assert::IsTrue(Outpost::ParseShipContent(std::string(EQUIPMENT) + Ship("Lancer"), "Ships.txt", content, error));
    const Outpost::ShipArchetype& ship = content.ships.at(0);
    Assert::AreEqual(100.0f, ship.massKilograms);
    Assert::AreEqual(1000.0f, ship.inertiaKilogramSquareMetres.z);
    Assert::AreEqual(58000.0f, ship.steeringTorqueNewtonMetres.z);
    Assert::AreEqual(35000.0f, ship.angularDragNewtonMetreSeconds.z);
    Assert::AreEqual(30.0f, ship.maxBankDegrees);
    Assert::AreEqual(599.0f, content.FindEngine(ship.engine)->dragKilogramsPerSecond);
  }

  TEST_METHOD(AShipMayNameEquipmentDefinedBelowIt)
  {
    Outpost::ShipContent content;
    std::string error;
    Assert::IsTrue(Outpost::ParseShipContent(Ship("Lancer") + std::string(EQUIPMENT), "Ships.txt", content, error));
  }

  TEST_METHOD(MistakesNameTheFileAndLine)
  {
    AssertMentions(ErrorFor(std::string(EQUIPMENT) + "hull name=X\n"), "Ships.txt line 4: unknown keyword 'hull'");
    AssertMentions(ErrorFor(std::string(EQUIPMENT) + Ship("Lancer", std::string(SHIP_KEYS) + " colour=red")),
                   "line 4: unknown key 'colour'");
    AssertMentions(ErrorFor("thruster name=T force=72000\n"), "line 1: missing 'drain'");
    AssertMentions(ErrorFor("engine name=E thrust=48000 drag=599 reverse=2 chargetime=5 cruisepower=20\n"), "'reverse' must be a number");
    AssertMentions(ErrorFor("thruster name=T force=-1 drain=1\n"), "'force' must be a number");
    AssertMentions(ErrorFor("thruster name=T force=1 drain=1\nthruster name=T force=2 drain=2\n"), "line 2: 'T' is defined twice");
    AssertMentions(ErrorFor("thruster name=lower force=1 drain=1\n"), "must start with an upper-case letter");
  }

  TEST_METHOD(AShipMustDivideByPositiveMassAndInertia)
  {
    std::string keys(SHIP_KEYS);
    keys.replace(keys.find("mass=100"), 8, "mass=0");
    AssertMentions(ErrorFor(std::string(EQUIPMENT) + Ship("Lancer", keys)), "'mass' must be a number");
  }

  TEST_METHOD(ADanglingEquipmentReferenceIsAnError)
  {
    std::string keys(SHIP_KEYS);
    keys.replace(keys.find("engine=E"), 8, "engine=Q");
    AssertMentions(ErrorFor(std::string(EQUIPMENT) + Ship("Lancer", keys)), "line 4: ship 'Lancer' names no known engine");
  }

  TEST_METHOD(AnUnknownModelIsReported)
  {
    Outpost::ShipContent content;
    std::string error;
    std::string keys(SHIP_KEYS);
    keys.replace(keys.find("model=EscortLancer"), 18, "model=Nothing");
    Assert::IsTrue(Outpost::ParseShipContent(std::string(EQUIPMENT) + Ship("Lancer", keys), "Ships.txt", content, error));
    const std::vector<std::string> problems = Outpost::ResolveShipModels(content, LoadRealScene());
    Assert::AreEqual(std::size_t{1}, problems.size());
    AssertMentions(problems[0], "'Nothing'");
  }
};

} // namespace GameCoreTests
