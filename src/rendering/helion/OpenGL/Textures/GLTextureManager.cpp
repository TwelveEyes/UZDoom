/*
** GLTextureManager.cpp
**
** Port of Helion.Render.OpenGL.Textures.GLTextureManager.
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
#include "textures.h"
#include "gametexture.h"
#include "texturemanager.h"
#include "c_cvars.h"
#include "hwrenderer/data/hw_cvars.h"
#include "GLTextureManager.h"

namespace Helion::Render::OpenGL::Textures
{

using namespace Geometry;

namespace
{

TArray<float> CreateIdentityTable()
{
	TArray<float> table;
	table.Resize(GLTextureManager::MaxSupportedTextures);
	for (int i = 0; i < GLTextureManager::MaxSupportedTextures; i++)
		table[i] = (float)i;
	return table;
}

TArray<float> CreateZeroTable()
{
	TArray<float> table;
	table.Resize(GLTextureManager::MaxSupportedTextures);
	memset(table.Data(), 0, sizeof(float) * table.Size());
	return table;
}

}

GLTextureManager::GLTextureManager() :
	TranslationTableBuffer("Translation table", CreateIdentityTable(), SizedInternalFormat::R32f, false),
	TextureHandleBuffer("Texture handle", CreateZeroTable(), SizedInternalFormat::R32f, false)
{
	std::optional<float> anisotropy;
	if (gl_texture_filter_anisotropic > 1)
		anisotropy = (float)gl_texture_filter_anisotropic;

	m_textures = std::make_unique<GLTexture2DArrayAtlas>("Texture Atlas", Dimension(2048, 2048), 8, TextureWrapMode::Clamp, anisotropy, true);
	NullHandle = m_textures->UploadImage(Texture::TextureImage::NullImage());
	WhiteHandle = m_textures->UploadImage(Texture::TextureImage::WhiteImage());
}

GLTextureManager::~GLTextureManager()
{
	Dispose();
}

void GLTextureManager::UploadAll(const std::unordered_set<int>& textureIndices)
{
	for (int textureIndex : textureIndices)
		Get(textureIndex);
}

GLImageHandle GLTextureManager::Get(int index)
{
	assert(index >= 0 && "Accessing a negative texture handle index");
	assert(index < MaxSupportedTextures && "Likely accessing an incorrect texture index due to the large size");

	if (index < 0 || index >= MaxSupportedTextures)
		return NullHandle;

	if ((unsigned)index >= m_textureIdxToPosition.Size())
		m_textureIdxToPosition.Resize((unsigned)index + 1);

	std::optional<GLImageHandle> existingHandle = m_textureIdxToPosition[index];
	if (existingHandle.has_value())
		return existingHandle.value();

	FGameTexture* texture = TexMan.GameByIndex(index);

	if (texture != nullptr && texture->isValid())
		return UploadTexture(index, texture);

	m_textureIdxToPosition[index] = NullHandle;
	return NullHandle;
}

GLImageHandle GLTextureManager::UploadTexture(int index, FGameTexture* texture)
{
	Texture::TextureImage image = Texture::TextureImage::FromGameTexture(texture, 0, false, false, false);
	if (image.Empty())
	{
		m_textureIdxToPosition[index] = NullHandle;
		return NullHandle;
	}

	GLImageHandle handle = m_textures->UploadImage(image);
	m_textureIdxToPosition[index] = handle;

	// Texture buffer #1 (TranslationTableBuffer) is the identity table and needs no update.
	// Texture buffer #2: the layer of the texture in the atlas.
	Util::GLMappedBuffer<float> mapped = TextureHandleBuffer.MapWithDisposable();
	mapped.MappedMemoryPtr[index] = (float)handle.Layer;
	TextureHandleBuffer.SetDirtyRange(index, 1);
	mapped.Dispose();
	return handle;
}

void GLTextureManager::Dispose()
{
	if (m_disposed)
		return;

	m_textures.reset();
	m_disposed = true;
}

}
