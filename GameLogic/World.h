#pragma once

#include "Collision.h"
#include "Messages.h"
#include "SceneDefinition.h"
#include "ShipArchetype.h"
#include "ShipFlight.h"
#include "WorldSnapshot.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace Outpost
{

/// A contact the tick found, with the damage chapter 02 §3.11 gives each side. Recorded, not yet
/// applied: hulls take damage with slice 3.
struct ContactRecord
{
  std::uint64_t tick = 0;
  ObjectId ship = NO_OBJECT;
  ObjectId body = NO_OBJECT;
  float relativeSpeedMetresPerSecond = 0.0f;
  float damageToShip = 0.0f;
  float damageToBody = 0.0f;
};

/// The authoritative game state: the server half of the game (AGENTS.md R18, R19). It loads its
/// own content, gives every placed instance a world object with an identity and a pose (ADR-009),
/// and advances in whole ticks. Nothing but an Observer reads it on a client's behalf (R25): the
/// client builds its own picture from the messages the observer sends.
///
/// Each tick pays resource income, the ledger the design gives an outpost whose owner is away
/// (Design/GameDesign.md §7.2), and flies every ship whose model has a ship archetype (plan task
/// 1.4): the ship a player flies on the command its session sent for the tick, every other ship on
/// its directive. Each ship's tick is StepShip, the one function the client's prediction runs too.
class World
{
public:
  static constexpr std::size_t MAX_CONTACT_RECORDS = 256;
  // Commands for ticks further ahead than this are refused, so a client cannot fill the queue.
  static constexpr std::uint64_t MAX_COMMAND_LEAD_TICKS = 120;

  /// Loads Scene.txt, the model sizes, and Content/Ships.txt from the asset root. On broken content,
  /// false and why.
  [[nodiscard]] bool LoadContent(const std::filesystem::path& _assetRoot, std::string& _outError);

  /// Builds the world from parsed content. Attached instances need their models' sizes set
  /// (LoadSceneContent); without them their poses use a pivot at the origin, and the bodies are
  /// empty boxes.
  void Load(const SceneDefinition& _scene, const ShipContent& _ships = {});

  void Tick();

  [[nodiscard]] std::uint64_t TickCount() const noexcept
  {
    return m_state.tick;
  }

  /// The server's own state. Only the observer encodes it for a client; no client is handed it.
  [[nodiscard]] const WorldSnapshot& State() const noexcept
  {
    return m_state;
  }

  /// The ship the scene gives the player to fly, or NO_OBJECT.
  [[nodiscard]] ObjectId PlayerShip() const noexcept
  {
    return m_playerShip;
  }

  /// A command from the session that flies the player's ship. Refused, with false, for another ship
  /// or for a tick that has run already or lies too far ahead.
  bool SubmitCommand(const FlightInputMessage& _input);

  /// A directive to a ship no player flies (blueprint ch. 05): the manoeuvre it runs from the next
  /// tick. False for a ship that does not fly or that a player flies.
  bool Direct(ObjectId _ship, const Manoeuvre& _manoeuvre);

  /// A flying ship's state, or null.
  [[nodiscard]] const FlightState* Flight(ObjectId _ship) const noexcept;

  /// The last tick whose state for this ship the client could not have predicted, or 0.
  [[nodiscard]] std::uint64_t DivergedAt(ObjectId _ship) const noexcept;

  /// Whether the ship's go-to or hold has arrived, as of the last tick.
  [[nodiscard]] bool Arrived(ObjectId _ship) const noexcept;

  [[nodiscard]] const std::vector<ContactRecord>& Contacts() const noexcept
  {
    return m_contacts;
  }

  [[nodiscard]] const std::vector<CollisionBox>& StaticBodies() const noexcept
  {
    return m_staticBodies;
  }

private:
  struct Income
  {
    std::int64_t perMinute = 0;
    std::int64_t remainder = 0; // income owed but not yet whole, in 1/ticks-per-minute units
  };

  struct Flyer
  {
    std::size_t objectIndex = 0;
    ShipBody body;
    CollisionBox localBox; // in the ship's frame: the centre is its offset from the pivot
    float massKilograms = 0.0f;
    FlightState state;
    Manoeuvre manoeuvre;
    bool arrived = false;
    std::uint64_t divergedAt = 0;
  };

  void PayIncome();
  void FlyShips();
  void RecordContact(const Flyer& _flyer, const ShipTickResult& _result, float _bodyMass);
  [[nodiscard]] Flyer* FindFlyer(ObjectId _ship) noexcept;
  [[nodiscard]] const Flyer* FindFlyer(ObjectId _ship) const noexcept;

  WorldSnapshot m_state;
  std::vector<Income> m_income;
  std::vector<Flyer> m_flyers;
  std::vector<CollisionBox> m_staticBodies;
  ObjectId m_playerShip = NO_OBJECT;
  // The player's commands by the tick they are for, in tick order. Vectors rather than maps here
  // and below: MSVC's node containers allocate even when moved, so a map would make World's move
  // throw.
  std::vector<std::pair<std::uint64_t, ShipCommand>> m_commands;
  ShipCommand m_heldCommand; // the newest command already run, reused for a tick whose command is missing
  bool m_commandsStarted = false;
  std::vector<ContactRecord> m_contacts;
  struct LastContact
  {
    ObjectId ship = NO_OBJECT;
    ObjectId body = NO_OBJECT;
    std::uint64_t tick = 0;
  };
  std::vector<LastContact> m_lastContacts;
};

} // namespace Outpost
