/*
** PrimitiveType.h
**
** OpenTK PrimitiveType (the values the renderer draws with) mapped onto the
** render state's draw types.
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

#include "hwrenderer/data/hw_renderstate.h"

namespace Helion::Render::OpenGL::Util
{

enum class PrimitiveType
{
	Points = DT_Points,
	Lines = DT_Lines,
	Triangles = DT_Triangles,
	TriangleFan = DT_TriangleFan,
	TriangleStrip = DT_TriangleStrip,
};

constexpr int ToDrawType(PrimitiveType type)
{
	return (int)type;
}

}
