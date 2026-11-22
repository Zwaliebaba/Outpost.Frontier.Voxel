#pragma once

#include "RecordFile.h"

#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Neuron
{

// A save file (ADR-011): a record file (ADR-002) in named sections, so that a person can read it
// and each part of the game writes and reads its own part of it.
//
//   # a comment line naming what wrote it
//   save version=1
//   section name=Character
//   ...records of that section...
//   section name=Universe
//   ...
//   end sections=2 records=14
//
// The end record counts what came before it, so a file cut short anywhere is refused rather than
// read as a smaller save. The format knows nothing of what the sections mean: the game does.

inline constexpr int SAVE_FORMAT_VERSION = 1;

struct SaveSection
{
  std::string name;
  std::vector<Record> records;
};

/// What a part of the game is given to write its sections with: records and fields, never a file.
class SaveWriter
{
public:
  /// Starts a section; the records added next belong to it. A name already used is refused when the
  /// save is formatted.
  void BeginSection(std::string_view _name);
  /// Starts a record in the current section; Field() adds to it.
  SaveWriter& Add(std::string_view _keyword);
  SaveWriter& Field(std::string_view _key, std::string_view _value);
  /// A whole section copied as it is, such as one a newer version wrote that this one does not read.
  void AddSection(const SaveSection& _section);

  [[nodiscard]] const std::vector<SaveSection>& Sections() const noexcept
  {
    return m_sections;
  }

private:
  std::vector<SaveSection> m_sections;
};

/// The sections as the text of a save file, headed by _comment as a comment line. False, with the
/// reason, when a section has no name or is named twice, a record lies outside any section, or a
/// value cannot be written (FormatRecord).
[[nodiscard]] bool FormatSave(std::span<const SaveSection> _sections, std::string_view _comment, std::string& _outText,
                              std::string& _outError);

/// The sections of a save file's text. Refused, with the reason: anything but a version 1 save,
/// records outside a section, a section named twice, and a file without its end record or whose end
/// record does not count what came before it, which is what a file cut short looks like.
[[nodiscard]] bool ParseSave(std::string_view _text, std::vector<SaveSection>& _outSections, std::string& _outError);

/// Writes the text beside the file first and then moves it over the file in one step, so that the
/// previous save stays whole until the new one is complete on disk. Creates the directory.
[[nodiscard]] bool WriteSaveFile(const std::filesystem::path& _path, std::string_view _text, std::string& _outError);

/// The section of that name, or null.
[[nodiscard]] const SaveSection* FindSection(std::span<const SaveSection> _sections, std::string_view _name) noexcept;

} // namespace Neuron
