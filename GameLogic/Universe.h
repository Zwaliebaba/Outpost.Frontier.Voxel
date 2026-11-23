#pragma once

#include "Ai.h"
#include "Character.h"
#include "Collision.h"
#include "Combat.h"
#include "MaskEvent.h"
#include "Messages.h"
#include "SaveGame.h"
#include "SceneDefinition.h"
#include "ShipArchetype.h"
#include "ShipFlight.h"
#include "SpaceSupport.h"
#include "SystemContent.h"
#include "UniverseSnapshot.h"

#include <DirectXMath.h>

#include <array>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
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
/// own content, gives every placed instance a universe object with an identity and a pose (ADR-009),
/// and advances in whole ticks. Nothing but an Observer reads it on a client's behalf (R25): the
/// client builds its own picture from the messages the observer sends.
///
/// Each tick pays resource income, the ledger the design gives an outpost whose owner is away
/// (Design/GameDesign.md §7.2), and flies every ship whose model has a ship archetype (plan task
/// 1.4): the ship a player flies on the command its session sent for the tick, every other ship on
/// its directive. Each ship's tick is StepShip, the one function the client's prediction runs too.
///
/// Docking (plan tasks 2.3 and 2.4, blueprint ch. 04 §3.2 to §3.5): the Dock command picks the
/// nearest station in range (D20), flies the ship there and asks space support for a point. Once the
/// ship holds one, the server flies it to the point's entry and then carries it along the approach to
/// the pad with collisions off. A docked ship stays in its berth, protected (D16). Launching carries
/// it back out to the entry and releases it there. From the Dock command until that release the
/// server flies the ship, the player's commands are set aside, and the client draws the ship where
/// the server says; the release states the ship's whole flight state again, and prediction resumes.
///
/// What the player owns is a character record (R23), and the universe saves it with the universe's own
/// state when the save rule says (D17); TakeSaveRequest tells the executable when.
///
/// Combat (plan slice 3): every ship flies its launchers and its shield in StepShip, and its shots
/// become the bolts Combat traces. A harmful bolt that meets a bubble takes from the shield; one that
/// meets voxels carves them by a mask event (ADR-012), and the ship's module health, measured from
/// its mask, sets its condition: thrust, charge, regeneration and guns. A ship whose core stops is
/// destroyed and breaks up into wrecks; the player's returns to its last base as it was fitted there
/// (D1). An NPC with a pilot flies itself (plan task 3.8), and a pilot that flees far enough is gone.
class Universe
{
public:
  static constexpr std::size_t MAX_CONTACT_RECORDS = 256;
  // Commands for ticks further ahead than this are refused, so a client cannot fill the queue.
  static constexpr std::uint64_t MAX_COMMAND_LEAD_TICKS = 120;

  /// A repair kit restores at most this many structure cells (plan task 3.11).
  static constexpr std::uint32_t REPAIR_KIT_CELLS = 120;

  /// The scripted mover's speeds: the approach peaks at this (ch. 04 §3.3's 18 m/s cap) ...
  static constexpr float DOCKING_PEAK_METRES_PER_SECOND = 18.0f;
  /// ... and a launch leaves the entry at this, accelerating evenly from the pad.
  static constexpr float LAUNCH_RELEASE_METRES_PER_SECOND = 30.0f;
  /// When a station's body lies between a cleared ship and its entry, the ship first flies to the
  /// point this far out along the docking axis, then in along the axis to the entry.
  static constexpr float AXIS_STANDOFF_METRES = 250.0f;

  /// Loads Scene.txt, the model sizes, Content/Ships.txt, Content/System.txt and the combat content
  /// (hulls, modules, pilots and every model's voxels) from the asset root. On broken content, false
  /// and why.
  [[nodiscard]] bool LoadContent(const std::filesystem::path& _assetRoot, std::string& _outError);
  /// Builds the universe from parsed content. Attached instances need their models' sizes set
  /// (LoadSceneContent); without them their poses use a pivot at the origin, and the bodies are
  /// empty boxes. The player's ship starts where the scene places it, flying; NewGame or Restore
  /// then puts it where a game starts.
  /// Without combat content nothing can be carved: bolts meet only shields, and module health stays
  /// as the scene authored it.
  void Load(const SceneDefinition& _scene, const ShipContent& _ships = {}, const SystemContent& _system = {},
            std::shared_ptr<const CombatContent> _combat = {});

  /// A new game (GameDesign.md D18): the player's ship docked at a free point of the start station,
  /// with a fresh character. False, with the reason, when the station has no free point for it.
  [[nodiscard]] bool NewGame(std::string& _outError);
  /// The universe and the character as a save left them. False, with the reason, for a save that does
  /// not fit the content: another player ship, an unknown station, point, resource or objective.
  [[nodiscard]] bool Restore(const SaveGame& _save, std::string& _outError);
  /// The character and the universe as they stand. A ship carried in or out is saved docked, where the
  /// carrying started or ends.
  [[nodiscard]] SaveGame MakeSave() const;
  /// Whether the save rule (D17) asked for a save since the last call: the player docked or launched.
  /// Quitting is the executable's to save on.
  [[nodiscard]] bool TakeSaveRequest() noexcept;

  /// A docking command from the session that flies the player's ship (plan tasks 2.3 and 2.4). The
  /// answer reaches the client as the ship's dock status. False for another ship, or for a command
  /// that does not apply in the ship's phase.
  bool Command(const DockCommandMessage& _command);
  /// Uses one of an item the player's ship carries (plan task 3.11). False for another ship, for none
  /// left, or for a ship that is docked or carried, where the hangar repairs.
  bool UseItem(const UseItemMessage& _use);
  [[nodiscard]] std::int64_t RepairKits() const noexcept
  {
    return m_character.repairKits;
  }
  [[nodiscard]] const Combat& Battle() const noexcept
  {
    return m_combat;
  }
  /// The authored objects that are gone, destroyed or fled, by name.
  [[nodiscard]] const std::vector<std::string>& Gone() const noexcept
  {
    return m_gone;
  }
  /// What an NPC's pilot is doing, or nothing for a ship without one.
  [[nodiscard]] std::optional<AiState> PilotState(ObjectId _ship) const noexcept;
  /// The object authored under this instance name, or NO_OBJECT.
  [[nodiscard]] ObjectId FindByName(std::string_view _instance) const noexcept;

  /// Where a ship is in docking, and the answer to its last command.
  [[nodiscard]] DockStatusState DockStatus(ObjectId _ship) const;
  /// Whether nothing may harm the ship: while it is carried in, docked, or carried out (D16).
  [[nodiscard]] bool Protected(ObjectId _ship) const noexcept;
  [[nodiscard]] const SystemContent& System() const noexcept
  {
    return m_system;
  }
  [[nodiscard]] const SpaceSupport& Support() const noexcept
  {
    return m_support;
  }

  void Tick();

  [[nodiscard]] std::uint64_t TickCount() const noexcept
  {
    return m_state.tick;
  }

  /// The server's own state. Only the observer encodes it for a client; no client is handed it.
  [[nodiscard]] const UniverseSnapshot& State() const noexcept
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

  /// The scripted mover (ch. 04 §2): the ship's pose set each tick along three points, collisions off.
  struct Mover
  {
    std::array<UniversePosition, 3> points{};
    double lengthMetres = 0.0;
    std::uint64_t startTick = 0;
    std::uint64_t durationTicks = 1;
    DirectX::XMFLOAT4 fromOrientation{0.0f, 0.0f, 0.0f, 1.0f};
    DirectX::XMFLOAT4 toOrientation{0.0f, 0.0f, 0.0f, 1.0f};
    bool outward = false; // a launch: from rest, leaving at speed; otherwise eased in and out
  };

  struct Flyer
  {
    std::size_t objectIndex = 0;
    ShipBody body;
    CollisionBox localBox; // in the ship's frame: the centre is its offset from the pivot
    float massKilograms = 0.0f;
    DockKind dockingClass = DockKind::Berth;
    FlightState state;
    Manoeuvre manoeuvre;
    bool arrived = false;
    std::uint64_t divergedAt = 0;
    std::uint64_t takenAt = 0; // the first tick the server flies the ship, from a Dock command between ticks
    DockStatusState dock;
    std::size_t dockStation = SpaceSupport::NO_POINT;
    std::size_t dockPoint = SpaceSupport::NO_POINT;
    UniversePosition farTarget; // the far approach's goal: the nearest fitting entry
    UniversePosition holdAnchor;
    std::optional<UniversePosition> axisPoint; // staging's first leg, when the way to the entry is blocked
    Mover mover;
    ShipFlightSpec baseSpec;               // the archetype's spec; body.spec is it as the modules leave it
    std::vector<std::string> mountRegions; // the weapon module of each mount
    int hull = -1;                         // the hull of its archetype in the combat content, or -1
    std::optional<Ai> ai;                  // its pilot, for an NPC that has one
  };

  struct PendingShots
  {
    ObjectId shooter = NO_OBJECT;
    Owner owner = Owner::Neutral;
    DirectX::XMFLOAT3 velocity{};
    std::array<std::uint8_t, MAX_LAUNCHERS> launchers{};
    FireResult fire;
  };

  void PayIncome();
  void FlyShips(std::vector<PendingShots>& _outShots);
  void Fight(const std::vector<PendingShots>& _shots);
  /// A ship's health again, from its mask, and a flying ship's condition with it.
  void Measure(ObjectId _ship);
  void Destroy(ObjectId _ship);
  void RemoveObject(ObjectId _object);
  void ReturnToBase(Flyer& _flyer);
  [[nodiscard]] std::vector<AiContact> PilotContacts() const;
  void BeatSupport();
  void StateDock(Flyer& _flyer, DockPhase _phase, DockAnswer _answer, std::string _reason = {});
  void AskForPoint(Flyer& _flyer);
  void Clear(Flyer& _flyer, std::size_t _point);
  void StartMover(Flyer& _flyer, const UniversePosition& _from, const UniversePosition& _via, const UniversePosition& _to,
                  const DirectX::XMFLOAT4& _toOrientation, bool _outward);
  /// One tick of the mover; true when it has reached its last point.
  bool StepMover(Flyer& _flyer);
  void PlaceDocked(Flyer& _flyer, std::size_t _station, std::size_t _point);
  void Release(Flyer& _flyer);
  void SetPose(Flyer& _flyer);
  [[nodiscard]] ShipState* ShipStateOf(ObjectId _ship) noexcept;
  [[nodiscard]] const ShipState* ShipStateOf(ObjectId _ship) const noexcept;
  [[nodiscard]] CharacterShip ShipRecordOf(ObjectId _ship) const;
  void RecordContact(const Flyer& _flyer, const ShipTickResult& _result, float _bodyMass);
  [[nodiscard]] Flyer* FindFlyer(ObjectId _ship) noexcept;
  [[nodiscard]] const Flyer* FindFlyer(ObjectId _ship) const noexcept;

  UniverseSnapshot m_state;
  SceneDefinition m_scene; // kept for the pilots and presets content names
  ShipContent m_ships;
  Combat m_combat;
  std::vector<std::string> m_gone;
  SystemContent m_system;
  SpaceSupport m_support;
  Character m_character; // the player's record; location and ship are read from the universe when saved
  bool m_saveRequested = false;
  std::vector<Income> m_income;
  std::vector<Flyer> m_flyers;
  std::vector<CollisionBox> m_staticBodies;
  ObjectId m_playerShip = NO_OBJECT;
  // The player's commands by the tick they are for, in tick order. Vectors rather than maps here
  // and below: MSVC's node containers allocate even when moved, so a map would make Universe's move
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
