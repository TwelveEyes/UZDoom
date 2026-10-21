/*
** GLTexture2DArrayAtlas.h
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
** Helion's UploadImage only allocates the area in a layer's atlas and never copies the pixels.
** The backend has no sub rectangle upload (IHardwareTexture::UploadLayer replaces a whole layer
** at its origin), so the pixels are composed into a per layer BGRA staging buffer that is created
** the first time a layer is used and uploaded as a whole layer the next time the array is bound
** (or by FlushLayers). The constructor's clear argument is ignored like in Helion.
**
*/

#pragma once

#include <memory>
#include <vector>
#include "Util/Geometry.h"
#include "Util/Atlas/Atlas2D.h"
#include "GLTexture2DArray.h"

namespace Helion::Render::OpenGL::Textures
{

struct GLImageHandle
{
	Geometry::Box2I Area;
	Geometry::Box2F UV;
	int Layer = 0;
};

class GLTexture2DArrayAtlas : public GLTexture2DArray
{
public:
	GLTexture2DArrayAtlas(const FString& label, Geometry::Dimension dimension, int depth, TextureWrapMode wrapMode, std::optional<float> anisotropy, bool clear);

	GLImageHandle UploadImage(const Texture::TextureImage& image);

	// Uploads the layers that changed since the last flush.
	void FlushLayers();

	void Bind(OpenGL::TextureUnit unit) override;

protected:
	void Clear();

private:
	std::vector<std::unique_ptr<Helion::Util::Atlas::Atlas2D>> m_layerAtlases;
	std::vector<std::vector<uint8_t>> m_layerPixels;
	std::vector<bool> m_layerDirty;

	void ResetAtlasHandles();
	void ClearLayers();
};

}
