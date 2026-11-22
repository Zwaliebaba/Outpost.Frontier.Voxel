#pragma once

#include "DdsFile.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Neuron
{

/// How a sprite atlas is sampled (ADR-023): a mask is R8 coverage that tints with the draw colour;
/// colour art is sRGB RGBA with straight alpha, multiplied by the draw colour.
enum class SpriteKind : std::uint8_t
{
  Mask,
  Color
};

/// A rectangle of an atlas, in texels from its top-left corner.
struct TexelRect
{
  std::int32_t x = 0;
  std::int32_t y = 0;
  std::int32_t width = 0;
  std::int32_t height = 0;
};

/// Nine-slice margins in texels: the corners keep their size, the edges stretch one way and the
/// centre both ways.
struct NineSliceMargins
{
  std::int32_t left = 0;
  std::int32_t top = 0;
  std::int32_t right = 0;
  std::int32_t bottom = 0;
};

struct SpriteAtlas
{
  std::string name;
  std::string file; // relative to the manifest's directory
  SpriteKind kind = SpriteKind::Mask;
  std::uint32_t widthTexels = 0;
  std::uint32_t heightTexels = 0;
  std::uint32_t textureSlot = 0; // the HUD texture slot it is uploaded to: 1 + its index
  int line = 0;
};

/// A named region of an atlas, with everything the HUD batch needs to draw it.
struct Sprite
{
  std::string name;
  std::uint32_t atlas = 0; // index into SpriteSheet::Atlases()
  std::uint32_t textureSlot = 0;
  SpriteKind kind = SpriteKind::Mask;
  TexelRect rect;
  NineSliceMargins slices;
  bool nineSlice = false;
  float u0 = 0.0f; // rect edges in texture coordinates
  float v0 = 0.0f;
  float u1 = 0.0f;
  float v1 = 0.0f;
  float texelU = 0.0f; // one texel in texture coordinates, for the slice lines
  float texelV = 0.0f;
  int line = 0;
};

/// Reports an atlas's size in texels: (atlas, out width, out height, out error) -> whether it is known.
/// The loader reads it from the atlas's DDS file; tests supply it.
using AtlasSizeQuery = std::function<bool(const SpriteAtlas&, std::uint32_t&, std::uint32_t&, std::string&)>;

/// The named sprite manifest (ADR-023), in the record format of RecordFile.h:
///
///   atlas name=UiMasks file=UiMasks.dds kind=mask       # kind: mask | color
///   sprite name=Icon.Repair atlas=UiMasks rect=88,8,64,64
///   sprite name=Panel.Frame atlas=UiFrames rect=8,144,64,64 slice=20,20,20,20
///
/// Every reference is checked when the manifest loads (R24): an unknown keyword or key, a missing
/// field, an unknown atlas, a rect outside its atlas, slices larger than their rect and a duplicate
/// name are each an error naming the file and line. Runtime code refers to sprites by name only.
class SpriteSheet
{
public:
  /// Atlases beyond the HUD font atlas the renderer can bind (Renderer::HUD_TEXTURE_COUNT - 1).
  static constexpr std::uint32_t MAX_ATLASES = 3;

  [[nodiscard]] static bool Parse(std::string_view _text, std::string_view _fileName, const AtlasSizeQuery& _sizeOf, SpriteSheet& _outSheet,
                                  std::string& _outError);

  [[nodiscard]] const Sprite* Find(std::string_view _name) const noexcept;
  [[nodiscard]] std::span<const SpriteAtlas> Atlases() const noexcept
  {
    return m_atlases;
  }
  [[nodiscard]] std::span<const Sprite> Sprites() const noexcept
  {
    return m_sprites;
  }
  [[nodiscard]] bool Empty() const noexcept
  {
    return m_sprites.empty();
  }

private:
  std::vector<SpriteAtlas> m_atlases;
  std::vector<Sprite> m_sprites;
};

/// Loads a manifest and every atlas it names: each DDS is read and parsed (DdsFile.h), its format
/// checked against the atlas's kind (a mask is R8_UNORM, colour art an sRGB colour format), and
/// every sprite checked against the atlas's real size. _outImages holds one image per atlas, in
/// the order of SpriteSheet::Atlases(), ready for Renderer::SetHudTexture.
[[nodiscard]] bool LoadSpriteSheet(const std::filesystem::path& _manifest, SpriteSheet& _outSheet, std::vector<DdsImage>& _outImages,
                                   std::string& _outError);

} // namespace Neuron
