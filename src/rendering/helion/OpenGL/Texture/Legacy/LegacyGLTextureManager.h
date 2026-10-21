/*
** LegacyGLTextureManager.h
**
** Port of Helion.Render.OpenGL.Texture.Legacy.LegacyGLTextureManager.
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** The OpenGL calls of Helion (GL.GenTexture, TexImage3D/TexSubImage3D, GenerateMipmap, TexParameter)
** are IHardwareTexture objects created with DFrameBuffer::CreateTextureArray and filled with
** UploadLayer; every legacy texture is a texture array (a plain texture has one layer), exactly like
** Helion. The wrap mode, filter and anisotropy are not texture state any more: they are the
** clamp mode / filter arguments of the program texture binding (GLLegacyTexture::Bind, which reads
** gl_texture_filter and gl_texture_filter_anisotropic in the backend sampler). This removes
** HandleFilterChange/HandleAnisotropyChange/RegisterTexture/UnRegisterTexture and
** SetTextureFilter/SetAnisotropicFiltering/SetTextureParameters. Palette colour mode needs nearest
** filtering and anisotropy 1 (colormap lookups): GLLegacyTexture::SamplerFilter is 1 then and the
** arrays are created without mipmaps.
**
*/

#pragma once

#include "OpenGL/Texture/GLTextureManager.h"
#include "GLLegacyImageDrawInfoProvider.h"

namespace Helion::Render::OpenGL::Texture::Legacy
{

class LegacyGLTextureManager : public GLTextureManager
{
public:
	LegacyGLTextureManager();
	~LegacyGLTextureManager() override;

	Shared::IImageDrawInfoProvider* ImageDrawInfoProvider() override { return &m_imageDrawInfoProvider; }

	GLLegacyTexture* GetParentArrayTexture(int index, bool repeatY = true);

	void UploadAndSetParameters(GLLegacyTexture* texture, const TextureImage& image, const FString& name, Resources::ResourceNamespace resourceNamespace, TextureFlags flags);

	void ReUpload(GLLegacyTexture* texture, const TextureImage& image) override;

	void Dispose();

protected:
	/// <summary>
	/// Creates a new texture. The manager owns it.
	/// </summary>
	/// <param name="image">The image that makes up this texture.</param>
	/// <param name="name">The name of the texture.</param>
	/// <param name="resourceNamespace">What namespace the texture is from.
	/// </param>
	/// <returns>A new texture.</returns>
	GLLegacyTexture* GenerateTexture(const TextureImage& image, const FString& name, Resources::ResourceNamespace resourceNamespace, TextureFlags flags = TextureFlags::Default) override;

	std::vector<GLLegacyTexture*> GenerateTextureArray(const GetImageFunc& getImage, int imageLength, Geometry::Dimension dimension, Resources::ResourceNamespace resourceNamespace,
		TextureFlags flags, TextureContext textureContext, GLLegacyTexture*& arrayTexture) override;

private:
	GLLegacyImageDrawInfoProvider m_imageDrawInfoProvider;
	bool m_disposed = false;

	static TextureFlags GetFlagsForNamespace(Resources::ResourceNamespace resourceNamespace, TextureFlags flags);
};

}
