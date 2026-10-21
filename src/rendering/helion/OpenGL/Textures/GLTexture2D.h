/*
** GLTexture2D.h
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
** Created with DFrameBuffer::CreateHardwareTexture(4) when the first image is uploaded.
** SetParameters/SetAnisotropy/GenerateMipmaps set GL state of the bound texture: the wrap mode
** is kept in WrapMode and applied by Bind (nearest filter, which also makes anisotropy and
** mipmaps irrelevant, so the texture is created without mipmaps). SetAnisotropy keeps the value.
**
*/

#pragma once

#include <optional>
#include "zstring.h"
#include "Util/Geometry.h"
#include "OpenGL/Texture/TextureImage.h"
#include "GLTexture.h"

namespace Helion::Render::OpenGL::Textures
{

class GLTexture2D : public GLTexture
{
public:
	const FString Label;
	Geometry::Dimension Dimension = Geometry::Dimension(1, 1);
	Geometry::Vec2I Offset() const { return m_offset; }
	Geometry::Vec2F UVInverse() const { return m_uvInverse; }

	TextureWrapMode WrapMode = TextureWrapMode::Repeat;
	std::optional<float> Anisotropy;

	explicit GLTexture2D(const FString& label);
	GLTexture2D(const FString& label, Geometry::Dimension dimension);
	GLTexture2D(const FString& label, const Texture::TextureImage& image, TextureWrapMode wrapMode, std::optional<float> anisotropy = 1.0f);

	void UploadImage(const Texture::TextureImage& image);

	void SetParameters(TextureWrapMode wrapMode) { WrapMode = wrapMode; }
	void SetAnisotropy(std::optional<float> anisotropy) { Anisotropy = anisotropy; }
	// Mipmaps are not needed with the nearest filter of SetParameters.
	static void GenerateMipmaps() {}

	void Bind(OpenGL::TextureUnit unit) override;
	void Unbind(OpenGL::TextureUnit unit) override;

private:
	Geometry::Vec2I m_offset;
	Geometry::Vec2F m_uvInverse = Geometry::Vec2F(1, 1);
};

}
