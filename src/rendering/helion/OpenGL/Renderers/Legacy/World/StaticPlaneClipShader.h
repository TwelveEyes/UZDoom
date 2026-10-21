/*
** StaticPlaneClipShader.h
**
** Port of Helion.Render.OpenGL.Renderers.Legacy.World.StaticPlaneClipShader.
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

class StaticPlaneClipShader : public StaticShader
{
public:
	StaticPlaneClipShader() : StaticShader("PlaneClip")
	{
	}
};

}
