/*
** TextureUnit.h
**
** OpenTK's TextureUnit as a typed index.
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** There is no glActiveTexture in the render program API. A texture unit is a
** slot of the TextureUnits table (Shader/TextureUnits.h); a program samples the
** slot its sampler uniform was set to (ProgramUniforms::Set(TextureUnit, ...)).
**
*/

#pragma once

namespace Helion::Render::OpenGL
{

enum class TextureUnit : int
{
	Texture0, Texture1, Texture2, Texture3, Texture4, Texture5, Texture6, Texture7,
	Texture8, Texture9, Texture10, Texture11, Texture12, Texture13, Texture14, Texture15,
	Texture16, Texture17, Texture18, Texture19, Texture20, Texture21, Texture22, Texture23,
	Texture24, Texture25, Texture26, Texture27, Texture28, Texture29, Texture30, Texture31,
};

constexpr int MaxTextureUnits = 32;

// GLHelper.ToInt(TextureUnit)
constexpr int ToInt(TextureUnit unit)
{
	return (int)unit - (int)TextureUnit::Texture0;
}

}
