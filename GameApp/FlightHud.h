#pragma once

#include "FlightModel.h"
#include "HudBatch.h"
#include "ScreenProjection.h"

#include <DirectXMath.h>

#include <string>

namespace Outpost
{

/// What the flight HUD shows, all of it read off the predicted ship and the input layer.
struct FlightHudInput
{
  float width = 1920.0f;
  float height = 1080.0f;
  float scale = 1.0f; // 1 at 1080 lines, as the tactical HUD
  float timeSeconds = 0.0f;
  std::string callsign;
  float speedMetresPerSecond = 0.0f;
  float throttle = 0.0f;    // 0..1
  float powerShare = 1.0f;  // the power pool over its capacity
  float thrustShare = 1.0f; // the thrust pool over its capacity
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
  std::string order; // the order being flown, or empty in free flight
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
  Neuron::ScreenRect help{};
};

/// The chase view's HUD (plan task 1.9): a reticle at the centre and a line to the cursor while it
/// steers; a panel at the bottom centre with the speed, the throttle, both pools and the engine's
/// state; and the key help. It uses the tactical HUD's fonts and palette.
class FlightHud
{
public:
  [[nodiscard]] FlightHudLayout Build(Neuron::HudBatch& _batch, const FlightHudInput& _input) const;
};

} // namespace Outpost
