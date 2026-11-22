#include "SpriteSheet.h"

#include "FileSys.h"
#include "RecordFile.h"
#include "Renderer.h"

#include <algorithm>
#include <array>
#include <initializer_list>

namespace Neuron
{

static_assert(Renderer::HUD_TEXTURE_COUNT == 1 + SpriteSheet::MAX_ATLASES, "one HUD texture slot per atlas, after the font atlas");

namespace
{

[[nodiscard]] std::string LineError(std::string_view _fileName, int _line, std::string_view _message)
{
  return std::string(_fileName) + " line " + std::to_string(_line) + ": " + std::string(_message);
}

/// Exactly four comma-separated integers, as in rect=8,8,64,64.
[[nodiscard]] bool ParseIntQuad(std::string_view _text, std::array<std::int32_t, 4>& _out) noexcept
{
  for (std::size_t i = 0; i < _out.size(); ++i)
  {
    const std::size_t comma = _text.find(',');
    const bool last = i + 1 == _out.size();
    if ((comma == std::string_view::npos) != last)
    {
      return false;
    }
    int value = 0;
    if (!ParseInt(last ? _text : _text.substr(0, comma), value))
    {
      return false;
    }
    _out[i] = value;
    if (!last)
    {
      _text.remove_prefix(comma + 1);
    }
  }
  return true;
}

/// Refuses a key the record's keyword does not define, so that a misspelt key is not silently ignored.
[[nodiscard]] bool CheckKeys(const Record& _record, std::initializer_list<std::string_view> _known, std::string_view _fileName,
                             std::string& _outError)
{
  for (const RecordField& field : _record.fields)
  {
    if (std::find(_known.begin(), _known.end(), field.key) == _known.end())
    {
      _outError = LineError(_fileName, _record.line, _record.keyword + " has no key '" + field.key + "'");
      return false;
    }
    if (std::count_if(_record.fields.begin(), _record.fields.end(),
                      [&field](const RecordField& _other) { return _other.key == field.key; }) > 1)
    {
      _outError = LineError(_fileName, _record.line, "key '" + field.key + "' is given twice");
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool RequireString(const Record& _record, std::string_view _key, std::string_view _fileName, std::string& _out,
                                 std::string& _outError)
{
  if (!_record.GetString(_key, _out) || _out.empty())
  {
    _outError = LineError(_fileName, _record.line, _record.keyword + " needs " + std::string(_key) + "=");
    return false;
  }
  return true;
}

[[nodiscard]] bool ParseAtlas(const Record& _record, std::string_view _fileName, SpriteAtlas& _out, std::string& _outError)
{
  if (!CheckKeys(_record, {"name", "file", "kind"}, _fileName, _outError) ||
      !RequireString(_record, "name", _fileName, _out.name, _outError) || !RequireString(_record, "file", _fileName, _out.file, _outError))
  {
    return false;
  }
  std::string kind;
  if (!RequireString(_record, "kind", _fileName, kind, _outError))
  {
    return false;
  }
  if (kind == "mask")
  {
    _out.kind = SpriteKind::Mask;
  }
  else if (kind == "color")
  {
    _out.kind = SpriteKind::Color;
  }
  else
  {
    _outError = LineError(_fileName, _record.line, "atlas kind '" + kind + "' is neither mask nor color");
    return false;
  }
  _out.line = _record.line;
  return true;
}

} // namespace

bool SpriteSheet::Parse(std::string_view _text, std::string_view _fileName, const AtlasSizeQuery& _sizeOf, SpriteSheet& _outSheet,
                        std::string& _outError)
{
  std::vector<Record> records;
  if (!ParseRecords(_text, records, _outError))
  {
    _outError = std::string(_fileName) + " " + _outError;
    return false;
  }
  SpriteSheet sheet;
  // Atlases first, so that a sprite may come before the atlas it names.
  for (const Record& record : records)
  {
    if (record.keyword == "sprite")
    {
      continue;
    }
    if (record.keyword != "atlas")
    {
      _outError = LineError(_fileName, record.line, "unknown record '" + record.keyword + "'");
      return false;
    }
    SpriteAtlas atlas;
    if (!ParseAtlas(record, _fileName, atlas, _outError))
    {
      return false;
    }
    for (const SpriteAtlas& other : sheet.m_atlases)
    {
      if (other.name == atlas.name)
      {
        _outError =
          LineError(_fileName, record.line, "atlas '" + atlas.name + "' is already declared on line " + std::to_string(other.line));
        return false;
      }
    }
    if (sheet.m_atlases.size() >= MAX_ATLASES)
    {
      _outError = LineError(_fileName, record.line, "more than " + std::to_string(MAX_ATLASES) + " atlases");
      return false;
    }
    std::string sizeError;
    if (!_sizeOf || !_sizeOf(atlas, atlas.widthTexels, atlas.heightTexels, sizeError))
    {
      _outError = LineError(_fileName, record.line, "atlas '" + atlas.name + "': " + (sizeError.empty() ? "size unknown" : sizeError));
      return false;
    }
    if (atlas.widthTexels == 0 || atlas.heightTexels == 0)
    {
      _outError = LineError(_fileName, record.line, "atlas '" + atlas.name + "' is empty");
      return false;
    }
    atlas.textureSlot = static_cast<std::uint32_t>(sheet.m_atlases.size()) + 1;
    sheet.m_atlases.push_back(std::move(atlas));
  }

  for (const Record& record : records)
  {
    if (record.keyword != "sprite")
    {
      continue;
    }
    if (!CheckKeys(record, {"name", "atlas", "rect", "slice"}, _fileName, _outError))
    {
      return false;
    }
    Sprite sprite;
    sprite.line = record.line;
    std::string atlasName;
    std::string rectText;
    if (!RequireString(record, "name", _fileName, sprite.name, _outError) ||
        !RequireString(record, "atlas", _fileName, atlasName, _outError) || !RequireString(record, "rect", _fileName, rectText, _outError))
    {
      return false;
    }
    if (const Sprite* existing = sheet.Find(sprite.name); existing != nullptr)
    {
      _outError =
        LineError(_fileName, record.line, "sprite '" + sprite.name + "' is already declared on line " + std::to_string(existing->line));
      return false;
    }
    const auto atlas = std::find_if(sheet.m_atlases.begin(), sheet.m_atlases.end(),
                                    [&atlasName](const SpriteAtlas& _atlas) { return _atlas.name == atlasName; });
    if (atlas == sheet.m_atlases.end())
    {
      _outError = LineError(_fileName, record.line, "sprite '" + sprite.name + "' names unknown atlas '" + atlasName + "'");
      return false;
    }
    std::array<std::int32_t, 4> rect{};
    if (!ParseIntQuad(rectText, rect))
    {
      _outError = LineError(_fileName, record.line, "rect= needs four integers x,y,width,height");
      return false;
    }
    sprite.rect = TexelRect{rect[0], rect[1], rect[2], rect[3]};
    const auto atlasWidth = static_cast<std::int64_t>(atlas->widthTexels);
    const auto atlasHeight = static_cast<std::int64_t>(atlas->heightTexels);
    if (sprite.rect.x < 0 || sprite.rect.y < 0 || sprite.rect.width <= 0 || sprite.rect.height <= 0 ||
        static_cast<std::int64_t>(sprite.rect.x) + sprite.rect.width > atlasWidth ||
        static_cast<std::int64_t>(sprite.rect.y) + sprite.rect.height > atlasHeight)
    {
      _outError = LineError(_fileName, record.line,
                            "sprite '" + sprite.name + "' rect " + rectText + " lies outside atlas '" + atlas->name + "' (" +
                              std::to_string(atlasWidth) + " x " + std::to_string(atlasHeight) + ")");
      return false;
    }
    if (const std::string* sliceText = record.Find("slice"); sliceText != nullptr)
    {
      std::array<std::int32_t, 4> slices{};
      if (!ParseIntQuad(*sliceText, slices))
      {
        _outError = LineError(_fileName, record.line, "slice= needs four integers left,top,right,bottom");
        return false;
      }
      sprite.slices = NineSliceMargins{slices[0], slices[1], slices[2], slices[3]};
      const NineSliceMargins& m = sprite.slices;
      if (m.left < 0 || m.top < 0 || m.right < 0 || m.bottom < 0 || m.left + m.right > sprite.rect.width ||
          m.top + m.bottom > sprite.rect.height)
      {
        _outError = LineError(_fileName, record.line, "sprite '" + sprite.name + "' slices " + *sliceText + " are larger than its rect");
        return false;
      }
      sprite.nineSlice = true;
    }
    sprite.atlas = static_cast<std::uint32_t>(atlas - sheet.m_atlases.begin());
    sprite.textureSlot = atlas->textureSlot;
    sprite.kind = atlas->kind;
    sprite.texelU = 1.0f / static_cast<float>(atlas->widthTexels);
    sprite.texelV = 1.0f / static_cast<float>(atlas->heightTexels);
    sprite.u0 = static_cast<float>(sprite.rect.x) * sprite.texelU;
    sprite.v0 = static_cast<float>(sprite.rect.y) * sprite.texelV;
    sprite.u1 = static_cast<float>(sprite.rect.x + sprite.rect.width) * sprite.texelU;
    sprite.v1 = static_cast<float>(sprite.rect.y + sprite.rect.height) * sprite.texelV;
    sheet.m_sprites.push_back(std::move(sprite));
  }
  _outSheet = std::move(sheet);
  return true;
}

const Sprite* SpriteSheet::Find(std::string_view _name) const noexcept
{
  for (const Sprite& sprite : m_sprites)
  {
    if (sprite.name == _name)
    {
      return &sprite;
    }
  }
  return nullptr;
}

bool LoadSpriteSheet(const std::filesystem::path& _manifest, SpriteSheet& _outSheet, std::vector<DdsImage>& _outImages,
                     std::string& _outError)
{
  std::string text;
  if (!ReadTextFile(_manifest, text))
  {
    _outError = "cannot read " + _manifest.string();
    return false;
  }
  const std::string fileName = _manifest.filename().string();
  const std::filesystem::path directory = _manifest.parent_path();
  std::vector<DdsImage> images;
  auto readAtlas =
    [&directory, &images](const SpriteAtlas& _atlas, std::uint32_t& _outWidth, std::uint32_t& _outHeight, std::string& _error)
  {
    std::vector<std::uint8_t> bytes;
    if (!ReadFileBytes(directory / _atlas.file, bytes))
    {
      _error = "cannot read " + _atlas.file;
      return false;
    }
    DdsImage image;
    std::string reason;
    if (!ParseDds(bytes, image, reason))
    {
      _error = _atlas.file + ": " + reason;
      return false;
    }
    if (_atlas.kind == SpriteKind::Mask && image.format != DXGI_FORMAT_R8_UNORM)
    {
      _error = _atlas.file + " is " + DdsFormatName(image.format) + ", but a mask atlas is R8_UNORM coverage";
      return false;
    }
    if (_atlas.kind == SpriteKind::Color && !IsDdsSrgbFormat(image.format))
    {
      _error = _atlas.file + " is " + DdsFormatName(image.format) + ", but colour art is decoded from sRGB (an _SRGB format)";
      return false;
    }
    _outWidth = image.width;
    _outHeight = image.height;
    images.push_back(std::move(image));
    return true;
  };
  SpriteSheet sheet;
  if (!SpriteSheet::Parse(text, fileName, readAtlas, sheet, _outError))
  {
    return false;
  }
  _outSheet = std::move(sheet);
  _outImages = std::move(images);
  return true;
}

} // namespace Neuron
