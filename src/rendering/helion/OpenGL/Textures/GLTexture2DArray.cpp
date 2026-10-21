/*
** GLTexture2DArray.cpp
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
*/

#include <cassert>
#include "v_video.h"
#include "textures.h"
#include "OpenGL/Shader/TextureUnits.h"
#include "GLTexture2DArray.h"

namespace Helion::Render::OpenGL::Textures
{

using namespace Geometry;

GLTexture2DArray::GLTexture2DArray(const FString& label) : Label(label)
{
}

GLTexture2DArray::GLTexture2DArray(const FString& label, Geometry::Dimension dimension, int depth, TextureWrapMode wrapMode, std::optional<float> anisotropy)
	: GLTexture2DArray(label)
{
	assert(dimension.Width * dimension.Height > 0 && "Should not have a zero or negative 2D texture array area");
	assert(depth > 0 && "Should not have a zero or negative 2D texture array depth");

	Create(dimension, depth);
	SetParameters(wrapMode);
	SetAnisotropy(anisotropy);
	GenerateMipmaps();
}

GLTexture2DArray::GLTexture2DArray(const FString& label, std::span<const Texture::TextureImage> images, TextureWrapMode wrapMode, std::optional<float> anisotropy)
	: GLTexture2DArray(label)
{
	assert(!images.empty() && "Require at least one image for a texture 2D array");
	assert(images[0].Width() * images[0].Height() > 0 && "Image must have a non-zero area");

	UploadImages(images);
	SetParameters(wrapMode);
	SetAnisotropy(anisotropy);
	GenerateMipmaps();
}

void GLTexture2DArray::Create(Geometry::Dimension dimension, int depth)
{
	m_dimension = dimension;
	m_depth = depth;
	m_uvInverse = Vec2F(1.0f / dimension.Width, 1.0f / dimension.Height);
	m_texture.reset(screen->CreateTextureArray(dimension.Width, dimension.Height, depth, 4, false, false, Label.GetChars()));
}

void GLTexture2DArray::UploadImages(std::span<const Texture::TextureImage> images)
{
	int numImages = (int)images.size();
	const Texture::TextureImage& firstImage = images[0];

	// All 2D array textures must be the same dimension.
	Create(firstImage.Dimension, numImages);

	for (int i = 0; i < numImages; i++)
		UploadLayer(images[i], i);
}

void GLTexture2DArray::UploadLayer(const Texture::TextureImage& image, int layer)
{
	if (!m_texture || image.Empty())
		return;

	m_texture->UploadLayer(layer, image.Pixels.data(), image.Width(), image.Height());
}

void GLTexture2DArray::GenerateMipmaps()
{
	// The array is created without mipmaps (nearest filter), nothing to generate.
}

void GLTexture2DArray::Bind(OpenGL::TextureUnit unit)
{
	Shader::TextureUnits::BindTexture(unit, m_texture.get(), ToClampMode(WrapMode), 1);
}

void GLTexture2DArray::Unbind(OpenGL::TextureUnit unit)
{
	Shader::TextureUnits::Unbind(unit);
}

}
