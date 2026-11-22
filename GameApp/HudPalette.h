#pragma once

#include "HudBatch.h"

#include <cstdint>

namespace Outpost
{

// The C2 palette: dark translucent panels, cyan accents, amber and red for damage.
// HUD blending happens in linear space into an sRGB target, so a fill that should hide the scene
// has to be nearly opaque: a lit rock behind a 96% fill still shows through clearly once the
// result is sRGB-encoded.
inline constexpr std::uint32_t PANEL_FILL = Neuron::Rgba(7, 17, 25, 252);
inline constexpr std::uint32_t PANEL_FILL_DEEP = Neuron::Rgba(4, 10, 16, 255);
inline constexpr std::uint32_t PANEL_EDGE = Neuron::Rgba(38, 196, 214, 210);
inline constexpr std::uint32_t PANEL_EDGE_DIM = Neuron::Rgba(30, 110, 128, 170);
inline constexpr std::uint32_t ACCENT = Neuron::Rgba(64, 226, 236);
inline constexpr std::uint32_t ACCENT_SOFT = Neuron::Rgba(64, 226, 236, 70);
inline constexpr std::uint32_t TEXT = Neuron::Rgba(226, 236, 240);
inline constexpr std::uint32_t TEXT_DIM = Neuron::Rgba(136, 162, 172);
inline constexpr std::uint32_t AMBER = Neuron::Rgba(255, 172, 44);
inline constexpr std::uint32_t RED = Neuron::Rgba(240, 72, 58);
inline constexpr std::uint32_t GREEN = Neuron::Rgba(76, 232, 112);
inline constexpr std::uint32_t BAR_TRACK = Neuron::Rgba(30, 52, 62, 220);

// The UI sprites are authored at twice the 1080-line reference (Assets/Ui/Sprites.txt, ADR-023),
// so at HUD scale 1 one texel covers half a pixel.
inline constexpr float SPRITE_PIXELS_PER_TEXEL = 0.5f;

} // namespace Outpost
