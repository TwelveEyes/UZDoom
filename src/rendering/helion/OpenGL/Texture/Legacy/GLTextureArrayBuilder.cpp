/*
** GLTextureArrayBuilder.cpp
**
** Port of Helion.Render.OpenGL.Texture.Legacy.GLTextureArrayBuilder.
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

#include <algorithm>
#include <cassert>
#include <unordered_map>
#include <unordered_set>
#include "printf.h"
#include "textures.h"
#include "gametexture.h"
#include "texturemanager.h"
#include "r_data/sprites.h"
#include "GLTextureArrayBuilder.h"

namespace Helion::Render::OpenGL::Texture::Legacy::GLTextureArrayBuilder
{

using namespace Geometry;
using Resources::ResourceNamespace;

namespace
{

// The size of the image a texture is uploaded with: Helion reads texture.Image.Dimension.
Geometry::Dimension ImageDimensionOf(LegacyGLTextureManager& manager, FGameTexture* texture)
{
	bool sprite = texture->GetUseType() == ETextureType::Sprite || texture->GetUseType() == ETextureType::SkinSprite;
	bool upscale = !manager.PaletteColorMode() && shouldUpscale(texture, sprite ? UF_Sprite : UF_Texture) != 0;
	return TextureImage::PredictUpscaledDimension(texture, upscale, manager.PaletteColorMode());
}

bool CanTextureArray(FGameTexture* texture)
{
	// Exclude brightmaps for now. They probably could be made into arrays but like animations the complication may not be worth it.
	// Textures that need the per material path (extra layers, custom shaders) are not arrays either.
	// UZDoom: warped textures (ANIMDEFS warp/warp2, FGameTexture::isWarped) are drawn alone so GLLegacyTexture::Bind can set the warp of the draw.
	if (texture == nullptr || !texture->isValid() || !GLTextureManager::UsesArray(texture) || texture->isWarped() || texture->GetShaderIndex() != SHADER_Default)
		return false;

	FTexture* base = texture->GetTexture();
	return base != nullptr && base->GetWidth() > 0 && base->GetHeight() > 0 && texture->GetBrightmap() == nullptr;
}

void DebugLog(TextureContext textureContext, int totalTextures, int compressed)
{
	static const char* contexts[] = { "Default", "WorldArray", "WorldSprites", "Hud" };
	DPrintf(DMSG_NOTIFY, "Compressed %s textures %d -> %d\n", contexts[(int)textureContext], totalTextures, compressed);
}

void AddTexture(std::vector<FGameTexture*>& textures, std::unordered_set<int>& seen, int index)
{
	FGameTexture* texture = TexMan.GameByIndex(index);
	// A texture that is in more than one of the lists must be one layer of the array only.
	if (CanTextureArray(texture) && seen.insert(index).second)
		textures.push_back(texture);
}

int BuildTextureArray(GLTextureManager& textureManager, std::span<FGameTexture*> textures, TextureContext textureContext, TextureFlags textureFlags, Dimension dimension, bool addToTextureTracker)
{
	const int maxArrayLayers = GLTextureManager::MaxArrayLayers();
	if ((int)textures.size() > maxArrayLayers)
	{
		int remaining = (int)textures.size();
		int offset = 0;
		int total = 0;

		while (remaining > 0)
		{
			int count = std::min(remaining, maxArrayLayers);
			bool createSubArray = textureManager.CreateTextureArray(textures.subspan((size_t)offset, (size_t)count), textureContext, textureFlags, dimension, addToTextureTracker);
			// Upstream: total += createSubArray ? 0 : 1. The counter is only used for the log, count the arrays that were created.
			total += createSubArray ? 1 : 0;

			offset += count;
			remaining -= count;
		}

		return total;
	}

	bool arrayTexture = textureManager.CreateTextureArray(textures, textureContext, textureFlags, dimension, addToTextureTracker);
	return arrayTexture ? 1 : 0;
}

int BuildTextureArrayFromTextures(LegacyGLTextureManager& renderTextureManager, std::vector<FGameTexture*>& textures, TextureContext textureContext, TextureFlags textureFlags, bool addToTextureTracker = false)
{
	// The sort key is the dimension of the image: cache it, loading is not needed for it.
	std::unordered_map<FGameTexture*, Dimension> dimensions;
	for (FGameTexture* texture : textures)
		dimensions[texture] = ImageDimensionOf(renderTextureManager, texture);

	std::stable_sort(textures.begin(), textures.end(), [&](FGameTexture* x, FGameTexture* y)
	{
		Dimension dx = dimensions[x], dy = dimensions[y];
		if (dx.Height == dy.Height)
			return dx.Width < dy.Width;
		return dx.Height < dy.Height;
	});

	int textureCount = 0;
	std::vector<FGameTexture*> arrayTextures;
	Dimension dimension(0, 0);
	for (FGameTexture* texture : textures)
	{
		Dimension textureDimension = dimensions[texture];
		if (dimension.Width != textureDimension.Width || dimension.Height != textureDimension.Height)
		{
			if (!arrayTextures.empty())
				textureCount += BuildTextureArray(renderTextureManager, arrayTextures, textureContext, textureFlags, dimension, addToTextureTracker);
			arrayTextures.clear();
			dimension = textureDimension;
		}

		arrayTextures.push_back(texture);
	}

	if (!arrayTextures.empty())
		textureCount += BuildTextureArray(renderTextureManager, arrayTextures, textureContext, textureFlags, dimension, addToTextureTracker);

	return textureCount;
}

std::vector<TextureBucket> FinalizeTextureBuckets(std::vector<TextureBucket>& buckets)
{
	const int maxArrayLayers = GLTextureManager::MaxArrayLayers();
	std::vector<TextureBucket> finalizedBuckets;
	for (TextureBucket& bucket : buckets)
	{
		if ((int)bucket.Textures.size() <= maxArrayLayers)
		{
			finalizedBuckets.push_back(std::move(bucket));
			continue;
		}

		int remaining = (int)bucket.Textures.size();
		int offset = 0;

		while (remaining > 0)
		{
			int count = std::min(remaining, maxArrayLayers);
			TextureBucket newBucket;
			newBucket.Dimension = bucket.Dimension;
			for (int i = 0; i < count; i++)
				newBucket.Textures.push_back(bucket.Textures[offset + i]);

			finalizedBuckets.push_back(std::move(newBucket));

			offset += count;
			remaining -= count;
		}
	}

	return finalizedBuckets;
}

std::vector<TextureBucket> CreateTextureBuckets(LegacyGLTextureManager& manager, const std::vector<FGameTexture*>& textures, int baseSize, int bucketCount)
{
	if (baseSize <= 0)
		I_FatalError("Invalid baseSize %d", baseSize);
	if (bucketCount <= 0)
		I_FatalError("Invalid bucketCount %d", bucketCount);

	std::vector<TextureBucket> buckets((size_t)bucketCount + 1);

	for (int i = 0; i < bucketCount; i++)
	{
		int size = baseSize * (1 << i);
		buckets[i].Dimension = Dimension(size, size);
		buckets[i].Textures.reserve(128);
	}

	int overflowBucketIndex = (int)buckets.size() - 1;
	buckets[overflowBucketIndex].Dimension = Dimension(0, 0);

	// Deviation: Helion puts images larger than the last bucket into the last bucket, where they do not fit.
	// They are not put into an array here.
	const int maxBucketSize = baseSize * (1 << (bucketCount - 1));
	for (FGameTexture* texture : textures)
	{
		Dimension dimension = CanTextureArray(texture) ? ImageDimensionOf(manager, texture) : Dimension(0, 0);
		bool fits = dimension.Width > 0 && dimension.Height > 0 && std::max(dimension.Width, dimension.Height) <= maxBucketSize;
		int index = fits ? GetBucketIndex(dimension, baseSize, bucketCount) : overflowBucketIndex;
		buckets[index].Textures.push_back(texture);
	}

	// Split buckets that overflow GLInfo.MaxArrayLayers
	return FinalizeTextureBuckets(buckets);
}

}

void BuildLevel(LegacyGLTextureManager& glTextureManager, std::span<const int> flatTextures, std::span<const int> wallTexturesRepeat, std::span<const int> wallTexturesClamp)
{
	glTextureManager.DestroyTextureArrays(TextureContext::WorldArray);
	glTextureManager.ResetAnimatedTextures();

	// Animations are excluded. This leaves them reliant on their previous behavior of swapping textures.
	int buildTextures = 0;
	int arrayTextures = 0;
	std::vector<FGameTexture*> textures;
	std::unordered_set<int> seen;
	textures.reserve(1024);
	for (int index : flatTextures)
	{
		if (!glTextureManager.IsTextureAnimated(index))
			AddTexture(textures, seen, index);
	}

	for (int index : wallTexturesRepeat)
	{
		if (!glTextureManager.IsTextureAnimated(index))
			AddTexture(textures, seen, index);
	}

	buildTextures += (int)textures.size();
	arrayTextures += BuildTextureArrayFromTextures(glTextureManager, textures, TextureContext::WorldArray, TextureFlags::Default);

	textures.clear();
	seen.clear();
	for (int index : wallTexturesClamp)
	{
		if (!glTextureManager.IsTextureAnimated(index))
			AddTexture(textures, seen, index);
	}

	buildTextures += (int)textures.size();
	arrayTextures += BuildTextureArrayFromTextures(glTextureManager, textures, TextureContext::WorldArray, TextureFlags::ClampY);

	int totalTextures = (int)(flatTextures.size() + wallTexturesRepeat.size() + wallTexturesClamp.size());
	int animated = totalTextures - buildTextures;
	DebugLog(TextureContext::WorldArray, totalTextures, arrayTextures + animated);
}

TextureBuckets BuildSprites(LegacyGLTextureManager& glTextureManager, const TArray<spritedef_t>& spriteDefinitions)
{
	int maxIndex = 0;
	std::vector<FGameTexture*> spriteTextures;
	std::unordered_set<int> spriteTextureHandles;
	std::vector<FTextureID> spriteTextureIds;
	for (const spritedef_t& spriteDefinition : spriteDefinitions)
	{
		for (int i = 0; i < spriteDefinition.numframes; i++)
		{
			const spriteframe_t& frame = SpriteFrames[spriteDefinition.spriteframes + i];
			for (int j = 0; j < 16; j++)
			{
				FTextureID id = frame.Texture[j];
				if (!id.isValid())
					continue;

				FGameTexture* texture = TexMan.GetGameTexture(id);
				if (texture == nullptr || !texture->isValid())
					continue;

				if (spriteTextureHandles.insert(texture->GetID().GetIndex()).second)
				{
					spriteTextures.push_back(texture);
					spriteTextureIds.push_back(id);
					maxIndex = std::max(texture->GetID().GetIndex(), maxIndex);
				}
			}
		}
	}

	const int BaseSize = 32;
	const int BucketCount = 5;
	std::vector<TextureBucket> buckets = CreateTextureBuckets(glTextureManager, spriteTextures, BaseSize, BucketCount);
	int textureArrayCount = 0;
	for (size_t i = 0; i + 1 < buckets.size(); i++)
	{
		TextureBucket& bucket = buckets[i];
		if (bucket.Textures.empty())
			continue;

		textureArrayCount += BuildTextureArray(glTextureManager, bucket.Textures, TextureContext::WorldSprites, TextureFlags::ClampX | TextureFlags::ClampY, bucket.Dimension, false);
	}

	// Create the rotations of every sprite texture: the ones that are not in an array (overflow bucket,
	// brightmaps, custom shaders) get their own texture, the ones in an array resolve to their array layer.
	for (FTextureID id : spriteTextureIds)
		glTextureManager.GetSpriteRotation(id, false, 0);

	DebugLog(TextureContext::WorldSprites, (int)spriteTextures.size(), textureArrayCount);
	return TextureBuckets{ maxIndex, std::move(buckets) };
}

void BuildHud(GLTextureManager& renderTextureManager, std::span<IArrayTexture* const> arrayTextures)
{
	std::vector<FGameTexture*> textures;
	textures.reserve(arrayTextures.size());

	std::unordered_map<int, IArrayTexture*> textureLookup;
	for (IArrayTexture* arrayTexture : arrayTextures)
	{
		// Helion tries the global namespace and then the sprites namespace.
		TextureImage image;
		Resources::ResourceNamespace fetched;
		if (!renderTextureManager.TryGetImage(arrayTexture->FetchTextureName(), image) &&
			!renderTextureManager.TryGetImage(arrayTexture->FetchTextureName(), Resources::ResourceNamespace::Sprites, image, fetched))
			continue;

		FTextureID id = TexMan.CheckForTexture(arrayTexture->FetchTextureName().GetChars(), ETextureType::Any, FTextureManager::TEXMAN_TryAny);
		FGameTexture* texture = id.isValid() ? TexMan.GetGameTexture(id) : nullptr;
		if (texture == nullptr || !GLTextureManager::UsesArray(texture))
			continue;

		arrayTexture->SetResolvedHeight(image.Height());
		textures.push_back(texture);
		textureLookup[texture->GetID().GetIndex()] = arrayTexture;
	}

	int textureArrayCount = BuildTextureArrayFromTextures(static_cast<LegacyGLTextureManager&>(renderTextureManager), textures, TextureContext::Hud, TextureFlags::ClampX | TextureFlags::ClampY, true);

	for (FGameTexture* texture : textures)
	{
		GLLegacyTexture* handle = renderTextureManager.GetRenderStore(texture->GetID().GetIndex(), false);
		if (handle == nullptr)
			continue;

		auto findArrayTexture = textureLookup.find(texture->GetID().GetIndex());
		if (findArrayTexture != textureLookup.end())
		{
			findArrayTexture->second->SetHandle(handle);
			renderTextureManager.RegisterTexture(findArrayTexture->second->FetchTextureName(), handle, ResourceNamespace::Undefined);
		}
	}

	DebugLog(TextureContext::Hud, (int)textures.size(), textureArrayCount);
}

int GetBucketIndex(Dimension dimension, int baseSize, int bucketCount)
{
	int size = std::max(dimension.Width, dimension.Height);
	// Fits in the smallest bucket
	if (size <= baseSize)
		return 0;

	int index = 0;
	int current = baseSize;

	while (index < bucketCount - 1 && size > current)
	{
		current *= 2;
		index++;
	}

	return index;
}

}
