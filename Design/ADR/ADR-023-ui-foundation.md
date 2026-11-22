# ADR-023 — The UI foundation: DDS atlases, the sprite manifest and the HUD batch

**Status:** accepted · **Date:** 2026-11-22

## Context

[GameplayUiProposal.md](../GameplayUiProposal.md) sets the direction for the game's UI, and its first
step comes before the docked view of slice 2 (plan task 2.5a, the owner's decision of 2026-11-22).
Shipped UI art is DDS. Until now the HUD could not show any:

- **`HudBatch` drew from one texture**, the GDI-built R8 font atlas. Solid shapes sampled a white
  block in it; `HudPS.hlsl` read one scalar and tinted it.
- **`Renderer::SetHudAtlas` uploaded R8 only**, and `GraphicsDevice::CreateStaticTexture` copied rows
  as `width × bytesPerPixel`, which is wrong for a block-compressed format.
- **Nothing read DDS**, and R14 rules out DirectXTex, DirectXTK, `d3dx12.h` and the Agility SDK.
- **Controls had no shared states.** The tactical HUD's action buttons were drawn shapes with no
  notion of hover, focus, disabled-with-reason or a pending reply.

## Decision

### The DDS subset (`NeuronCore/DdsFile`)

- **Read by hand, from memory**: the `DDS ` magic, the 124-byte header and the DX10 extension
  header are required. A legacy header without DX10 is refused.
- **A 2D texture, array size 1, exactly one mip level.** Cube maps (either flag), volumes, arrays and
  mip chains are refused. Sides are 1 to 16,384.
- **Five formats**: `R8_UNORM`, `R8G8B8A8_UNORM`, `R8G8B8A8_UNORM_SRGB`, `BC7_UNORM`, `BC7_UNORM_SRGB`.
  BC7 is validated and handed to the GPU, never decoded on the CPU.
- **The payload length is checked exactly**: rows tightly packed, BC7 as 16-byte 4×4 blocks with
  the sides rounded up to whole blocks. A short file is "truncated", a long one "over-long".
- **Alpha is straight.** Colour formats must declare straight or opaque alpha in the DX10 header's
  `miscFlags2`; premultiplied, custom and undeclared are refused. R8 is coverage and its alpha
  mode is not read.
- **Every refusal returns a reason.** `WriteDds` writes exactly the subset `ParseDds` reads, for tests.

### Upload (`NeuronClient/Renderer`, `GraphicsDevice`)

- **`CreateStaticTexture` takes the payload and lets `GetCopyableFootprints` lay it out**: the source
  row is the footprint's unpadded row size, so BC7 block rows and R8 rows take the same path.
- **`Renderer::SetHudTexture(slot, image, error)`** checks `CheckFeatureSupport` for 2D sampling of
  the format, uploads to a default-heap texture and writes the slot's SRV.
- **Four HUD texture slots**: slot 0 is the font atlas, slots 1 to 3 the sprite atlases. The root
  signature's descriptor table grows from two SRVs to five (shadow map, then the four slots). An
  unfilled slot holds a null SRV, which samples as zero.

### The manifest (`Assets/Ui/Sprites.txt`, `NeuronClient/SpriteSheet`)

- **Records in the format of `RecordFile.h`** (ADR-009's syntax):
  `atlas name= file= kind=mask|color`, and `sprite name= atlas= rect=x,y,w,h [slice=l,t,r,b]`.
- **Checked when it loads** (R24): an unknown keyword or key, a repeated key, a missing field, an
  unknown atlas, a rect outside its atlas, slices larger than their rect, a duplicate name and
  more than three atlases are each an error naming the file and line. Atlas sizes come from the
  DDS files themselves; a mask atlas must be `R8_UNORM`, a colour atlas an `_SRGB` format.
- **Runtime code names sprites** (`Panel.Frame`, `Button.Hover`, `Icon.Repair`, `Status.Warning`),
  never rects. The client loads the manifest from the asset root, as every other content file.

### The art (`Tools/BuildUiAtlases.py`)

- **Drawn procedurally and deterministically** by a standard-library script from signed distance
  fields, one texel of antialiasing. It writes the two DDS files and the manifest from one table,
  so they cannot disagree; `--check` fails when the committed files differ from what it draws.
- **`UiMasks.dds`**, 512 × 512 `R8_UNORM`: seven icons (launch, repair, build, yard, fleet, services,
  warning), the tintable `Panel.Fill` and `Panel.Edge`, and `Focus.Ring`.
- **`UiFrames.dds`**, 256 × 256 `R8G8B8A8_UNORM_SRGB`, straight alpha: `Button.<State>` for all eight
  control states, `Panel.Frame` and `Panel.Inset`. Transparent texels carry the colour of the
  nearest drawn texel in their sprite's cell, so linear filtering never pulls black into a rim.
- **Rects on 4-texel boundaries with an 8-texel gutter**, which the script asserts and a test checks.
- **Authored at twice the 1080-line reference**; the HUD draws sprites at half its own scale
  (`SPRITE_PIXELS_PER_TEXEL`), so they stay sharp up to 2160 lines.

### Sampling and blending

- **One mip, LOD 0, linear clamp** — the static sampler the HUD already had.
- **sRGB decoding for colour art** comes from the texture format; **masks are linear coverage**.
  Blending stays straight alpha, `SRC_ALPHA / INV_SRC_ALPHA`, in linear space into the sRGB target.

### The HUD batch

- **The vertex keeps its 24 bytes.** Its unused padding word becomes `sampling`: the texture slot in
  the low byte and `HUD_SAMPLE_COLOR` for RGBA art. `HudPS.hlsl` indexes `g_hudTextures` with
  `NonUniformResourceIndex` and either multiplies the tint by the texel (colour) or scales the
  tint's alpha by its red channel (text, solid fills, masks).
- **Still one draw per layer**, so text, shapes and sprites land in exactly the order they were
  added, and the back and front layers around the voxel thumbnails are unchanged.
- **`HudBatch::Image`** stretches a sprite over a rectangle; **`HudBatch::NineSlice`** draws nine quads
  with the corners at native size times a scale, shrinking them in proportion when the rectangle
  is too small, pixel-snapped.

### Controls (`NeuronClient/ControlState`, `ControlFocus`, `ButtonSkin`)

- **`ControlState`**: idle, hover, pressed, selected, disabled, pending, confirmed, refused.
- **`ControlFocus`** lays out controls per frame and turns pointer and keyboard input into hover,
  press, click and focus. A disabled control is never clicked; a press on it is a refusal, and
  its reason is kept. The authority's pending, confirmed and refused win over the pointer.
- **`ButtonSkin`** draws a button from one nine-slice per state with an icon, a label and a reason
  line. A disabled or refused button always shows its reason; any other only while hovered,
  pressed or focused. None of it knows a game concept (R9).

### The reference integration

The tactical HUD's panels are nine-slices: the standard panel is `Panel.Frame` as authored, other
colour pairs are `Panel.Fill` and `Panel.Edge` tinted, the action buttons are `ButtonSkin` buttons
(repair and build with mask icons) and the build slots are `Panel.Inset`. No position changes. Without
a manifest it draws the old polygons, which is what its earlier layout tests still exercise.

### Why the atlases ship uncompressed

The proposal allows `R8G8B8A8_UNORM_SRGB` where an atlas does not survive BC7 review. No BC7 encoder
is available to the build: R14 keeps DirectXTex out of the executable, `texconv` is neither installed
nor pinned here, and a BC7 encoder written for this tool would be a project of its own. At these sizes
it is not worth one yet: the frames atlas is 256 KiB uncompressed and would be 64 KiB as BC7. The
reader and the upload keep BC7, tested on synthetic files, so a compressed atlas is a tool change.

## Measurements

Measured on 2026-11-22 on the development machine, from the files the tool wrote:

| File | Bytes on disk | GPU payload, one mip |
|---|---:|---:|
| `UiMasks.dds`, 512 × 512 R8 | 262,292 | 262,144 (256 KiB) |
| `UiFrames.dds`, 256 × 256 RGBA8 sRGB | 262,292 | 262,144 (256 KiB) |

The authored-art payload is **0.5 MiB** of the proposal's 4 MiB budget, before D3D allocation alignment.
Each file is the payload plus the 148-byte header (`ls -l`). `python Tools/BuildUiAtlases.py` ran in
0.44 s (`time`), and `--check` reproduced the committed bytes on a second run.

The HUD's GPU cost with sprites was not measured; the scripted WARP smoke run drew the re-skinned HUD
with zero debug-layer errors or warnings.

## What this forecloses

- **Premultiplied art in the HUD.** A premultiplied atlas would need its own blend state and a flag
  in `sampling`; until then the reader refuses it.
- **Mipped UI art.** Screen-space atlases have one level. World-space art, if it comes, uses its own
  assets and mip policy.
- **More than three sprite atlases** without growing `Renderer::HUD_TEXTURE_COUNT`, the descriptor
  table and `HUD_TEXTURE_COUNT` in `Common.hlsli` together (a static assert ties the C++ to the
  manifest's limit).
- **Rects in code.** A sprite is reached by name through the manifest, so packing can change freely.

Left open:

- **Pointer and keyboard wiring.** `ControlFocus` is not yet fed by `TacticalClient`; the docked view
  (task 2.5) is the first screen with controls that do something, and connects it with the command
  layer, including Escape and the suppression of flight input while a menu has focus.
- **BC7 encoding** of the colour atlas, when an encoder is approved or written.
- **A UI-scale setting** separate from the window height; today the HUD scale follows the height.
