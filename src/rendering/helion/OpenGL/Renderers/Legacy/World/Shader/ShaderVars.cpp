/*
** ShaderVars.cpp
**
** Port of Helion.Render.OpenGL.Renderers.Legacy.World.Shader.ShaderVars.
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

#include "ShaderVars.h"
#include "HelionCVars.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Shader
{

void ShaderVars::Initialize()
{
	PaletteColorMode = vid_colormode == 1;
	EmulateInvulnerabilityColorMap = gl_emulateinvulncolormap;
	SoftwareSpriteEmulation = gl_vanillarender;
}

bool ShaderVars::Changed()
{
	return PaletteColorMode != (vid_colormode == 1) || EmulateInvulnerabilityColorMap != (bool)gl_emulateinvulncolormap ||
		SoftwareSpriteEmulation != (bool)gl_vanillarender;
}

}
