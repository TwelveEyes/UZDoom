/*
** SkySphereTexture.h
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
** Differences to Helion:
**  - The textures are the ones of the LegacyGLTextureManager (GetTexture(FTextureID) resolves texture animations,
**    so GetSkyTexture() follows an animated sky without Helion's per animation frame list). Only the cap colours
**    (average of the top / bottom rows) are computed here, once per texture.
**  - SkyTransformTexture is the UZDoom state of one sky layer (offset, scroll) instead of an ID24 SKYDEFS entry.
**    Fire skies and the SKYDEFS MidTexel do not exist in UZDoom.
**  - The texture is placed like the classic sky dome does (CalcDome = FSkyVertexBuffer::SetupMatrices, evaluated
**    per fragment by SkySphereShader::DomeFunctions) instead of Helion's CalcOffset / CalcScale / CalcSkyHeight, so
**    the texture height rules, the texture sky offset and forced tiled skies behave like before. Helion's sky render
**    modes (r_skymode) do not apply: the hardware renderer never read r_skymode.
**
*/

#pragma once

#include <unordered_map>
#include "vectors.h"
#include "OpenGL/Texture/Legacy/LegacyGLTextureManager.h"
#include "Resources/Definitions/SkyOptions.h"
#include "Util/Geometry.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Sky::Sphere
{

// Helion.Resources.SkyTransformTexture
struct SkyTransformTexture
{
	int TextureIndex = -1;
	// x in degrees, y in texture pixels (sky transfer offsets, HWSkyInfo x_offset / y_offset).
	::Helion::Geometry::Vec2F Offset = ::Helion::Geometry::Vec2F(0, 0);
	// Level->hw_sky1pos / hw_sky2pos (degrees, already evaluated at the tic fraction).
	float ScrollDegrees = 0;
	// FGameTexture::GetSkyOffset() of the texture (texture pixels, positive moves the sky up).
	float SkyOffset = 0;
};

// FSkyVertexBuffer::SetupMatrices of one layer, see SkySphereShader::DomeFunctions.
struct SkyDome
{
	// u scale (negative = mirrored), v scale, u offset (turns), v offset (texture heights).
	FVector4 Texture = FVector4(1, 1, 0, 0);
	// Model y scale, model y translation, pixel stretch of the view.
	FVector3 Model = FVector3(1, 0, 1);
};

struct SkyTexture
{
	Texture::Legacy::GLLegacyTexture* GlTexture = nullptr;
	FVector4 TopColor = FVector4(0, 0, 0, 0);
	FVector4 BottomColor = FVector4(0, 0, 0, 0);
	int TopColorIndex = 0;
	int BottomColorIndex = 0;
};

class SkySphereTexture
{
public:
	static inline const ::Helion::Geometry::Dimension StandardDimension = ::Helion::Geometry::Dimension((int)256, (int)128);

	explicit SkySphereTexture(Texture::Legacy::LegacyGLTextureManager* textureManager) : m_textureManager(textureManager) { }

	// The texture to draw for the FTextureID index; animations are resolved every call. Falls back to the null texture.
	SkyTexture GetSkyTexture(int textureIndex);

	static SkyDome CalcDome(::Helion::Geometry::Dimension dimension, const SkyTransformTexture& transform, bool mirror, bool tiled, float pixelStretch);

private:
	struct CapColors
	{
		FVector4 Top;
		FVector4 Bottom;
		int TopIndex;
		int BottomIndex;
	};

	Texture::Legacy::LegacyGLTextureManager* m_textureManager;
	// By the index of the resolved (animation frame) game texture.
	std::unordered_map<int, CapColors> m_capColors;

	bool GetCapColors(int resolvedIndex, CapColors& colors);
};

}
