/*
** Constants.h
**
** Port of the Helion.Util.Constants subset used by the renderer (LightBuffer).
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

namespace Helion::Util::Constants
{

struct LightBuffer
{
	static constexpr int DarkIndex = 0;
	static constexpr int FullBrightIndex = 1;
	static constexpr int ColorMapStartIndex = 2;
	static constexpr int BufferSize = 3;
	static constexpr int ColorMapCount = 32;
	static constexpr int SectorIndexStart = ColorMapStartIndex + ColorMapCount;
	static constexpr int FloorOffset = 0;
	static constexpr int CeilingOffset = 1;
	static constexpr int WallOffset = 2;
	static constexpr int UpperWallOffset = 0;
	static constexpr int MiddleWallOffset = 1;
	static constexpr int LowerWallOffset = 2;
	static constexpr int LightLevels = 48;
	static constexpr int LightLevelsIndexMax = LightLevels - 1;
};

struct ColorMapIndex
{
	static constexpr int FullBright = 0;
	static constexpr int LightAmp = 1;
};

}
