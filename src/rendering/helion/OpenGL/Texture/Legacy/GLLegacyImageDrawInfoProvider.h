/*
** GLLegacyImageDrawInfoProvider.h
**
** Port of Helion.Render.OpenGL.Texture.Legacy.GLLegacyImageDrawInfoProvider.
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

#pragma once

#include "Common/Shared/IImageDrawInfoProvider.h"

namespace Helion::Render::OpenGL::Texture::Legacy
{

class LegacyGLTextureManager;

class GLLegacyImageDrawInfoProvider : public Shared::IImageDrawInfoProvider
{
public:
	explicit GLLegacyImageDrawInfoProvider(LegacyGLTextureManager* textureManager) : m_textureManager(textureManager) { }

	bool ImageExists(const FString& image) override;
	Geometry::Dimension GetImageDimension(const FString& image, Resources::ResourceNamespace resourceNamespace = Resources::ResourceNamespace::Global) override;
	Geometry::Vec2I GetImageOffset(const FString& image, Resources::ResourceNamespace resourceNamespace = Resources::ResourceNamespace::Global) override;
	// Helion's GLFontTexture is not ported: the height is the one of the UZDoom font of that name (0 if there is none).
	int GetFontHeight(const FString& font) override;

private:
	LegacyGLTextureManager* m_textureManager;
};

}
