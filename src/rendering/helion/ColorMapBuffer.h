/*
** ColorMapBuffer.h
**
** Port of Helion.Render.ColorMapBuffer.
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** Helion builds the buffer from Graphics.Palettes.Palette (PLAYPAL layers) and its Colormap list.
** UZDoom keeps neither: the data are the raw PLAYPAL lump, the COLORMAP lump (colormap 0) and the
** Boom colormap lumps of the colormaps namespace (fakecmaps[i].lump of r_data/colormaps.h; the
** shader colormap index of a Boom colormap is its fakecmaps index, like Helion's Colormap.Index).
** The overload without arguments gathers them. The buffer is indexed with in-game palette indices:
** in-game index k equals the original PLAYPAL index for k > 0 (GPalette.Remap only replaces the
** duplicate of the transparent entry 0), so the lookup is PLAYPAL[palette][COLORMAP[layer][k]].
** Layer 32 of colormap 0 is the invulnerability map the emulation reads (FragFunction.cs:425).
**
*/

#pragma once

#include <cstdint>
#include <vector>

namespace Helion::Render
{

namespace ColorMapBuffer
{

// Graphics.Palettes.Colormap.NumColors / NumLayers, Palette.NumPalettes
constexpr int NumColors = 256;
constexpr int NumLayers = 34;
constexpr int NumPalettes = 14;
// GLBufferTextureStorage<float>.FourComponentLength
constexpr int FourComponentLength = 4;

constexpr int LayerSize = NumColors * NumLayers * FourComponentLength;
constexpr int ColorMapSize = LayerSize * NumPalettes;

// A colormap: NumLayers layers of NumColors palette indices. Fewer layers are allowed, the missing
// layers read layer 0 (Colormap.IndexLayer).
struct ColorMapData
{
	std::vector<uint8_t> Indices;

	int LayerCount() const { return (int)(Indices.size() / NumColors); }
	const uint8_t* IndexLayer(int index) const;
};

// palette: PLAYPAL, any number of 768 byte layers (at most NumPalettes are used).
// The result has ColorMapSize floats per colormap, rgba, the alpha component is not written.
std::vector<float> CreateRgba(const std::vector<uint8_t>& palette, const std::vector<ColorMapData>& colormaps);

// UZDoom: the PLAYPAL, COLORMAP and Boom colormap lumps of the loaded game.
std::vector<float> CreateRgba();

}

}
