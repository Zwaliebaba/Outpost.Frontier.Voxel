#pragma once

#include "FlightModel.h"
#include "HazardWarning.h"
#include "HudBatch.h"
#include "ScreenProjection.h"

#include <DirectXMath.h>

#include <string>

namespace Outpost
{

/// The player's target as the flight HUD shows it (plan task 3.10): what the server last stated of
/// it, and where it is on screen.
struct TargetReadout
{
  bool active = false;
  std::string callsign;
  bool hostile = false;
  bool ship = true; // a station otherwise: no shield or structure to show
  float distanceMetres = 0.0f;
  int shieldPercent = -1; // -1: it has no shield
  int hullPercent = 100;  // its structure, measured from its mask
  std::string module;     // the sub-target, or empty for the whole ship
  int modulePercent = 100;
  bool onScreen = false;
  Neuron::ScreenRect bracket{}; // around its bounds, or the sub-target's
  bool leadOnScreen = false;
  DirectX::XMFLOAT2 lead{0.0f, 0.0f}; // where to aim for the guns to meet it
};

/// What the flight HUD shows, all of it read off the predicted ship and the input layer.
struct FlightHudInput
{
  float width = 1920.0f;
  float height = 1080.0f;
  float scale = 1.0f; // 1 at 1080 lines, as the tactical HUD
  float timeSeconds = 0.0f;
  std::string callsign;
  float speedMetresPerSecond = 0.0f;
  float throttle = 0.0f;     // 0..1
  float powerShare = 1.0f;   // the power pool over its capacity
  float thrustShare = 1.0f;  // the thrust pool over its capacity
  float shieldShare = -1.0f; // the shield over its capacity; below 0, no shield to show
  bool shieldUp = true;      // false while it rebuilds after a collapse
  CruiseState cruise = CruiseState::Off;
  float cruiseChargeShare = 0.0f; // charging: how far through the charge time
  bool cruiseWish = false;
  bool engineOn = true;
  bool thruster = false;
  bool reverse = false;
  bool mouseSteering = false;
  bool mouseLocked = false;
  DirectX::XMFLOAT2 cursor{960.0f, 540.0f};
  float clickRadiusPixels = 120.0f;
  std::string order;    // the order being flown, or empty in free flight
  HazardWarning hazard; // the hazard where the ship is, or none (plan task 2.8)
  TargetReadout target; // the player's target, if any (plan task 3.10)
  bool showHelp = true;
  std::string statusLine;
};

/// Where the flight HUD put its parts, for tests and for anything drawn beside it.
struct FlightHudLayout
{
  Neuron::ScreenRect panel{};
  Neuron::ScreenRect throttleBar{};
  Neuron::ScreenRect powerBar{};
  Neuron::ScreenRect thrustBar{};
  Neuron::ScreenRect shieldBar{};   // empty without a shield
  Neuron::ScreenRect targetPanel{}; // empty without a target
  Neuron::ScreenRect help{};
  Neuron::ScreenRect warning{}; // empty when there is no hazard to warn of
};

/// The chase view's HUD (plan task 1.9): a reticle at the centre and a line to the cursor while it
/// steers; a panel at the bottom centre with the speed, the throttle, both pools and the engine's
/// state; the key help; and, inside a hazard zone, a warning under the status line saying what the
/// zone does (plan task 2.8). In a fight (plan task 3.10) it shows the ship's own shield beside the
/// throttle, brackets the target with a lead marker where to aim, and gives the target's shield,
/// structure and sub-targeted module in a panel at the top right. It uses the tactical HUD's fonts
/// and palette.
class FlightHud
{
public:
  [[nodiscard]] FlightHudLayout Build(Neuron::HudBatch& _batch, const FlightHudInput& _input) const;
};

} // namespace Outpost
