/*
** InterpolationPlaneClipAlphaShader.h
**
** Port of Helion.Render.OpenGL.Renderers.Legacy.World.InterpolationPlaneClipAlphaShader.
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

class InterpolationPlaneClipAlphaShader : public InterpolationShader
{
public:
	InterpolationPlaneClipAlphaShader() : InterpolationShader("PlaneAlphaClip")
	{
	}
};

}
