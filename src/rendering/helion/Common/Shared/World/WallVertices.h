/*
** WallVertices.h
**
** Port of Helion.Render.OpenGL.Shared.World.WallVertices.
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

#include "Common/Shared/World/TriangulatedWorldVertex.h"

namespace Helion::Render::OpenGL::Shared::World
{

struct WallVertices
{
	TriangulatedWorldVertex TopLeft;
	TriangulatedWorldVertex BottomRight;
	float PrevTopZ = 0;
	float PrevBottomZ = 0;
};

}
