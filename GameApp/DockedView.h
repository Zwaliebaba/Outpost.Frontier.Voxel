#pragma once

#include "ControlFocus.h"
#include "HudBatch.h"
#include "ScreenProjection.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Outpost
{

/// The docked view's sections, in the rail's order (Design/GameplayUiProposal.md, docked command).
enum class DockedSection : std::uint8_t
{
  Build,
  Yard,
  Fleet,
  Services
};

/// What the docked view shows. Every fact comes from the server's statements or the content.
struct DockedViewInput
{
  float width = 1920.0f;
  float height = 1080.0f;
  float scale = 1.0f; // 1 at 1080 lines
  DockedSection section = DockedSection::Services;
  std::string stationLabel; // "Kessler Reach Shipyard"
  std::string berth;        // where the ship sits: "Hangar berth"
  std::string shipLabel;    // "Lancer E-114"
  // Launch is offered when the ship can fly; otherwise it is disabled with the reason.
  bool launchAvailable = true;
  std::string launchUnavailable;
  bool launchPending = false; // asked, not yet answered
  std::string launchRefused;  // the server's refusal, shown until the next request
  const Neuron::ControlFocus* focus = nullptr;
};

/// A control the view laid out, for the client to register with its ControlFocus.
struct DockedControl
{
  Neuron::ControlId id = Neuron::NO_CONTROL;
  Neuron::ScreenRect rect{};
  bool enabled = true;
  std::string reason;
};

/// Where the view put its parts.
struct DockedViewLayout
{
  std::vector<DockedControl> controls; // in tab order
  Neuron::ScreenRect rail{};
  Neuron::ScreenRect inspector{};
};

/// The docked command view (plan task 2.5): a narrow rail of sections at the left and one context
/// inspector at the right, over the tactical view of the station, which stays visible between them.
/// Slice 2 fills SERVICES: launch, and a repair that is not yet offered; BUILD, YARD and FLEET get
/// their content in slices 4 and 6. Pure layout over a HudBatch, like TacticalHud; the client turns
/// clicks into requests, and the server's answers into the states drawn here.
class DockedView
{
public:
  static constexpr Neuron::ControlId RAIL_BUILD = 1;
  static constexpr Neuron::ControlId RAIL_YARD = 2;
  static constexpr Neuron::ControlId RAIL_FLEET = 3;
  static constexpr Neuron::ControlId RAIL_SERVICES = 4;
  static constexpr Neuron::ControlId LAUNCH = 10;
  static constexpr Neuron::ControlId REPAIR = 11;

  /// The rail control that selects a section, and the section a rail control selects.
  [[nodiscard]] static Neuron::ControlId RailControl(DockedSection _section) noexcept;
  [[nodiscard]] static bool SectionOf(Neuron::ControlId _id, DockedSection& _outSection) noexcept;

  [[nodiscard]] DockedViewLayout Build(Neuron::HudBatch& _batch, const DockedViewInput& _input) const;
};

} // namespace Outpost
