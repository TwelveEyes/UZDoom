/*
** DecalRenderer.cpp
**
** UZDoom-only (plan M4.5), see DecalRenderer.h. HWWall::ProcessDecal and HWDecal::DrawDecal of hw_decal.cpp.
**
**---------------------------------------------------------------------------
**
** Copyright 1993-1996 id Software
** Copyright 1994-1996 Raven Software
** Copyright 1998-1998 Chi Hoang
** Copyright 2002-2016 Randy Heit
** Copyright 2016 Christoph Oelckers
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
*/

#include <algorithm>
#include "DecalRenderer.h"
#include "doomdata.h"
#include "a_sharedglobal.h"
#include "g_levellocals.h"
#include "g_mapinfo.h"
#include "texturemanager.h"
#include "hw_cvars.h"
#include "hw_renderstate.h"
#include "v_video.h"
#include "BindTextures.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Geometry
{

using Texture::Legacy::GLLegacyTexture;

DecalRenderer::DecalRenderer(DecalProgram* program, Texture::Legacy::LegacyGLTextureManager* textureManager, Data::SortedTranslucentRenderer* sortedTranslucentRenderer) :
	m_program(program),
	m_textureManager(textureManager),
	m_sortedTranslucentRenderer(sortedTranslucentRenderer),
	m_paletteColorMode(textureManager->PaletteColorMode()),
	m_vbo("Decals")
{
	m_pipeline = std::make_unique<VertexPipeline<DecalVertex>>(*program, m_vbo, "Decals");
}

DecalRenderer::~DecalRenderer() = default;

void DecalRenderer::Clear()
{
	m_opaque.Clear();
	m_translucent.Clear();
	m_mirror.Clear();
	m_translucentKeys.Clear();
	m_vbo.Clear();
}

// A style that replaces the destination (STYLE_Normal): drawn in the opaque decal pass, everything else blends.
bool DecalRenderer::IsOpaqueStyle(FRenderStyle style)
{
	return style.BlendOp == STYLEOP_Add && style.SrcAlpha == STYLEALPHA_One && style.DestAlpha == STYLEALPHA_Zero;
}

GLLegacyTexture* DecalRenderer::GetTexture(DBaseDecal* decal, FGameTexture* gameTexture)
{
	// Decals are drawn with CLAMP_XY: the clamped store of the texture. A translated decal uses the translated image in
	// true color mode (GetSpriteRotation resolves the translation, in palette mode it is a colormap).
	int translation = decal->Translation.index();
	if (translation != 0)
	{
		const Texture::SpriteRotation* rotation = m_textureManager->GetSpriteRotation(decal->PicNum, false, translation);
		if (rotation->RenderStore != nullptr)
			return rotation->RenderStore;
	}

	return m_textureManager->GetTexture(gameTexture->GetID().GetIndex(), false);
}

//==========================================================================
//
// HWWall::ProcessDecals
//
//==========================================================================

void DecalRenderer::RenderDecals(const DecalWall& wall, Shared::RenderInfo& renderInfo)
{
	if (wall.Side == nullptr)
		return;

	for (DBaseDecal* decal = wall.Side->AttachedDecals; decal != nullptr; decal = decal->WallNext)
		ProcessDecal(wall, decal, renderInfo);
}

//==========================================================================
//
// HWWall::ProcessDecal
//
//==========================================================================

void DecalRenderer::ProcessDecal(const DecalWall& wall, DBaseDecal* decal, Shared::RenderInfo& renderInfo)
{
	using WallLocation = ::Helion::World::Geometry::Walls::WallLocation;

	side_t* side = wall.Side;
	line_t* line = side->linedef;

	if (decal->RenderFlags & RF_INVISIBLE)
		return;
	if (wall.Location == WallLocation::Middle3D && wall.MaskedTexture)
		return;	// No decals on 3D floors with transparent textures.
	if (line == nullptr)
		return;

	bool flipx = !!(decal->RenderFlags & RF_XFLIP);
	bool flipy = !!(decal->RenderFlags & RF_YFLIP);

	FGameTexture* texture = TexMan.GetGameTexture(decal->PicNum);
	if (texture == nullptr)
		return;

	// the sectors are only used for their texture origin coordinates
	// so we don't need the fake sectors for deep water etc.
	// As this is a completely split wall fragment no further splits are
	// necessary for the decal.
	sector_t* frontsector;

	// for 3d-floor segments use the model sector as reference
	if ((decal->RenderFlags & RF_CLIPMASK) == RF_CLIPMID)
		frontsector = decal->Sector;
	else
		frontsector = wall.FrontSector;

	if (frontsector == nullptr)
		return;

	float zpos;
	switch (decal->RenderFlags & RF_RELMASK)
	{
	default:
		// No valid decal can have this type. If one is encountered anyway
		// it is in some way invalid so skip it.
		return;

	case RF_RELUPPER:
		if (wall.Location != WallLocation::Upper || wall.BackSector == nullptr)
			return;
		if (line->flags & ML_DONTPEGTOP)
			zpos = (float)(decal->Z + frontsector->GetPlaneTexZ(sector_t::ceiling));
		else
			zpos = (float)(decal->Z + wall.BackSector->GetPlaneTexZ(sector_t::ceiling));
		break;

	case RF_RELLOWER:
		if (wall.Location != WallLocation::Lower || wall.BackSector == nullptr)
			return;
		if (line->flags & ML_DONTPEGBOTTOM)
			zpos = (float)(decal->Z + frontsector->GetPlaneTexZ(sector_t::ceiling));
		else
			zpos = (float)(decal->Z + wall.BackSector->GetPlaneTexZ(sector_t::floor));
		break;

	case RF_RELMID:
		if (wall.Location == WallLocation::Upper || wall.Location == WallLocation::Lower)
			return;
		if (line->flags & ML_DONTPEGBOTTOM)
			zpos = (float)(decal->Z + frontsector->GetPlaneTexZ(sector_t::floor));
		else
			zpos = (float)(decal->Z + frontsector->GetPlaneTexZ(sector_t::ceiling));
		break;
	}

	// now clip the decal to the actual polygon

	float decalwidth = texture->GetDisplayWidth() * (float)decal->ScaleX;
	float decalheight = texture->GetDisplayHeight() * (float)decal->ScaleY;
	float decallefto = texture->GetDisplayLeftOffset() * (float)decal->ScaleX;
	float decaltopo = texture->GetDisplayTopOffset() * (float)decal->ScaleY;

	float leftedge = wall.FracLeft * side->TexelLength;
	float linelength = wall.FracRight * side->TexelLength - leftedge;
	if (linelength <= 0)
		return;

	// texel index of the decal's left edge
	float decalpixpos = (float)side->TexelLength * (float)decal->LeftDistance - (flipx ? decalwidth - decallefto : decallefto) - leftedge;

	float left, right;
	float lefttex, righttex;

	// decal is off the left edge
	if (decalpixpos < 0)
	{
		left = 0;
		lefttex = -decalpixpos;
	}
	else
	{
		left = decalpixpos;
		lefttex = 0;
	}

	// decal is off the right edge
	if (decalpixpos + decalwidth > linelength)
	{
		right = linelength;
		righttex = right - decalpixpos;
	}
	else
	{
		right = decalpixpos + decalwidth;
		righttex = decalwidth;
	}
	if (right <= left)
		return;	// nothing to draw

	// one texture unit on the wall as vector
	float vx = (wall.X2 - wall.X1) / linelength;
	float vy = (wall.Y2 - wall.Y1) / linelength;

	struct DV { float x, y, z, u, v; };
	DV dv[4];
	enum { LL, UL, LR, UR };
	dv[UL].x = dv[LL].x = wall.X1 + vx * left;
	dv[UL].y = dv[LL].y = wall.Y1 + vy * left;

	dv[LR].x = dv[UR].x = wall.X1 + vx * right;
	dv[LR].y = dv[UR].y = wall.Y1 + vy * right;

	zpos += (flipy ? decalheight - decaltopo : decaltopo);

	dv[UL].z = dv[UR].z = zpos;
	dv[LL].z = dv[LR].z = dv[UL].z - decalheight;
	dv[UL].v = dv[UR].v = 0.f;

	float decalscale = float(decal->ScaleX * texture->GetDisplayWidth());
	dv[UL].u = dv[LL].u = lefttex / decalscale;
	dv[LR].u = dv[UR].u = righttex / decalscale;
	dv[LL].v = dv[LR].v = 1.f;

	// now clip to the top plane
	float vzt = (wall.ZTopRight - wall.ZTopLeft) / linelength;
	float topleft = wall.ZTopLeft + vzt * left;
	float topright = wall.ZTopLeft + vzt * right;

	// completely below the wall
	if (topleft < dv[LL].z && topright < dv[LR].z)
		return;

	if (topleft < dv[UL].z || topright < dv[UR].z)
	{
		// decal has to be clipped at the top
		// let texture clamping handle all extreme cases
		dv[UL].v = (dv[UL].z - topleft) / (dv[UL].z - dv[LL].z) * dv[LL].v;
		dv[UR].v = (dv[UR].z - topright) / (dv[UR].z - dv[LR].z) * dv[LR].v;
		dv[UL].z = topleft;
		dv[UR].z = topright;
	}

	// now clip to the bottom plane
	float vzb = (wall.ZBottomRight - wall.ZBottomLeft) / linelength;
	float bottomleft = wall.ZBottomLeft + vzb * left;
	float bottomright = wall.ZBottomLeft + vzb * right;

	// completely above the wall
	if (bottomleft > dv[UL].z && bottomright > dv[UR].z)
		return;

	if (bottomleft > dv[LL].z || bottomright > dv[LR].z)
	{
		// decal has to be clipped at the bottom
		// let texture clamping handle all extreme cases
		dv[LL].v = (dv[UL].z - bottomleft) / (dv[UL].z - dv[LL].z) * (dv[LL].v - dv[UL].v) + dv[UL].v;
		dv[LR].v = (dv[UR].z - bottomright) / (dv[UR].z - dv[LR].z) * (dv[LR].v - dv[UR].v) + dv[UR].v;
		dv[LL].z = bottomleft;
		dv[LR].z = bottomright;
	}

	if (flipx)
	{
		for (int i = 0; i < 4; i++)
			dv[i].u = 1.f - dv[i].u;
	}
	if (flipy)
	{
		for (int i = 0; i < 4; i++)
			dv[i].v = 1.f - dv[i].v;
	}

	GLLegacyTexture* glTexture = GetTexture(decal, texture);

	Decal gldecal;
	gldecal.Side = side;
	gldecal.Texture = glTexture;
	gldecal.Style = decal->RenderStyle;
	gldecal.Alpha = (float)decal->Alpha;

	if (decal->RenderFlags & RF_FULLBRIGHT)
	{
		// lightlevel = 255, rellight = 0: the light buffer value is replaced by the highest light level.
		gldecal.LightOffset = 256.0f;
		gldecal.NoDynamicLights = true;
	}
	else
	{
		gldecal.LightOffset = wall.LightLevelOffset;
	}
	gldecal.LightBufferIndex = wall.LightBufferIndex;
	gldecal.Decolorize = !!(frontsector->Level->flags3 & LEVEL3_NOCOLOREDSPRITELIGHTING);

	// HWDecal::DrawDecal: colour and mode of the render style.
	PalEntry decalColor;
	// alpha color only has an effect when using an alpha texture.
	if (decal->RenderStyle.Flags & (STYLEF_RedIsAlpha | STYLEF_ColorIsFixed))
		decalColor = decal->AlphaColor | 0xff000000;
	else
		decalColor = 0xffffffff;

	if (!renderInfo.DrawFullBright)
		decalColor = decalColor.Modulate(frontsector->SpecialColors[sector_t::sprites]);

	gldecal.ObjectColor = decalColor;
	gldecal.TextureMode = Data::SortedTranslucentRenderer::GetTextureMode(decal->RenderStyle);

	// If srcalpha is one it looks better with a higher alpha threshold
	gldecal.AlphaThreshold = decal->RenderStyle.SrcAlpha == STYLEALPHA_One ? (float)gl_mask_sprite_threshold : 0.f;

	// for additively drawn decals we must temporarily set the fog color to black.
	gldecal.BlackFog = decal->RenderStyle.BlendOp == STYLEOP_Add && decal->RenderStyle.DestAlpha == STYLEALPHA_One;

	gldecal.ColorMapIndex = m_paletteColorMode ? (decal->Translation.index() & 0x3FFF) : 0;

	gldecal.VertexIndex = m_vbo.Count();
	for (int i = 0; i < 4; i++)
		m_vbo.Add(DecalVertex(dv[i].x, dv[i].y, dv[i].z, dv[i].u, dv[i].v));

	if (wall.Mirror)
	{
		m_mirror.Push(gldecal);
		return;
	}

	if (IsOpaqueStyle(gldecal.Style))
	{
		m_opaque.Push(gldecal);
		return;
	}

	unsigned index = m_translucent.Push(gldecal);
	float zcenter = zpos - decalheight * 0.5f;
	float centerX = (dv[LL].x + dv[LR].x) * 0.5f;
	float centerY = (dv[LL].y + dv[LR].y) * 0.5f;
	const ::Helion::Geometry::Vec3F& view = renderInfo.Camera->PositionInterpolated;
	float key = Data::SortedTranslucentRenderer::DistanceSquared(centerX, centerY, zcenter, view.X, view.Y, view.Z);
	if (m_vanillaRender)
	{
		m_translucentKeys.Push(key);
		return;
	}

	m_sortedTranslucentRenderer->Add(key, [this, index](Shared::RenderInfo& info)
	{
		FRenderState& state = *screen->RenderState();
		state.SetPolygonOffset(-1, -128);
		DrawDecal(m_translucent[index], info);
		state.ClearDepthBias();
	});
}

//==========================================================================
//
// classic DrawDecals
//
//==========================================================================

void DecalRenderer::RenderOpaque(Shared::RenderInfo& renderInfo)
{
	const bool translucent = m_vanillaRender && m_translucentKeys.Size() > 0;
	if (m_opaque.Size() == 0 && !translucent)
		return;

	FRenderState& state = *screen->RenderState();
	state.SetDepthMask(false);
	state.SetPolygonOffset(-1, -128);

	for (const Decal& decal : m_opaque)
		DrawDecal(decal, renderInfo);

	if (translucent)
	{
		TArray<unsigned> order(m_translucentKeys.Size(), true);
		for (unsigned i = 0; i < order.Size(); i++)
			order[i] = i;
		std::stable_sort(order.begin(), order.end(), [this](unsigned a, unsigned b) { return m_translucentKeys[a] > m_translucentKeys[b]; });
		for (unsigned index : order)
			DrawDecal(m_translucent[index], renderInfo);
	}

	state.ClearDepthBias();
	state.SetDepthMask(true);
	state.ClearCustomBlend();
	state.SetRenderStyle(STYLE_Normal);
}

//==========================================================================
//
// HWWall::DrawDecalsForMirror
// This list will never get long, so this code should be ok.
//
//==========================================================================

void DecalRenderer::RenderMirrorDecals(Shared::RenderInfo& renderInfo, const side_t* side)
{
	if (m_mirror.Size() == 0)
		return;

	FRenderState& state = *screen->RenderState();
	state.SetDepthMask(false);
	state.SetPolygonOffset(-1, -128);

	for (const Decal& decal : m_mirror)
	{
		if (decal.Side == side)
			DrawDecal(decal, renderInfo);
	}

	state.ClearDepthBias();
	state.SetDepthMask(true);
	state.ClearCustomBlend();
	state.SetRenderStyle(STYLE_Normal);
}

void DecalRenderer::SetUniforms(Shared::RenderInfo& renderInfo)
{
	DecalProgram& program = *m_program;
	program.BoundTexture(BindTextures::BoundTexture);
	program.BrightmapTexture(BindTextures::BrightmapTexture);
	program.SectorLightTexture(BindTextures::SectorLight);
	program.ColormapTexture(BindTextures::Colormap);
	program.SectorColormapTexture(BindTextures::SectorColormap);
	program.SectorFogTexture(BindTextures::SectorFog);
	program.Mvp(renderInfo.Uniforms.Mvp);
	program.HasInvulnerability(renderInfo.Uniforms.DrawInvulnerability);
	program.MvpNoPitch(renderInfo.Uniforms.MvpNoPitch);
	program.LightLevelMix(renderInfo.Uniforms.Mix);
	program.ExtraLight(renderInfo.Uniforms.ExtraLightOrColorMapIndex);
	program.DistanceOffset(renderInfo.Uniforms.DistanceOffset);
	program.ColorMix(renderInfo.Uniforms.ColorMix.Global);
	program.PaletteIndex((int)renderInfo.Uniforms.PaletteIndex);
	program.LightMode(renderInfo.Uniforms.LightMode);
	program.GammaCorrection(renderInfo.Uniforms.GammaCorrection);
	program.UseBrightmaps(renderInfo.Uniforms.UseBrightmaps);
	program.UseSectorColor(renderInfo.Uniforms.SectorColor);
	program.UseSectorFog(renderInfo.Uniforms.SectorFogIndex);

	if (ContractUniforms)
		ContractUniforms(program, renderInfo);
}

//==========================================================================
//
// HWDecal::DrawDecal
//
//==========================================================================

void DecalRenderer::DrawDecal(const Decal& decal, Shared::RenderInfo& renderInfo)
{
	DecalProgram& program = *m_program;
	FRenderState& state = *screen->RenderState();

	program.Bind();
	SetUniforms(renderInfo);

	// A translation overrides the colormap, a fixed (invulnerability / nightvision) colormap overrides the translation.
	int globalColorMap = renderInfo.Uniforms.ColorMapUniforms.GlobalIndex;
	program.ColorMapIndex(globalColorMap != 0 ? globalColorMap : decal.ColorMapIndex);

	decal.Texture->Bind(BindTextures::BoundTexture);
	if (m_paletteColorMode)
		GLLegacyTexture::Unbind(BindTextures::BrightmapTexture);

	program.DecalAlpha(decal.Alpha);
	program.DecalLayer(decal.Texture->ArrayIndex);
	program.DecalLightBufferIndex(decal.LightBufferIndex);
	program.DecalLightOffset(decal.LightOffset);
	program.DecalBlackFog(decal.BlackFog);
	program.DecalDecolorize(decal.Decolorize);
	program.ClusterLightsOff(decal.NoDynamicLights);

	const float norm = 1.0f / 255.0f;
	program.DecalStyle(decal.TextureMode, FVector4(decal.ObjectColor.r * norm, decal.ObjectColor.g * norm, decal.ObjectColor.b * norm, decal.ObjectColor.a * norm), decal.AlphaThreshold);

	Data::SortedTranslucentRenderer::SetStyle(state, decal.Style);
	// The world pass leaves back face culling on; the winding of the decal quad depends on the side it is on.
	state.SetCulling(Cull_None);

	m_vbo.UploadIfNeeded();
	m_pipeline->Bind(true);
	m_vbo.DrawArrays(Util::PrimitiveType::TriangleStrip, decal.VertexIndex, 4);
}

}
