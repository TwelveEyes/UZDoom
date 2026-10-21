/*
** ArrayTextureData.cpp
**
** Port of Helion.Render.OpenGL.Texture.ArrayTextureData<GLTextureType>.
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
#include "textures.h"
#include "gametexture.h"
#include "ArrayTextureData.h"

namespace Helion::Render::OpenGL::Texture
{

void ArrayTextureData::Add(Legacy::GLLegacyTexture* arrayTexture, std::vector<Legacy::GLLegacyTexture*> arraySubTextures, std::span<FGameTexture* const> textures, TextureFlags textureFlags)
{
	assert(arraySubTextures.size() == textures.size() && "arraySubTextures != textures length");
	// Flip high bit to ensure no collisions
	int arrayTextureId = arrayTexture->TextureId | (1 << (30 - (int)Context));
	m_arrayTextures.push_back(arrayTexture);
	m_arraySubTextures.push_back(std::move(arraySubTextures));
	for (FGameTexture* texture : textures)
	{
		m_texturesForArrays.push_back(texture);
		m_textureByIndex[texture->GetID().GetIndex()] = texture;
	}
	for (size_t i = 0; i < textures.size(); i++)
		m_arrayTextureLookup[LookupKey(textureFlags, textures[i]->GetID().GetIndex())] = arrayTextureId;
}

int ArrayTextureData::GetArrayTextureHandle(int textureHandle, TextureFlags textureFlags) const
{
	auto it = m_arrayTextureLookup.find(LookupKey(textureFlags, textureHandle));
	if (it != m_arrayTextureLookup.end())
		return it->second;
	return textureHandle;
}

bool ArrayTextureData::TryGetTexture(int textureIndex, FGameTexture*& texture) const
{
	auto it = m_textureByIndex.find(textureIndex);
	if (it != m_textureByIndex.end())
	{
		texture = it->second;
		return true;
	}

	texture = nullptr;
	return false;
}

void ArrayTextureData::Destroy(const std::function<void(FGameTexture*)>& clearGLTexture)
{
	for (Legacy::GLLegacyTexture* arrayTexture : m_arrayTextures)
		arrayTexture->Dispose();

	for (FGameTexture* texture : m_texturesForArrays)
		clearGLTexture(texture);

	m_arrayTextures.clear();
	m_arraySubTextures.clear();
	m_arrayTextureLookup.clear();
	m_texturesForArrays.clear();
	m_textureByIndex.clear();
}

}
