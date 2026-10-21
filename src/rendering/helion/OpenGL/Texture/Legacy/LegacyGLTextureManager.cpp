/*
** LegacyGLTextureManager.cpp
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
*/

#include <cassert>
#include "v_video.h"
#include "engineerrors.h"
#include "textures.h"
#include "LegacyGLTextureManager.h"

namespace Helion::Render::OpenGL::Texture::Legacy
{

using namespace Geometry;
using Resources::ResourceNamespace;

LegacyGLTextureManager::LegacyGLTextureManager() : m_imageDrawInfoProvider(this)
{
	// Filtering must be nearest for colormap support
	GLLegacyTexture::SamplerFilter = PaletteColorMode() ? 1 : 0;

	CreateDefaultTextures();
}

LegacyGLTextureManager::~LegacyGLTextureManager()
{
	Dispose();
}

GLLegacyTexture* LegacyGLTextureManager::GetParentArrayTexture(int index, bool repeatY)
{
	GLLegacyTexture* texture = GetTexture(index, repeatY);
	return texture->ParentArrayTexture != nullptr ? texture->ParentArrayTexture : texture;
}

TextureFlags LegacyGLTextureManager::GetFlagsForNamespace(ResourceNamespace resourceNamespace, TextureFlags flags)
{
	if (resourceNamespace == ResourceNamespace::Sprites || resourceNamespace == ResourceNamespace::Brightmaps || resourceNamespace == ResourceNamespace::Undefined)
		flags = TextureFlags::ClampX | TextureFlags::ClampY;
	return flags;
}

void LegacyGLTextureManager::UploadAndSetParameters(GLLegacyTexture* texture, const TextureImage& image, const FString& name, ResourceNamespace resourceNamespace, TextureFlags flags)
{
	flags = GetFlagsForNamespace(resourceNamespace, flags);

	// Mipmaps are created once per texture: the filter that needs them is chosen when the texture is bound.
	IHardwareTexture* hwTexture = screen->CreateTextureArray(image.Width(), image.Height(), 1, 4, false, !PaletteColorMode(), name.GetChars());
	if (hwTexture == nullptr)
		I_FatalError("LegacyGLTextureManager: the backend cannot create texture arrays (%s)", name.GetChars());

	hwTexture->UploadLayer(0, image.Pixels.data(), image.Width(), image.Height());
	hwTexture->GenerateMipmaps();

	texture->SetHardwareTexture(hwTexture);
	texture->Flags = flags;
}

void LegacyGLTextureManager::ReUpload(GLLegacyTexture* texture, const TextureImage& image)
{
	IHardwareTexture* hwTexture = texture->HardwareTexture();
	if (hwTexture == nullptr || image.Empty())
		return;

	hwTexture->UploadLayer(texture->ArrayIndex, image.Pixels.data(), image.Width(), image.Height());
	hwTexture->GenerateMipmaps();
}

GLLegacyTexture* LegacyGLTextureManager::GenerateTexture(const TextureImage& image, const FString& name, ResourceNamespace resourceNamespace, TextureFlags flags)
{
	// The texture size is the logical (map unit) size, the layer size the pixel size.
	auto texture = std::make_unique<GLLegacyTexture>(NextTextureId(), name, image.GetLogicalDimension(), image.Offset, image.Namespace, TextureTarget::Texture2D,
		image.TransparentPixelCount(), image.BlankRowsFromTop, image.BlankRowsFromBottom, nullptr);
	texture->ImageDimension = image.Dimension;
	UploadAndSetParameters(texture.get(), image, name, resourceNamespace, flags);

	return Own(std::move(texture));
}

std::vector<GLLegacyTexture*> LegacyGLTextureManager::GenerateTextureArray(const GetImageFunc& getImage, int imageLength, Dimension dimension, ResourceNamespace resourceNamespace,
	TextureFlags flags, TextureContext textureContext, GLLegacyTexture*& arrayTexture)
{
	std::vector<GLLegacyTexture*> textures((size_t)imageLength, nullptr);
	int textureId = NextTextureId();

	FString arrayName;
	arrayName.Format("Texture Array %d %dx%d Length=%d", (int)textureContext, dimension.Width, dimension.Height, imageLength);

	IHardwareTexture* hwTexture = screen->CreateTextureArray(dimension.Width, dimension.Height, imageLength, 4, false, !PaletteColorMode(), arrayName.GetChars());
	if (hwTexture == nullptr)
		I_FatalError("LegacyGLTextureManager: the backend cannot create texture arrays (%s)", arrayName.GetChars());

	auto parentArray = std::make_unique<GLLegacyTexture>(textureId, arrayName, dimension, Vec2I(0, 0), resourceNamespace, TextureTarget::Texture2DArray, 0, 0, 0,
		hwTexture, true, textureContext);
	arrayTexture = parentArray.get();
	Own(std::move(parentArray));

	flags = GetFlagsForNamespace(resourceNamespace, flags);

	for (int i = 0; i < imageLength; i++)
	{
		ArrayTextureImage arrayImage = getImage(i);
		const TextureImage& image = *arrayImage.Image;
		assert(image.Dimension.Width == dimension.Width && image.Dimension.Height == dimension.Height && "Incorrect array texture sub image dimension");

		hwTexture->UploadLayer(i, image.Pixels.data(), image.Width(), image.Height());

		FString subName;
		subName.Format("Sub Image %d", i);
		auto texture = std::make_unique<GLLegacyTexture>(textureId, subName, arrayImage.LogicalDimension, arrayImage.Offset, resourceNamespace, TextureTarget::Texture2DArray,
			arrayImage.TransparentPixelCount, arrayImage.BlankRowsFromTop, arrayImage.BlankRowsFromBottom, nullptr, false);
		texture->ArrayIndex = i;
		texture->ParentArrayTexture = arrayTexture;
		texture->ImageDimension = arrayImage.OriginalDimension;
		texture->Flags = flags;
		textures[i] = Own(std::move(texture));
	}

	hwTexture->GenerateMipmaps();

	arrayTexture->Flags = flags;
	arrayTexture->ImageDimension = dimension;
	arrayTexture->IsParentArray = true;
	return textures;
}

void LegacyGLTextureManager::Dispose()
{
	if (m_disposed)
		return;

	GLTextureManager::Dispose();
	m_disposed = true;
}

}
