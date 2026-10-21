/*
** GLTexture2D.cpp
**
** Port of Helion.Render.OpenGL.Textures.GLTexture2D.
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

#include "v_video.h"
#include "textures.h"
#include "OpenGL/Shader/TextureUnits.h"
#include "GLTexture2D.h"

namespace Helion::Render::OpenGL::Textures
{

using namespace Geometry;

GLTexture2D::GLTexture2D(const FString& label) : Label(label)
{
}

GLTexture2D::GLTexture2D(const FString& label, Geometry::Dimension dimension) : GLTexture2D(label)
{
	Dimension = dimension;
}

GLTexture2D::GLTexture2D(const FString& label, const Texture::TextureImage& image, TextureWrapMode wrapMode, std::optional<float> anisotropy)
	: GLTexture2D(label)
{
	UploadImage(image);
	SetParameters(wrapMode);
	SetAnisotropy(anisotropy);
	GenerateMipmaps();
}

void GLTexture2D::UploadImage(const Texture::TextureImage& image)
{
	if (image.Empty())
		return;

	// A new image replaces the previous texture: CreateTexture on an existing texture would keep its size.
	m_texture.reset(screen->CreateHardwareTexture(4));
	if (!m_texture)
		return;

	m_texture->CreateTexture(const_cast<unsigned char*>(image.Pixels.data()), image.Width(), image.Height(), 0, false, Label.GetChars());

	Dimension = image.Dimension;
	m_offset = image.Offset;
	m_uvInverse = Vec2F(1.0f / image.Width(), 1.0f / image.Height());
}

void GLTexture2D::Bind(OpenGL::TextureUnit unit)
{
	Shader::TextureUnits::BindTexture(unit, m_texture.get(), ToClampMode(WrapMode), 1);
}

void GLTexture2D::Unbind(OpenGL::TextureUnit unit)
{
	Shader::TextureUnits::Unbind(unit);
}

}
