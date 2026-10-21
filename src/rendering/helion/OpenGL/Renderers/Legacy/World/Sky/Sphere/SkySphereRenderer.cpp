/*
** SkySphereRenderer.cpp
**
** Port of Helion.Render.OpenGL.Renderers.Legacy.World.Sky.Sphere.SkySphereRenderer.
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
#include <vector>
#include "c_cvars.h"
#include "g_level.h"
#include "g_levellocals.h"
#include "gametexture.h"
#include "hw_renderstate.h"
#include "r_defs.h"
#include "texturemanager.h"
#include "v_video.h"
#include "SkySphereRenderer.h"
#include "SphereTable.h"
#include "BindTextures.h"
#include "OpenGL/Renderers/Legacy/World/Sky/SkyProjection.h"
#include "OpenGL/Renderers/Legacy/World/Shader/ShaderVars.h"
#include "OpenGL/Shader/ProgramUniforms.h"
#include "OpenGL/Shader/ProgramRegistry.h"

EXTERN_CVAR(Int, r_distance_cull_type)
EXTERN_CVAR(Color, gl_cullcolor)
CUSTOM_CVARD(Int, gl_cull_skyfog, 255, CVAR_ARCHIVE | CVAR_GLOBALCONFIG, "Sky Fog (1-255) to use when r_distance_cull_type > 1. Overlays over skymist layer. Hardware renderer only.")
{
	if (self < 1)
		self = 1;
	else if (self > 255)
		self = 255;
}

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Sky::Sphere
{

using namespace ::Helion::Geometry;
using Helion::Resources::Definitions::SkyOptions;
using Texture::Legacy::GLLegacyTexture;

static std::vector<SkySphereVertex>& SpherePoints()
{
	static std::vector<SkySphereVertex> points;
	if (!points.empty())
		return points;

	SphereTable sphereTable(SkySphereRenderer::HorizontalSpherePoints, SkySphereRenderer::VerticalSpherePoints);
	points.reserve((size_t)SkySphereRenderer::VerticalSpherePoints * SkySphereRenderer::HorizontalSpherePoints * 6);
	for (int row = 0; row < SkySphereRenderer::VerticalSpherePoints; row++)
	{
		for (int col = 0; col < SkySphereRenderer::HorizontalSpherePoints; col++)
		{
			// Note that this works fine with the +1, it will not go
			// out of range because we specifically made sure that the
			// code adds in one extra vertex for us on both the top row
			// and the right column.
			SkySphereVertex bottomLeft = sphereTable.MercatorRectangle(row, col);
			SkySphereVertex bottomRight = sphereTable.MercatorRectangle(row, col + 1);
			SkySphereVertex topLeft = sphereTable.MercatorRectangle(row + 1, col);
			SkySphereVertex topRight = sphereTable.MercatorRectangle(row + 1, col + 1);

			points.push_back(topLeft);
			points.push_back(bottomLeft);
			points.push_back(topRight);

			points.push_back(topRight);
			points.push_back(bottomLeft);
			points.push_back(bottomRight);
		}
	}
	return points;
}

static std::unique_ptr<SkySphereShared> s_shared;

SkySphereShared& SkySphereShared::Get()
{
	if (s_shared != nullptr)
		return *s_shared;

	auto shared = std::make_unique<SkySphereShared>();
	shared->Sky = OpenGL::Shader::HelionProgramRegistry::Get<SkySphereShader>(OpenGL::Shader::ProgramType::SkySphere);
	shared->Foreground = OpenGL::Shader::HelionProgramRegistry::Get<SkySphereForegroundShader>(OpenGL::Shader::ProgramType::SkySphereForeground);
	shared->Fog = OpenGL::Shader::HelionProgramRegistry::Get<SkySphereFogShader>(OpenGL::Shader::ProgramType::SkySphereFog);
	shared->Geometry = OpenGL::Shader::HelionProgramRegistry::Get<SkySphereGeometryShader>(OpenGL::Shader::ProgramType::SkySphereGeometry);

	const std::vector<SkySphereVertex>& points = SpherePoints();
	shared->Vbo = std::make_unique<Buffer::Array::Vertex::StaticVertexBuffer<SkySphereVertex>>("Sky sphere", (int)points.size());
	shared->Vbo->AddMemoryCopy(std::span<const SkySphereVertex>(points.data(), points.size()));
	shared->Vbo->UploadIfNeeded();

	OpenGL::Shader::RenderProgram* programs[] = { shared->Sky, shared->Foreground, shared->Fog };
	shared->Pipeline = std::make_unique<VertexPipeline<SkySphereVertex>>(std::span<OpenGL::Shader::RenderProgram* const>(programs), *shared->Vbo, "Sky sphere");

	s_shared = std::move(shared);
	return *s_shared;
}

void SkySphereShared::Release()
{
	s_shared.reset();
}

SkySphereRenderer::SkySphereRenderer(Texture::Legacy::LegacyGLTextureManager* textureManager, const SkyInfo& info)
	: m_textureManager(textureManager), m_texture(textureManager), m_info(info)
{
}

void SkySphereRenderer::Render(Shared::RenderInfo& renderInfo)
{
	FLevelLocals* level = renderInfo.ViewSector != nullptr ? renderInfo.ViewSector->Level : primaryLevel;
	SkySphereShared& shared = SkySphereShared::Get();
	FRenderState& state = *screen->RenderState();

	int mainTextureIndex = m_info.Texture != SkyInfo::NoTexture ? m_info.Texture : level->skytexture1.GetIndex();
	SkyTexture skyTexture = m_texture.GetSkyTexture(mainTextureIndex);
	SkyTransformTexture skyTransform = CreateTransform(mainTextureIndex, m_info.ScrollLayer, level);
	bool tiled = (level->flags & LEVEL_FORCETILEDSKY) != 0;

	// HWSkyPortal::DrawContents: the main layer (opaque, with the caps), then the front layer of a double sky
	// translucent over it. Helion's Flip is the normal sky orientation, i.e. not HWSkyInfo::mirrored.
	shared.Sky->Bind();
	state.DisableBlend();
	SetSkyUniforms(*shared.Sky, renderInfo, skyTexture, skyTransform, (m_info.Options & SkyOptions::Flip) == SkyOptions::None, tiled);
	DrawSphere(shared, skyTexture.GlTexture);
	shared.Sky->Unbind();

	if (m_info.ForegroundTexture != SkyInfo::NoTexture)
	{
		// UZDoom double sky: the front layer is scrolled with the first sky speed and never mirrored.
		SkyTexture foregroundTexture = m_texture.GetSkyTexture(m_info.ForegroundTexture);
		SkyTransformTexture foregroundTransform = CreateTransform(m_info.ForegroundTexture, 0, level);
		foregroundTransform.Offset = Vec2F(0, 0);

		shared.Foreground->Bind();
		state.SetCustomBlend(RBF_SrcAlpha, RBF_OneMinusSrcAlpha, RBF_One, RBF_OneMinusSrcAlpha);
		SetSkyUniforms(*shared.Foreground, renderInfo, foregroundTexture, foregroundTransform, false, tiled);
		DrawSphere(shared, foregroundTexture.GlTexture);
		shared.Foreground->Unbind();
		state.DisableBlend();
	}

	RenderFog(m_textureManager, m_info.FadeColor, renderInfo, level);
}

// Helion: SkyTransform.Sky / .Foreground come from the SKYDEFS; here they are built from the level (R_UpdateSky's
// hw_sky1pos / hw_sky2pos in degrees) and the sky info (sky transfer offsets). FLevelLocals::skystretch
// (r_skymode 1) is not applied: the hardware renderer never used it (only the removed software renderer did).
SkyTransformTexture SkySphereRenderer::CreateTransform(int textureIndex, int scrollLayer, FLevelLocals* level) const
{
	SkyTransformTexture transform;
	transform.TextureIndex = textureIndex;

	FGameTexture* texture = TexMan.GetGameTexture(FSetTextureID(textureIndex), true);
	if (texture != nullptr)
		transform.SkyOffset = (float)texture->GetSkyOffset();

	if (scrollLayer == 0)
		transform.ScrollDegrees = level->hw_sky1pos;
	else if (scrollLayer == 1)
		transform.ScrollDegrees = level->hw_sky2pos;

	transform.Offset = m_info.Offset;
	return transform;
}

mat4 SkySphereRenderer::CalculateMvp(const Shared::RenderInfo& renderInfo)
{
	// We want the sky sphere to not be touching the NDC edges because
	// we'll be doing some translating which could push it outside of
	// the clipping box. Therefore we shrink the unit sphere from r = 1
	// down to r = 0.5 around the origin.
	mat4 model;
	model.loadIdentity();
	model.scale(0.5f, 0.5f, 0.5f);

	// Our world system is in the form <X, Z, -Y> with respect to
	// the OpenGL coordinate transformation system. We will also move
	// our body upwards by 20% (so 0.1 units since r = 0.5) so prevent
	// the horizon from appearing.
	Vec3F direction = renderInfo.Camera->Direction;
	Vec3F pos(0.0f, 0.1f, 0.0f);
	Vec3F eye(direction.X, direction.Z, -direction.Y);
	mat4 view;
	view.loadIdentity();
	view.lookAt(pos.X, pos.Y, pos.Z, pos.X + eye.X, pos.Y + eye.Y, pos.Z + eye.Z, 0, 1, 0);

	// Our projection far plane only goes as far as the scaled sphere
	// radius.
	mat4 projection = SkyProjection(renderInfo, 0.0f, 0.5f);
	projection.multMatrix(view);
	projection.multMatrix(model);
	return projection;
}

void SkySphereRenderer::DrawSphere(SkySphereShared& shared, GLLegacyTexture* texture)
{
	texture->Bind(BindTextures::BoundTexture);
	shared.Pipeline->Bind();
	shared.Pipeline->DrawArrays();
	shared.Pipeline->Unbind();
	GLLegacyTexture::Unbind(BindTextures::BoundTexture);
}

// The cap colours are those of skyTexture; the foreground program has no caps.
void SkySphereRenderer::SetSkyUniforms(SkySphereShader& skyProgram, Shared::RenderInfo& renderInfo, const SkyTexture& skyTexture,
	const SkyTransformTexture& skyTransform, bool mirror, bool tiled)
{
	bool invulnerability = renderInfo.Uniforms.DrawInvulnerability;

	Dimension dimension = skyTexture.GlTexture->Dimension();
	if (dimension.Width <= 0 || dimension.Height <= 0)
		dimension = SkySphereTexture::StandardDimension;
	SkyDome dome = SkySphereTexture::CalcDome(dimension, skyTransform, mirror, tiled, renderInfo.PixelStretch);

	skyProgram.BoundTexture(BindTextures::BoundTexture);
	skyProgram.ColormapTexture(BindTextures::Colormap);
	skyProgram.Mvp(CalculateMvp(renderInfo));
	skyProgram.DomeTexture(dome.Texture);
	skyProgram.DomeModel(dome.Model);
	skyProgram.ColorMix(renderInfo.Uniforms.ColorMix.Sky);
	skyProgram.GammaCorrection(renderInfo.Uniforms.GammaCorrection);

	if (Shader::ShaderVars::PaletteColorMode)
	{
		skyProgram.TopColor(FVector4(skyTexture.TopColorIndex / 255.0f, 0, 0, 0));
		skyProgram.BottomColor(FVector4(skyTexture.BottomColorIndex / 255.0f, 0, 0, 0));
	}
	else
	{
		skyProgram.TopColor(skyTexture.TopColor);
		skyProgram.BottomColor(skyTexture.BottomColor);
	}

	skyProgram.HasInvulnerability(invulnerability);
	skyProgram.PaletteIndex((int)renderInfo.Uniforms.PaletteIndex);
	skyProgram.ColorMapIndex(renderInfo.Uniforms.ColorMapUniforms.SkyIndex);
}

// UZDoom HWSkyPortal::DrawContents, the part after the layers: the fade colour of the sector over the sky, or the
// skymist texture tinted with it.
void SkySphereRenderer::RenderFog(Texture::Legacy::LegacyGLTextureManager* textureManager, uint32_t fadeColorValue, Shared::RenderInfo& renderInfo, FLevelLocals* level)
{
	// SkyInfo::FadeColor is already resolved like HWSkyInfo::init does (sector fade colour, or gl_cullcolor with
	// r_distance_cull_type > 1) and has its alpha byte set when a fog layer is wanted at all.
	if ((fadeColorValue >> 24) == 0)
		return;

	PalEntry fadeColor = fadeColorValue;
	if (r_distance_cull_type < 2)
	{
		if (level->skyfog <= 0 || (fadeColorValue & 0xffffff) == 0)
			return;
		fadeColor.a = (uint8_t)std::clamp<int>(level->skyfog, 0, 255);
	}
	else
	{
		fadeColor.a = (uint8_t)std::clamp<int>(gl_cull_skyfog, 0, 255);
	}

	SkySphereShared& shared = SkySphereShared::Get();
	FRenderState& state = *screen->RenderState();

	GLLegacyTexture* mistTexture = nullptr;
	FGameTexture* mist = TexMan.GetGameTexture(level->skymisttexture, true);
	bool useMist = (level->flags3 & LEVEL3_SKYMIST) && mist != nullptr && !Shader::ShaderVars::PaletteColorMode;

	FVector4 mistParams(1, 1, 0, 0);
	if (useMist)
	{
		float misth = (float)mist->GetDisplayHeight();
		float myscale = level->hw_skymistyscale;
		mistParams = FVector4(1.0f, myscale == 0.0f ? 0.0f : 240.0f / misth / myscale, (myscale - 1.0f) * 0.857f, level->hw_skymistpos / 360.0f);
		mistTexture = textureManager->GetTexture(mist->GetID(), false);
	}
	else
	{
		mistTexture = textureManager->WhiteTexture;
	}

	shared.Fog->Bind();
	// Translucent.
	state.SetCustomBlend(RBF_SrcAlpha, RBF_OneMinusSrcAlpha, RBF_One, RBF_OneMinusSrcAlpha);
	shared.Fog->Mvp(CalculateMvp(renderInfo));
	shared.Fog->FlipU(true);
	shared.Fog->BoundTexture(BindTextures::BoundTexture);
	shared.Fog->FogColor(FVector4(fadeColor.r / 255.0f, fadeColor.g / 255.0f, fadeColor.b / 255.0f, fadeColor.a / 255.0f));
	shared.Fog->UseTexture(useMist);
	shared.Fog->MistParams(mistParams);
	shared.Fog->ColorMix(Vec3F(1, 1, 1));
	shared.Fog->GammaCorrection(renderInfo.Uniforms.GammaCorrection);
	DrawSphere(shared, mistTexture);
	shared.Fog->Unbind();
}

}
