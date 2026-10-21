/*
** SideTexture.h
**
** Port of Helion.World.Static.SideTexture.
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

namespace Helion::World::Static
{

// [Flags]
enum class SideTexture
{
	None = 0,
	Upper = 1,
	Middle = 2,
	Lower = 4
};

constexpr SideTexture operator|(SideTexture a, SideTexture b) { return (SideTexture)((int)a | (int)b); }
constexpr SideTexture operator&(SideTexture a, SideTexture b) { return (SideTexture)((int)a & (int)b); }
constexpr SideTexture operator~(SideTexture a) { return (SideTexture)(~(int)a); }
constexpr SideTexture& operator|=(SideTexture& a, SideTexture b) { a = a | b; return a; }
constexpr SideTexture& operator&=(SideTexture& a, SideTexture b) { a = a & b; return a; }

}
