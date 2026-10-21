/*
** ViewMath.h
**
** Port of Helion.Render.Common.World.ViewMath.
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

#include "Common/Context/WorldRenderContext.h"

namespace Helion::Render::Common::World
{

class ViewMath
{
public:
	static Geometry::mat4 Mvp(const Context::WorldRenderContext& renderInfo);
};

}
