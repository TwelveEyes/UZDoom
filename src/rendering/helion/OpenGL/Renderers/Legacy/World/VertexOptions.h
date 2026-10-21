/*
** VertexOptions.h
**
** Port of Helion.Render.OpenGL.Renderers.Legacy.World.VertexOptions.
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

#pragma once

#include <bit>
#include <cstdint>
#include "UvFlags.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World
{

class VertexOptions
{
public:
	// When overrideLightIndex is non-zero then lighting uses index overrideLightIndex - 1
	static float PackSurface(int topLeft, float alpha, int addAlpha, int upper, int lower, int overrideLightIndex, int textureIndex)
	{
		int alphaByte = (int)(alpha * 255.0f);
		int packed = (alphaByte & 0xFF) | (topLeft << 8) | (addAlpha << 9) | (upper << 10) | (lower << 11) | (overrideLightIndex << 12) | ((textureIndex & 0xFFFFF) << 13);
		return std::bit_cast<float>(packed);
	}

	// UvFlags only required for walls
	static float PackRender(int lightIndex, int vertexLightLevel, UvFlags uvFlags = UvFlags::Normal)
	{
		// Helion: lightIndex 22 bits, uvFlags 2 bits, vertexLightLevel 8 bits.
		// UZDoom: relative side/plane light can be negative, so vertexLightLevel is 9 bit two's complement (-256..255): the low 8 bits
		// in bits 0-7 and the sign in bit 31, which leaves 21 bits for lightIndex (VertexFunction::ColorMapAndLightLevelSet unpacks it).
		int packed = ((lightIndex & 0x1FFFFF) << 10) | (((int)uvFlags & 0x3) << 8) | (vertexLightLevel & 0xFF) | (vertexLightLevel < 0 ? (int)0x80000000u : 0);
		return std::bit_cast<float>(packed);
	}

	static float LightLevelAdd(int mapId, int lightLevelAdd)
	{
		// Packs LightLevelAdd with MapId
		// First bit is sign, next 8 are lightLevelAdd, last 23 are mapId
		int signMask = lightLevelAdd >> 31;
		lightLevelAdd = (lightLevelAdd ^ signMask) - signMask;
		int packed = ((mapId & 0xFFFFFF) << 9) | ((lightLevelAdd & 0xFF) << 1) | (signMask & 1);
		return std::bit_cast<float>(packed);
	}

	static float EntityPackSurface(float alpha, int fuzz, int flipU, int colormap, int lightLevel)
	{
		int alphaByte = (int)(alpha * 255.0f);
		int clampedLightLevel = lightLevel < 0 ? 0 : lightLevel > 255 ? 255 : lightLevel;
		int packed = (alphaByte & 0xFF) | (fuzz << 8) | (flipU << 9) | (clampedLightLevel << 10) | (colormap << 18);
		return std::bit_cast<float>(packed);
	}

	static float EntityPackRender(int lightIndex, int renderIndex)
	{
		int packed = (lightIndex << 12) | (renderIndex & 0xFFF);
		return std::bit_cast<float>(packed);
	}

	static float EntityPackXYZ(int offsetXY, int offsetZ)
	{
		// Shift negative bit for mask to get absolute value and sign bit to remove branches
		int maskXY = offsetXY >> 31;
		int maskZ = offsetZ >> 31;
		offsetXY = (offsetXY ^ maskXY) - maskXY;
		offsetZ = (offsetZ ^ maskZ) - maskZ;
		int offsetXYSign = maskXY & 1;
		int offsetZSign = maskZ & 1;

		// The sign bits land in bits 31/30 so the packing is done unsigned.
		uint32_t packed = ((uint32_t)offsetXYSign << 31) | ((uint32_t)offsetZSign << 30) | ((uint32_t)offsetXY << 16) | (uint32_t)offsetZ;
		return std::bit_cast<float>(packed);
	}

	static float EntityPackTextureInfo(int arrayIndex, int paddedWidth, int paddedHeight)
	{
		// 12, 10, and 10 bits
		int packed = ((arrayIndex & 0xFFF) << 20) | ((paddedWidth & 0x3FF) << 10) | ((paddedHeight & 0x3FF));
		return std::bit_cast<float>(packed);
	}
};

}
