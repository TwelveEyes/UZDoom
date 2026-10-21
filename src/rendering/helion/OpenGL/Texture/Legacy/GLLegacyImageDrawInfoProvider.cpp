/*
** GLLegacyImageDrawInfoProvider.cpp
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

#include "v_font.h"
#include "GLLegacyImageDrawInfoProvider.h"
#include "LegacyGLTextureManager.h"

namespace Helion::Render::OpenGL::Texture::Legacy
{

bool GLLegacyImageDrawInfoProvider::ImageExists(const FString& image)
{
	return m_textureManager->Contains(image);
}

Geometry::Dimension GLLegacyImageDrawInfoProvider::GetImageDimension(const FString& image, Resources::ResourceNamespace resourceNamespace)
{
	GLLegacyTexture* texture;
	m_textureManager->TryGet(image, resourceNamespace, texture);
	return texture->Dimension();
}

Geometry::Vec2I GLLegacyImageDrawInfoProvider::GetImageOffset(const FString& image, Resources::ResourceNamespace resourceNamespace)
{
	GLLegacyTexture* texture;
	m_textureManager->TryGet(image, resourceNamespace, texture);
	return texture->Offset();
}

int GLLegacyImageDrawInfoProvider::GetFontHeight(const FString& font)
{
	FFont* uzFont = V_GetFont(font.GetChars());
	return uzFont != nullptr ? uzFont->GetHeight() : 0;
}

}
