/*
** SkyOptions.h
**
** Port of Helion.Resources.Definitions.SkyOptions.
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

namespace Helion::Resources::Definitions
{

enum class SkyOptions
{
	None = 0,
	// The sky is not mirrored (UZDoom: HWSkyInfo::mirrored == false). This is the default of every sector sky.
	Flip = 1,
	// The sky comes from a line (MBF sky transfer / ZDoom static init), see SkyInfo::Offset.
	SkyTransfer = 2
};

constexpr SkyOptions operator|(SkyOptions a, SkyOptions b) { return (SkyOptions)((int)a | (int)b); }
constexpr SkyOptions operator&(SkyOptions a, SkyOptions b) { return (SkyOptions)((int)a & (int)b); }
constexpr SkyOptions& operator|=(SkyOptions& a, SkyOptions b) { a = a | b; return a; }

}
