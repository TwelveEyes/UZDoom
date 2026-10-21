/*
** GLLegacyTexture.h
**
** Port of Helion.Render.OpenGL.Texture.Legacy.GLLegacyTexture.
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** Every legacy texture is a texture array (a plain texture is a one layer array),
** exactly like Helion, so the world programs always sample sampler2DArray.
** Bind/Unbind take the TextureUnit (Shader/TextureUnits.h) the program samples.
** Source is UZDoom-only: the FGameTexture the pixels came from.
**
*/

#pragma once

#include "OpenGL/Texture/GLTexture.h"
#include "OpenGL/Util/TextureUnit.h"

class FGameTexture;

namespace Helion::Render::OpenGL::Texture::Legacy
{

class GLLegacyTexture : public GLTexture
{
public:
	// 0 = the gl_texture_filter / gl_texture_filter_anisotropic setting of the backend,
	// 1 = nearest. Palette colour mode needs nearest filtering and no anisotropy (colormap lookups).
	static int SamplerFilter;

	TextureFlags Flags = TextureFlags::Default;
	int ArrayIndex = 0;
	bool IsArray() const { return ParentArrayTexture != nullptr; }
	bool IsParentArray = false;
	GLLegacyTexture* ParentArrayTexture = nullptr;
	FGameTexture* Source = nullptr;
	// UZDoom-only: Dimension() is the size in map units (the geometry maps the image with it), ImageDimension
	// the pixel size of the layer content (hires textures are larger). Helion's Dimension is both.
	Geometry::Dimension ImageDimension;

	GLLegacyTexture(int textureId, const FString& name, Geometry::Dimension dimension, Geometry::Vec2I offset, Resources::ResourceNamespace ns,
		TextureTarget target, int transparentPixelCount, int blankRowsFromTop = 0, int blankRowsFromBottom = 0, IHardwareTexture* hwTexture = nullptr,
		bool ownsTexture = true, TextureContext textureContext = TextureContext::Default);

	IHardwareTexture* HardwareTexture() const override;

	void Bind(TextureUnit unit) const;
	static void Unbind(TextureUnit unit);

	FString ToString() const;

private:
	void SetWarpUniform() const;
};

}
