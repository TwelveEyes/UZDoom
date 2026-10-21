/*
** StaticCompositeShader.h
**
** Port of Helion.Render.OpenGL.Renderers.Legacy.World.StaticCompositeShader.
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

#include "StaticShader.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World
{

class StaticCompositeShader : public StaticShader
{
public:
	StaticCompositeShader() : StaticShader("OIT Static Composite")
	{
	}
};

}
