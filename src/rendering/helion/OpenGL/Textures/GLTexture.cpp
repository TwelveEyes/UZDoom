/*
** GLTexture.cpp
**
** Port of Helion.Render.OpenGL.Textures.GLTexture.
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

#include "textures.h"
#include "GLTexture.h"

namespace Helion::Render::OpenGL::Textures
{

int ToClampMode(TextureWrapMode wrapMode)
{
	return wrapMode == TextureWrapMode::Repeat ? CLAMP_NONE : CLAMP_XY;
}

}
