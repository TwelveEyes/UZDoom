/*
** GLTexture2DArrayAtlas.cpp
**
** Port of Helion.Render.OpenGL.Textures.GLTexture2DArrayAtlas.
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
#include <cstring>
#include "engineerrors.h"
#include "GLTexture2DArrayAtlas.h"

namespace Helion::Render::OpenGL::Textures
{

using namespace Geometry;

GLTexture2DArrayAtlas::GLTexture2DArrayAtlas(const FString& label, Geometry::Dimension dimension, int depth, TextureWrapMode wrapMode, std::optional<float> anisotropy, bool clear)
	: GLTexture2DArray(label, dimension, depth, wrapMode, anisotropy)
{
	// Need to re-evaluate why we're needing to upload so many pixels per layer, and not getting more layers
	assert(dimension.Width <= 4096 && dimension.Height <= 4096);

	ResetAtlasHandles();
}

void GLTexture2DArrayAtlas::ResetAtlasHandles()
{
	m_layerAtlases.clear();
	m_layerPixels.assign((size_t)Depth(), std::vector<uint8_t>());
	m_layerDirty.assign((size_t)Depth(), false);

	for (int i = 0; i < Depth(); i++)
		m_layerAtlases.push_back(std::make_unique<Helion::Util::Atlas::Atlas2D>(Dimension()));
}

void GLTexture2DArrayAtlas::Clear()
{
	ResetAtlasHandles();
	ClearLayers();
}

void GLTexture2DArrayAtlas::ClearLayers()
{
	// We will assume clearing is extremely rare and only happens once, and if this ever
	// needs to happen, then we're okay with the minor (if any) hitch that happens when
	// doing this.
	std::vector<uint8_t> zeroedData((size_t)Dimension().Width * Dimension().Height * 4, 0);

	for (int layer = 0; layer < (int)m_layerAtlases.size(); layer++)
		HardwareTexture()->UploadLayer(layer, zeroedData.data(), Dimension().Width, Dimension().Height);
}

GLImageHandle GLTexture2DArrayAtlas::UploadImage(const Texture::TextureImage& image)
{
	assert(image.Width() * image.Height() > 0 && "Cannot upload an image with zero or negative area");

	for (int layer = 0; layer < (int)m_layerAtlases.size(); layer++)
	{
		Helion::Util::Atlas::AtlasHandle* handle = m_layerAtlases[layer]->Add(image.Dimension);
		if (handle == nullptr)
			continue;

		Box2I location = handle->Location();

		// Compose the image into the layer.
		std::vector<uint8_t>& pixels = m_layerPixels[layer];
		if (pixels.empty())
			pixels.assign((size_t)Dimension().Width * Dimension().Height * 4, 0);

		for (int y = 0; y < image.Height(); y++)
		{
			memcpy(&pixels[((size_t)(location.Min.Y + y) * Dimension().Width + location.Min.X) * 4],
				&image.Pixels[(size_t)y * image.Width() * 4], (size_t)image.Width() * 4);
		}
		m_layerDirty[layer] = true;

		Vec2F min((float)location.Min.X, (float)location.Min.Y);
		Vec2F max((float)location.Max.X, (float)location.Max.Y);
		Vec2F uvInverse = UVInverse();
		return GLImageHandle{ location, Box2F(Vec2F(min.X * uvInverse.X, min.Y * uvInverse.Y), Vec2F(max.X * uvInverse.X, max.Y * uvInverse.Y)), layer };
	}

	I_FatalError("GLTexture2DArrayAtlas %s: atlas overflow (need to resize the texture, copy, etc)", Label.GetChars());
	return GLImageHandle();
}

void GLTexture2DArrayAtlas::FlushLayers()
{
	for (int layer = 0; layer < (int)m_layerDirty.size(); layer++)
	{
		if (!m_layerDirty[layer])
			continue;

		HardwareTexture()->UploadLayer(layer, m_layerPixels[layer].data(), Dimension().Width, Dimension().Height);
		m_layerDirty[layer] = false;
	}
}

void GLTexture2DArrayAtlas::Bind(OpenGL::TextureUnit unit)
{
	FlushLayers();
	GLTexture2DArray::Bind(unit);
}

}
