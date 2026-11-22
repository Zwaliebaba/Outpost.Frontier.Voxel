#include "SaveGame.h"

#include "CppUnitTest.h"

#include <string>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

[[nodiscard]] std::wstring Wide(const std::string& _text)
{
  return std::wstring(_text.begin(), _text.end());
}

/// Every field of the record set to something other than its default, with values whose decimal
/// forms are long, so that a lossy number format would show.
[[nodiscard]] Outpost::SaveGame FullSave(Outpost::LocationKind _kind)
{
  Outpost::SaveGame save;
  Outpost::Character& character = save.character;
  character.location.kind = _kind;
  if (_kind == Outpost::LocationKind::Docked)
  {
    character.location.station = "Shipyard";
    character.location.instance = "Hangar";
    character.location.point = "Berth";
  }
  else
  {
    Outpost::FlightState& flight = character.location.flight;
    flight.position = Outpost::UniversePosition{-87.98123456789012, 1.0e7 / 3.0, -234.63};
    flight.orientation = DirectX::XMFLOAT4{0.1f / 3.0f, -0.7071068f, 0.2f, 0.67f};
    flight.velocityMetresPerSecond = DirectX::XMFLOAT3{79.93f, -1.0f / 3.0f, 1.0e-7f};
    flight.angularVelocityRadiansPerSecond = DirectX::XMFLOAT3{0.01f, -0.25f, 2.0f / 3.0f};
    flight.powerPool = 812.5f / 3.0f;
    flight.thrustPool = 0.125f;
    flight.thrusterActive = true;
    flight.cruise = Outpost::CruiseState::Cooldown;
    flight.cruiseTimerTicks = -173;
    flight.cruiseRamp = 0.9f / 7.0f;
  }
  character.ship = Outpost::CharacterShip{"Lancer", 87, {{"Engines", 90}, {"Hull Section (B)", 34}}};
  character.cargo = {{"Ore", 12}, {"Alloy plate", 3}};
  character.money = -9'000'000'000'123;
  character.baseSnapshot = Outpost::CharacterShip{"Lancer", 100, {{"Engines", 100}}};
  character.interfaceState = {{"selected", "Tug"}, {"note", "two words"}};
  save.universe.tick = 123'456'789'012ull;
  save.universe.resources = {{"Alloy", 12'400, 37}, {"Water", 0, 3599}};
  save.universe.objectives = {{"Defend the shipyard", true}, {"Escort tug T-041 to dock", false}};
  return save;
}

[[nodiscard]] std::string Formatted(const Outpost::SaveGame& _save)
{
  std::string text;
  std::string error;
  Assert::IsTrue(Outpost::FormatSaveGame(_save, text, error), Wide(error).c_str());
  return text;
}

[[nodiscard]] Outpost::SaveGame Parsed(const std::string& _text)
{
  Outpost::SaveGame save;
  std::string error;
  Assert::IsTrue(Outpost::ParseSaveGame(_text, save, error), Wide(error).c_str());
  return save;
}

[[nodiscard]] std::string Refusal(const std::string& _text)
{
  Outpost::SaveGame save;
  std::string error;
  Assert::IsFalse(Outpost::ParseSaveGame(_text, save, error));
  Assert::IsFalse(error.empty());
  return error;
}

} // namespace

TEST_CLASS(SaveGameTests)
{
public:
  TEST_METHOD(EveryFieldRoundTripsDocked)
  {
    const Outpost::SaveGame save = FullSave(Outpost::LocationKind::Docked);
    Assert::IsTrue(Parsed(Formatted(save)) == save);
  }

  TEST_METHOD(EveryFieldRoundTripsInSpaceToTheBit)
  {
    const Outpost::SaveGame save = FullSave(Outpost::LocationKind::Space);
    const Outpost::SaveGame read = Parsed(Formatted(save));
    Assert::IsTrue(read.character.location.flight == save.character.location.flight, L"the flight state, bit for bit");
    Assert::IsTrue(read == save);
  }

  TEST_METHOD(TheFileIsAReadableTextRecord)
  {
    const std::string text = Formatted(FullSave(Outpost::LocationKind::Docked));
    Assert::AreNotEqual(std::string::npos, text.find("section name=Character\r\n"));
    Assert::AreNotEqual(std::string::npos, text.find("location kind=docked station=Shipyard instance=Hangar point=Berth\r\n"));
    Assert::AreNotEqual(std::string::npos, text.find("module name=\"Hull Section (B)\" health=34\r\n"));
    Assert::AreNotEqual(std::string::npos, text.find("resource name=Alloy amount=12400 remainder=37\r\n"));
  }

  TEST_METHOD(AnUnknownSectionIsKeptOnRewrite)
  {
    std::string text = Formatted(FullSave(Outpost::LocationKind::Docked));
    // A section from a later version, inserted before the end record, which then counts it.
    const std::size_t end = text.find("end sections=2 records=");
    const std::string records = text.substr(end + std::string("end sections=2 records=").size());
    const int count = std::stoi(records) + 2;
    text = text.substr(0, end) + "section name=Missions\r\nmission name=Breach stage=2\r\nlog text=\"met the tug\"\r\n" +
           "end sections=3 records=" + std::to_string(count) + "\r\n";
    const Outpost::SaveGame read = Parsed(text);
    Assert::AreEqual(std::size_t{1}, read.kept.size());
    const std::string rewritten = Formatted(read);
    Assert::AreNotEqual(std::string::npos,
                        rewritten.find("section name=Missions\r\nmission name=Breach stage=2\r\nlog text=\"met the tug\"\r\n"));
    Assert::AreEqual(std::size_t{1}, Parsed(rewritten).kept.size());
  }

  TEST_METHOD(ATruncatedFileIsRefusedWithAReason)
  {
    const std::string text = Formatted(FullSave(Outpost::LocationKind::Space));
    const std::string reason = Refusal(text.substr(0, text.size() / 2));
    Assert::IsTrue(reason.find("incomplete") != std::string::npos, Wide(reason).c_str());
  }

  TEST_METHOD(BrokenSectionsAreRefusedWithTheLine)
  {
    const auto broken = [](const std::string& _old, const std::string& _new)
    {
      std::string text = Formatted(FullSave(Outpost::LocationKind::Docked));
      text.replace(text.find(_old), _old.size(), _new);
      return Refusal(text);
    };
    Assert::IsTrue(broken("character money=", "character cash=").find("Character line 4: unknown key 'cash'") != std::string::npos);
    Assert::IsTrue(broken("kind=docked", "kind=orbit").find("'kind' must be docked or space") != std::string::npos);
    Assert::IsTrue(broken("health=34", "health=lots").find("'health' must be a whole number") != std::string::npos);
    Assert::IsTrue(broken("universe tick", "planet tick").find("unknown record 'planet'") != std::string::npos);
    Assert::IsTrue(broken("section name=Universe", "section name=Cosmos").find("no Universe section") != std::string::npos);
  }
};

} // namespace GameLogicTests
