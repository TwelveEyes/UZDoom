/*
** GLTexture.h
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
** GL.GenTexture is replaced by an IHardwareTexture created by the derived classes through
** DFrameBuffer; Bind/Unbind select the TextureUnit slot of Shader/TextureUnits.h.
** TextureWrapMode is OpenTK's enum, reduced to the values the classes use. The wrap mode and
** the (always nearest) filter of Helion's SetParameters are the clamp mode and filter of the
** program texture binding.
**
*/

#pragma once

#include <memory>
#include "hw_ihwtexture.h"
#include "OpenGL/Util/TextureUnit.h"

namespace Helion::Render::OpenGL::Textures
{

enum class TextureWrapMode
{
	Repeat,
	ClampToEdge,
	Clamp = ClampToEdge,
};

class GLTexture
{
public:
	GLTexture() = default;
	GLTexture(const GLTexture&) = delete;
	GLTexture& operator=(const GLTexture&) = delete;
	virtual ~GLTexture() = default;

	virtual void Bind(OpenGL::TextureUnit unit) = 0;
	virtual void Unbind(OpenGL::TextureUnit unit) = 0;

	IHardwareTexture* HardwareTexture() const { return m_texture.get(); }

	void Dispose()
	{
		m_texture.reset();
	}

protected:
	// Helion: protected int Name.
	std::unique_ptr<IHardwareTexture> m_texture;
};

// Parameters of Helion's SetParameters as a backend sampler description (CLAMP_* of textures.h, filter 1 = nearest).
int ToClampMode(TextureWrapMode wrapMode);

}
