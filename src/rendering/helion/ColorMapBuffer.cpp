/*
** ColorMapBuffer.cpp
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
*/

#include <algorithm>
#include <cstring>
#include "filesystem.h"
#include "r_data/colormaps.h"
#include "ColorMapBuffer.h"

namespace Helion::Render::ColorMapBuffer
{

const uint8_t* ColorMapData::IndexLayer(int index) const
{
	if (index >= LayerCount())
		return Indices.data();
	return Indices.data() + (size_t)index * NumColors;
}

static int WriteColorMap(int index, const std::vector<uint8_t>& palettes, const ColorMapData& colormap, std::vector<float>& buffer)
{
	size_t startOffset = (size_t)index * ColorMapSize;
	size_t offset = startOffset;
	int paletteCount = (int)(palettes.size() / (NumColors * 3));
	for (int paletteIndex = 0; paletteIndex < paletteCount && paletteIndex < NumPalettes; paletteIndex++)
	{
		const uint8_t* palette = palettes.data() + (size_t)paletteIndex * NumColors * 3;
		for (int colormapIndex = 0; colormapIndex < NumLayers; colormapIndex++)
		{
			const uint8_t* layer = colormap.IndexLayer(colormapIndex);
			for (int i = 0; i < NumColors; i++)
			{
				const uint8_t* color = palette + (size_t)layer[i] * 3;
				buffer[offset++] = color[0] / 255.0f;
				buffer[offset++] = color[1] / 255.0f;
				buffer[offset++] = color[2] / 255.0f;
				offset++;
			}
		}
	}

	return (int)offset;
}

std::vector<float> CreateRgba(const std::vector<uint8_t>& palette, const std::vector<ColorMapData>& colormaps)
{
	std::vector<float> buffer((size_t)ColorMapSize * colormaps.size(), 0.0f);

	for (size_t i = 0; i < colormaps.size(); i++)
	{
		if (colormaps[i].Indices.size() < NumColors)
			continue;

		WriteColorMap((int)i, palette, colormaps[i], buffer);
	}

	return buffer;
}

static std::vector<uint8_t> ReadLump(int lump)
{
	std::vector<uint8_t> data;
	if (lump < 0)
		return data;

	auto file = fileSystem.ReadFile(lump);
	const uint8_t* bytes = file.bytes();
	data.assign(bytes, bytes + file.size());
	return data;
}

std::vector<float> CreateRgba()
{
	std::vector<uint8_t> palette = ReadLump(fileSystem.CheckNumForName("PLAYPAL"));

	// Colormap 0 is the game colormap (v_palette.cpp InitPalette: COLORMAP, or the first one of the colormaps namespace).
	int lump = fileSystem.CheckNumForName("COLORMAP");
	if (lump == -1)
		lump = fileSystem.CheckNumForName("COLORMAP", FileSys::ns_colormaps);

	ColorMapData defaultMap;
	defaultMap.Indices = ReadLump(lump);
	if (defaultMap.Indices.size() < NumColors)
	{
		// No colormap: identity, no light diminishing.
		defaultMap.Indices.resize(NumColors);
		for (int i = 0; i < NumColors; i++)
			defaultMap.Indices[i] = (uint8_t)i;
	}

	std::vector<ColorMapData> colormaps;
	colormaps.push_back(defaultMap);

	// fakecmaps[0] is the default map. A Boom colormap without enough data keeps the default one.
	for (unsigned i = 1; i < fakecmaps.Size(); i++)
	{
		ColorMapData data;
		data.Indices = ReadLump(fakecmaps[i].lump);
		if (data.Indices.size() < NumColors)
			data = defaultMap;
		colormaps.push_back(std::move(data));
	}

	return CreateRgba(palette, colormaps);
}

}
