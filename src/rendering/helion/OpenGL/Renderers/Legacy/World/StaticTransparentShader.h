/*
** StaticTransparentShader.h
**
** Port of Helion.Render.OpenGL.Renderers.Legacy.World.StaticTransparentShader.
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

class StaticTransparentShader : public StaticShader
{
public:
	StaticTransparentShader() : StaticShader("OIT Static Transparent")
	{
	}
};

}
