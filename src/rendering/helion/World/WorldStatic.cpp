/*
** WorldStatic.cpp
**
** Port of Helion.World.WorldStatic (render-visible subset).
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

#include "WorldStatic.h"
#include "g_levellocals.h"

namespace Helion::World
{

void WorldStatic::CaptureFlatVertexPositions(FLevelLocals* level)
{
	s_flatVertexPositions.Resize(level->vertexes.Size());
	for (unsigned i = 0; i < level->vertexes.Size(); i++)
		s_flatVertexPositions[i] = level->vertexes[i].fPos();
}

DVector2 WorldStatic::FlatVertexPosition(const vertex_t* vertex)
{
	unsigned index = (unsigned)vertex->Index();
	return index < s_flatVertexPositions.Size() ? s_flatVertexPositions[index] : vertex->fPos();
}

}
