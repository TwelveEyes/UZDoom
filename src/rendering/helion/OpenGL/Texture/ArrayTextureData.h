/*
** ArrayTextureData.h
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
** Helion's class is generic over the GL texture type of the manager; the only instantiation is
** GLLegacyTexture (the fonts texture family is not ported), so it is a plain class here.
** Resources.Texture is FGameTexture, keyed by its texture index (FTextureID::GetIndex()).
** Destroy() cannot call Texture.ClearGLTexture(): the manager passes the callback that clears
** its render store slot of the texture.
**
*/

#pragma once

#include <functional>
#include <span>
#include <unordered_map>
#include <vector>
#include "OpenGL/Texture/GLTexture.h"
#include "OpenGL/Texture/Legacy/GLLegacyTexture.h"

class FGameTexture;

namespace Helion::Render::OpenGL::Texture
{

struct LookupKey
{
	int Key1;
	int Key2;

	LookupKey(TextureFlags textureFlags, int textureHandle) : Key1((int)textureFlags), Key2(textureHandle) { }

	int GetHashCode() const
	{
		return Key1 + Key2 * 131072;
	}

	bool operator==(const LookupKey& other) const
	{
		return other.Key1 == Key1 && other.Key2 == Key2;
	}
};

struct LookupKeyHash
{
	size_t operator()(const LookupKey& key) const { return (size_t)key.GetHashCode(); }
};

class ArrayTextureData
{
public:
	const TextureContext Context;

	explicit ArrayTextureData(TextureContext context) : Context(context) { }

	void Add(Legacy::GLLegacyTexture* arrayTexture, std::vector<Legacy::GLLegacyTexture*> arraySubTextures, std::span<FGameTexture* const> textures, TextureFlags textureFlags);

	int GetArrayTextureHandle(int textureHandle, TextureFlags textureFlags) const;

	bool TryGetTexture(int textureIndex, FGameTexture*& texture) const;

	void Destroy(const std::function<void(FGameTexture*)>& clearGLTexture);

private:
	std::vector<std::vector<Legacy::GLLegacyTexture*>> m_arraySubTextures;
	std::vector<Legacy::GLLegacyTexture*> m_arrayTextures;
	std::vector<FGameTexture*> m_texturesForArrays;
	std::unordered_map<int, FGameTexture*> m_textureByIndex;
	std::unordered_map<LookupKey, int, LookupKeyHash> m_arrayTextureLookup;
};

}
