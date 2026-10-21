/*
** SkySphereTexture.cpp
**
** Port of Helion.Render.OpenGL.Renderers.Legacy.World.Sky.Sphere.SkySphereTexture.
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
#include <cmath>
#include "bitmap.h"
#include "c_cvars.h"
#include "colormatcher.h"
#include "g_levellocals.h"
#include "g_level.h"
#include "gametexture.h"
#include "textures.h"
#include "texturemanager.h"
#include "SkySphereTexture.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Sky::Sphere
{

using namespace ::Helion::Geometry;

// UZDoom R_GetSkyCapColor (the classic sky dome): the caps are the average of the top / bottom 30 rows (Helion: 24, at most
// half of the texture).
static constexpr int PixelRowsToEvaluate = 30;
// the classic sky dome: 57 world units roughly represent one sky texel for the model translation.
static constexpr float SkyOffsetFactor = 57;

SkyTexture SkySphereTexture::GetSkyTexture(int textureIndex)
{
	SkyTexture result;
	result.GlTexture = m_textureManager->NullTexture;

	FGameTexture* texture = TexMan.GetGameTexture(FSetTextureID(textureIndex), true);
	if (texture == nullptr || !texture->isValid())
		return result;

	// Helion SkySphereTexture.CreateTexture: the sky has a texture of its own, never a layer of a shared wall array
	// (a sky texture used on a wall is packed into one, and the sky programs sample layer 0 with wrapping UVs).
	result.GlTexture = m_textureManager->GetStandaloneTexture(texture->GetID().GetIndex());

	CapColors colors;
	if (!GetCapColors(texture->GetID().GetIndex(), colors))
		return result;

	result.TopColor = colors.Top;
	result.BottomColor = colors.Bottom;
	result.TopColorIndex = colors.TopIndex;
	result.BottomColorIndex = colors.BottomIndex;
	return result;
}

static PalEntry CalculateAverageRowColor(int startY, int exclusiveEndY, const uint32_t* pixels, int width)
{
	int r = 0;
	int g = 0;
	int b = 0;

	for (int y = startY; y < exclusiveEndY; y++)
	{
		for (int x = 0; x < width; x++)
		{
			PalEntry color = pixels[y * width + x];
			r += color.r;
			g += color.g;
			b += color.b;
		}
	}

	int totalPixels = (exclusiveEndY - startY) * width;
	r /= totalPixels;
	g /= totalPixels;
	b /= totalPixels;

	return PalEntry(255, r, g, b);
}

bool SkySphereTexture::GetCapColors(int resolvedIndex, CapColors& colors)
{
	auto it = m_capColors.find(resolvedIndex);
	if (it != m_capColors.end())
	{
		colors = it->second;
		return true;
	}

	FGameTexture* texture = TexMan.GetGameTexture(FSetTextureID(resolvedIndex), false);
	if (texture == nullptr || texture->GetTexture() == nullptr)
		return false;

	FBitmap bitmap = texture->GetTexture()->GetBgraBitmap(nullptr);
	int width = bitmap.GetWidth();
	int height = bitmap.GetHeight();
	const uint32_t* pixels = (const uint32_t*)bitmap.GetPixels();
	if (pixels == nullptr || width <= 0 || height <= 0)
		return false;

	// R_GetSkyCapColor: textures of at most 30 rows use the top colour for both caps.
	int rowsToEvaluate = std::min(height, PixelRowsToEvaluate);
	PalEntry top = CalculateAverageRowColor(0, rowsToEvaluate, pixels, width);
	PalEntry bottom = height > PixelRowsToEvaluate ? CalculateAverageRowColor(height - PixelRowsToEvaluate, height, pixels, width) : top;

	colors.Top = FVector4(top.r / 255.0f, top.g / 255.0f, top.b / 255.0f, 1.0f);
	colors.Bottom = FVector4(bottom.r / 255.0f, bottom.g / 255.0f, bottom.b / 255.0f, 1.0f);
	colors.TopIndex = ColorMatcher.Pick(top.r, top.g, top.b);
	colors.BottomIndex = ColorMatcher.Pick(bottom.r, bottom.g, bottom.b);
	m_capColors[resolvedIndex] = colors;
	return true;
}

// FSkyVertexBuffer::SetupMatrices (the classic sky dome) with xscale = yscale = 0 (the regular sky layers). The
// skyoffset test CVar of the classic sky dome is not carried.
SkyDome SkySphereTexture::CalcDome(Dimension dimension, const SkyTransformTexture& transform, bool mirror, bool tiled, float pixelStretch)
{
	float texw = (float)dimension.Width;
	float texh = (float)dimension.Height;
	float skyOffset = transform.SkyOffset;

	float xscale = texw < 1024.f ? std::floor(1024.f / texw) : 1.f;
	float yscale;
	float modelScaleY;
	float modelTranslateY;
	if (texh <= 128 && tiled)
	{
		modelTranslateY = (-40 + skyOffset) * SkyOffsetFactor;
		modelScaleY = 1.2f * 1.17f;
		yscale = 240.f / texh;
	}
	else if (texh < 128)
	{
		// smaller sky textures must be tiled. We restrict it to 128 sky pixels, though
		modelTranslateY = -1250.f;
		modelScaleY = 128 / 230.f;
		yscale = (float)(128 / (int)texh);	// intentionally left as integer.
	}
	else if (texh < 200)
	{
		modelTranslateY = -1250.f;
		modelScaleY = texh / 230.f;
		yscale = 1.f;
	}
	else if (texh <= 240)
	{
		modelTranslateY = (200 - texh + skyOffset) * SkyOffsetFactor;
		modelScaleY = 1.f + ((texh - 200.f) / 200.f) * 1.17f;
		yscale = 1.f;
	}
	else
	{
		modelTranslateY = (-40 + skyOffset) * SkyOffsetFactor;
		modelScaleY = 1.2f * 1.17f;
		yscale = 240.f / texh;
	}

	// The model matrix rotates the dome by -180 + x_offset degrees; the vertex u of a dome column at the world
	// angle a is (a + x_offset) / 360 (with the 180 degrees of the rotation and of the mirrored x axis of the view
	// cancelling). The texture matrix is scale(mirror ? -xscale : xscale, yscale) * translate(1, y_offset / texh).
	float xOffset = transform.Offset.X + transform.ScrollDegrees;
	SkyDome dome;
	dome.Texture = FVector4(mirror ? -xscale : xscale, yscale, xOffset / 360.f + 1.f, transform.Offset.Y / texh);
	dome.Model = FVector3(modelScaleY, modelTranslateY, pixelStretch);
	return dome;
}

}
