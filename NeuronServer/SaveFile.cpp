#include "SaveFile.h"

#include "WindowsSdk.h"

#include "FileSys.h"

#include <algorithm>
#include <span>

namespace Neuron
{

namespace
{

constexpr std::string_view HEADER_KEYWORD = "save";
constexpr std::string_view SECTION_KEYWORD = "section";
constexpr std::string_view END_KEYWORD = "end";

[[nodiscard]] std::string LineError(const Record& _record, std::string_view _message)
{
  return "line " + std::to_string(_record.line) + ": " + std::string(_message);
}

} // namespace

void SaveWriter::BeginSection(std::string_view _name)
{
  m_sections.push_back(SaveSection{std::string(_name), {}});
}

SaveWriter& SaveWriter::Add(std::string_view _keyword)
{
  if (m_sections.empty())
  {
    m_sections.push_back(SaveSection{}); // nameless: FormatSave refuses it
  }
  m_sections.back().records.push_back(Record{std::string(_keyword), {}, 0});
  return *this;
}

SaveWriter& SaveWriter::Field(std::string_view _key, std::string_view _value)
{
  if (m_sections.empty() || m_sections.back().records.empty())
  {
    Add("");
  }
  m_sections.back().records.back().fields.push_back(RecordField{std::string(_key), std::string(_value)});
  return *this;
}

void SaveWriter::AddSection(const SaveSection& _section)
{
  m_sections.push_back(_section);
}

const SaveSection* FindSection(std::span<const SaveSection> _sections, std::string_view _name) noexcept
{
  const auto found = std::ranges::find(_sections, _name, &SaveSection::name);
  return found != _sections.end() ? &*found : nullptr;
}

bool FormatSave(std::span<const SaveSection> _sections, std::string_view _comment, std::string& _outText, std::string& _outError)
{
  std::string text;
  if (!_comment.empty())
  {
    if (_comment.find_first_of("\r\n") != std::string_view::npos)
    {
      _outError = "the comment holds a line break";
      return false;
    }
    text += "# ";
    text += _comment;
    text += "\r\n";
  }
  text += std::string(HEADER_KEYWORD) + " version=" + std::to_string(SAVE_FORMAT_VERSION) + "\r\n";
  std::size_t records = 0;
  std::string line;
  for (std::size_t i = 0; i < _sections.size(); ++i)
  {
    const SaveSection& section = _sections[i];
    if (section.name.empty())
    {
      _outError = "a section has no name";
      return false;
    }
    if (FindSection(_sections.subspan(0, i), section.name) != nullptr)
    {
      _outError = "section '" + section.name + "' is written twice";
      return false;
    }
    if (!FormatRecord(Record{std::string(SECTION_KEYWORD), {RecordField{"name", section.name}}, 0}, line))
    {
      _outError = "section name '" + section.name + "' cannot be written";
      return false;
    }
    text += line + "\r\n";
    for (const Record& record : section.records)
    {
      if (record.keyword == HEADER_KEYWORD || record.keyword == SECTION_KEYWORD || record.keyword == END_KEYWORD)
      {
        _outError = "section '" + section.name + "' uses the reserved keyword '" + record.keyword + "'";
        return false;
      }
      if (!FormatRecord(record, line))
      {
        _outError = "section '" + section.name + "': a '" + record.keyword + "' record cannot be written";
        return false;
      }
      text += line + "\r\n";
      ++records;
    }
  }
  text += std::string(END_KEYWORD) + " sections=" + std::to_string(_sections.size()) + " records=" + std::to_string(records) + "\r\n";
  _outText = std::move(text);
  return true;
}

bool ParseSave(std::string_view _text, std::vector<SaveSection>& _outSections, std::string& _outError)
{
  std::vector<Record> records;
  if (!ParseRecords(_text, records, _outError))
  {
    // A file cut inside a quoted value does not even read as records; say what most likely happened.
    if (_text.find("\nend ") == std::string_view::npos)
    {
      _outError = "the save is incomplete: it ends before its end record (" + _outError + ")";
    }
    return false;
  }
  if (records.empty() || records.front().keyword != HEADER_KEYWORD)
  {
    _outError = "not a save file: it does not begin with a 'save' record";
    return false;
  }
  int version = 0;
  if (!records.front().GetInt("version", version))
  {
    _outError = LineError(records.front(), "the 'save' record has no version");
    return false;
  }
  if (version != SAVE_FORMAT_VERSION)
  {
    _outError =
      "the save is format version " + std::to_string(version) + "; this game reads version " + std::to_string(SAVE_FORMAT_VERSION);
    return false;
  }
  std::vector<SaveSection> sections;
  std::size_t contentRecords = 0;
  bool ended = false;
  for (std::size_t i = 1; i < records.size(); ++i)
  {
    const Record& record = records[i];
    if (ended)
    {
      _outError = LineError(record, "a record after the end record");
      return false;
    }
    if (record.keyword == END_KEYWORD)
    {
      std::uint64_t sectionCount = 0;
      std::uint64_t recordCount = 0;
      const std::string* sectionsText = record.Find("sections");
      const std::string* recordsText = record.Find("records");
      if (sectionsText == nullptr || recordsText == nullptr || !ParseUInt64(*sectionsText, sectionCount) ||
          !ParseUInt64(*recordsText, recordCount))
      {
        _outError = LineError(record, "the end record must count its sections and records");
        return false;
      }
      if (sectionCount != sections.size() || recordCount != contentRecords)
      {
        _outError = "the save is incomplete: its end record counts " + std::to_string(sectionCount) + " section(s) and " +
                    std::to_string(recordCount) + " record(s), the file holds " + std::to_string(sections.size()) + " and " +
                    std::to_string(contentRecords);
        return false;
      }
      ended = true;
    }
    else if (record.keyword == SECTION_KEYWORD)
    {
      std::string name;
      if (!record.GetString("name", name) || name.empty())
      {
        _outError = LineError(record, "a section without a name");
        return false;
      }
      if (FindSection(sections, name) != nullptr)
      {
        _outError = LineError(record, "section '" + name + "' appears twice");
        return false;
      }
      sections.push_back(SaveSection{std::move(name), {}});
    }
    else if (record.keyword == HEADER_KEYWORD)
    {
      _outError = LineError(record, "a second 'save' record");
      return false;
    }
    else if (sections.empty())
    {
      _outError = LineError(record, "a '" + record.keyword + "' record before the first section");
      return false;
    }
    else
    {
      sections.back().records.push_back(record);
      ++contentRecords;
    }
  }
  if (!ended)
  {
    _outError = "the save is incomplete: it ends before its end record";
    return false;
  }
  _outSections = std::move(sections);
  return true;
}

bool WriteSaveFile(const std::filesystem::path& _path, std::string_view _text, std::string& _outError)
{
  std::filesystem::path partial = _path;
  partial += L".partial";
  const auto* bytes = reinterpret_cast<const std::uint8_t*>(_text.data());
  if (!WriteFileBytes(partial, std::span<const std::uint8_t>(bytes, _text.size())))
  {
    _outError = "cannot write " + partial.string();
    return false;
  }
  // The new file reaches the disk before it replaces the old one, and the replacement is one step:
  // a crash leaves either the previous save or the new one, never part of either.
  const HANDLE file = CreateFileW(partial.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE)
  {
    _outError = "cannot reopen " + partial.string() + " to flush it";
    return false;
  }
  const BOOL flushed = FlushFileBuffers(file);
  CloseHandle(file);
  if (flushed == FALSE)
  {
    _outError = "cannot flush " + partial.string();
    return false;
  }
  if (MoveFileExW(partial.c_str(), _path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == FALSE)
  {
    _outError = "cannot replace " + _path.string() + " (error " + std::to_string(GetLastError()) + ")";
    return false;
  }
  return true;
}

} // namespace Neuron
