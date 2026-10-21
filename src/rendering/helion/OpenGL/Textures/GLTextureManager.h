/*
** GLTextureManager.h
**
** Port of Helion.Render.OpenGL.Textures.GLTextureManager (the atlas texture manager).
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** Helion's class is unfinished: UploadTexture ends in throw new("TODO: Did not update texture
** buffer"). The two buffers are completed here as follows: TranslationTableBuffer holds, per
** texture index, the texture index to draw (identity until a caller rewrites an entry, e.g. for
** animations) and TextureHandleBuffer holds the atlas layer of the uploaded texture.
** Resources.Texture is an FGameTexture; the null handle for an invalid texture is NullHandle.
** There is no ArchiveCollection/IConfig, the anisotropy setting is gl_texture_filter_anisotropic.
**
*/

#pragma once

#include <memory>
#include <optional>
#include <unordered_set>
#include "tarray.h"
#include "OpenGL/Texture/TextureImage.h"
#include "GLBufferTexture.h"
#include "GLTexture2DArrayAtlas.h"

class FGameTexture;

namespace Helion::Render::OpenGL::Textures
{

class GLTextureManager
{
public:
	static constexpr int MaxSupportedTextures = 65536;

	GLImageHandle NullHandle;
	GLImageHandle WhiteHandle;
	GLBufferTexture<float> TranslationTableBuffer;
	GLBufferTexture<float> TextureHandleBuffer;

	GLTextureManager();
	GLTextureManager(const GLTextureManager&) = delete;
	GLTextureManager& operator=(const GLTextureManager&) = delete;
	~GLTextureManager();

	// A convenience method for uploading every handle. Invoking this with textures that
	// are already uploaded is okay, since it will not cause duplicate uploading.
	void UploadAll(const std::unordered_set<int>& textureIndices);

	// Gets an image handle from the texture manager (TexMan) texture index.
	GLImageHandle Get(int index);

	GLTexture2DArrayAtlas& Atlas() { return *m_textures; }

	void Dispose();

private:
	std::unique_ptr<GLTexture2DArrayAtlas> m_textures;
	TArray<std::optional<GLImageHandle>> m_textureIdxToPosition;
	bool m_disposed = false;

	GLImageHandle UploadTexture(int index, FGameTexture* texture);
};

}
