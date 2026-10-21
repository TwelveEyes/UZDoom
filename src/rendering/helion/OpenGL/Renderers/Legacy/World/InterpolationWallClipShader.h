/*
** InterpolationWallClipShader.h
**
** Port of Helion.Render.OpenGL.Renderers.Legacy.World.InterpolationWallClipShader.
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

#include "InterpolationShader.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World
{

class InterpolationWallClipShader : public InterpolationShader
{
public:
	InterpolationWallClipShader() : InterpolationShader("WallClip")
	{
	}
};

}
