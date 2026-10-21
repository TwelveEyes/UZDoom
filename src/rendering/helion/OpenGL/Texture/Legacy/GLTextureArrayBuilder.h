/*
** GLTextureArrayBuilder.h
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
** Mapping: TextureManager is TexMan (and TexAnim for IsTextureAnimated, kept on the GL manager),
** Resources.Texture is FGameTexture*, SpriteDefinition[] is UZDoom's sprites/SpriteFrames
** (r_data/sprites.h), DynamicArray<T> is std::vector<T>. The functions are free functions of the
** namespace instead of a static class.
**
** BuildSprites does not write RenderStore/BrightmapRenderStore into sprite definitions: the rotations
** are created by GLTextureManager::GetSpriteRotation(texture, mirror, translation) on first use and
** the loop of BuildSprites only warms them up. BuildHud takes the GLTextureManager because the
** IRendererTextureManager interface does not give access to the render stores.
**
** Brightmaps: like Helion, a texture with a brightmap is not put into an array (CanTextureArray);
** it is a one layer array of its own, and its brightmap is a one layer array bound separately
** (GLTextureManager::GetBrightmapTexture, BindTextures::BrightmapTexture).
**
*/

#pragma once

#include <span>
#include <vector>
#include "tarray.h"
#include "Util/Geometry.h"
#include "OpenGL/Texture/Legacy/IArrayTexture.h"
#include "LegacyGLTextureManager.h"

struct spritedef_t;
class FGameTexture;

namespace Helion::Render::OpenGL::Texture::Legacy
{

struct TextureBucket
{
	Geometry::Dimension Dimension;
	std::vector<FGameTexture*> Textures;
};

struct TextureBuckets
{
	int MaxTextureIndex = 0;
	std::vector<TextureBucket> Buckets;
};

namespace GLTextureArrayBuilder
{

void BuildLevel(LegacyGLTextureManager& glTextureManager, std::span<const int> flatTextures, std::span<const int> wallTexturesRepeat, std::span<const int> wallTexturesClamp);

TextureBuckets BuildSprites(LegacyGLTextureManager& glTextureManager, const TArray<spritedef_t>& spriteDefinitions);

void BuildHud(GLTextureManager& renderTextureManager, std::span<IArrayTexture* const> arrayTextures);

int GetBucketIndex(Geometry::Dimension dimension, int baseSize, int bucketCount);

}

}
