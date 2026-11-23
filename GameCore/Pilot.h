#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Outpost
{

/// How an NPC flies and fights (plan task 3.8; blueprint ch. 05). The numbers are a personality: the
/// same behaviours with other numbers make another pilot.
struct PilotRecord
{
  std::string name;
  float detectionMetres = 2500.0f;
  float turnInMetres = 900.0f;
  float passMetres = 250.0f;
  float passSeconds = 2.0f;
  float extendSeconds = 6.0f;
  float trailMetres = 250.0f;
  float burstSeconds = 2.0f;
  float pauseSeconds = 1.0f;
  float aimConeDegrees = 4.0f;
  float aimExponent = 1.1f;
  float rerollMinSeconds = 0.5f;
  float rerollMaxSeconds = 2.0f;
  float rangeFactor = 1.1f;
  float evadeMetres = 300.0f;
  float dodgeSeconds = 3.0f;
  int fleeCorePercent = 80;
  int fleeHullPercent = 40;
  float goneMetres = 5000.0f;
};

struct PilotContent
{
  std::vector<PilotRecord> pilots;

  [[nodiscard]] const PilotRecord* FindPilot(std::string_view _name) const noexcept;
};

/// Assets/Content/Pilots.txt (ADR-009): unknown keys, missing keys, values out of range and duplicate
/// names are errors naming the file and line.
[[nodiscard]] bool ParsePilots(std::string_view _text, std::string_view _fileName, PilotContent& _outContent, std::string& _outError);
[[nodiscard]] bool LoadPilots(const std::filesystem::path& _path, PilotContent& _outContent, std::string& _outError);

} // namespace Outpost
