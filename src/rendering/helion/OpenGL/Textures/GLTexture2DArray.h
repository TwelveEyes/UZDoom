/*
** GLTexture2DArray.h
**
** Port of Helion.Render.OpenGL.Textures.GLTexture2DArray.
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** Created with DFrameBuffer::CreateTextureArray(w, h, layers, 4, false, ...). Like GLTexture2D the
** wrap mode is applied when binding and the filter is nearest, so no mipmaps are created.
** UploadImages creates the array when the layer count is known (the constructor taking images).
**
*/

#pragma once

#include <optional>
#include <span>
#include "zstring.h"
#include "Util/Geometry.h"
#include "OpenGL/Texture/TextureImage.h"
#include "GLTexture.h"

namespace Helion::Render::OpenGL::Textures
{

class GLTexture2DArray : public GLTexture
{
public:
	const FString Label;
	Geometry::Dimension Dimension() const { return m_dimension; }
	int Depth() const { return m_depth; }
	Geometry::Vec2F UVInverse() const { return m_uvInverse; }

	TextureWrapMode WrapMode = TextureWrapMode::Repeat;
	std::optional<float> Anisotropy;

	explicit GLTexture2DArray(const FString& label);
	GLTexture2DArray(const FString& label, Geometry::Dimension dimension, int depth, TextureWrapMode wrapMode, std::optional<float> anisotropy = 1.0f);
	GLTexture2DArray(const FString& label, std::span<const Texture::TextureImage> images, TextureWrapMode wrapMode, std::optional<float> anisotropy = 1.0f);

	void UploadImages(std::span<const Texture::TextureImage> images);
	void UploadLayer(const Texture::TextureImage& image, int layer);

	void SetParameters(TextureWrapMode wrapMode) { WrapMode = wrapMode; }
	void SetAnisotropy(std::optional<float> anisotropy) { Anisotropy = anisotropy; }
	void GenerateMipmaps();

	void Bind(OpenGL::TextureUnit unit) override;
	void Unbind(OpenGL::TextureUnit unit) override;

protected:
	void Create(Geometry::Dimension dimension, int depth);

private:
	Geometry::Dimension m_dimension = Geometry::Dimension(1, 1);
	int m_depth = 1;
	Geometry::Vec2F m_uvInverse = Geometry::Vec2F(1, 1);
};

}
