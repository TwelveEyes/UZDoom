/*
** CoronaRenderer.cpp
**
** UZDoom-only (plan M4.1), see CoronaRenderer.h.
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
#include "CoronaRenderer.h"
#include "BindTextures.h"
#include "a_corona.h"
#include "actor.h"
#include "actorinlines.h"
#include "gametexture.h"
#include "hw_renderstate.h"
#include "p_trace.h"
#include "r_data/sprites.h"
#include "r_utility.h"
#include "texturemanager.h"
#include "v_video.h"
#include "OpenGL/Renderers/Legacy/World/Data/SortedTranslucentRenderer.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Entities
{

using Texture::Legacy::GLLegacyTexture;

// ----------------------------------------------------------------------------------------------------------------
// CoronaRenderer
// ----------------------------------------------------------------------------------------------------------------

namespace
{

ETraceStatus CheckForViewpointActor(FTraceResults& res, void* userdata)
{
	FRenderViewpoint* data = (FRenderViewpoint*)userdata;
	if (res.HitType == TRACE_HitActor && res.Actor && res.Actor == data->ViewActor)
		return TRACE_Skip;

	return TRACE_Stop;
}

}

CoronaRenderer::CoronaRenderer(CoronaProgram* program, Texture::Legacy::LegacyGLTextureManager* textureManager) :
	m_program(program),
	m_textureManager(textureManager),
	m_coronaClass(PClass::FindActor("Corona")),
	m_paletteColorMode(textureManager->PaletteColorMode()),
	m_vbo("Coronas")
{
	m_pipeline = std::make_unique<VertexPipeline<CoronaVertex>>(*program, m_vbo, "Coronas");
}

CoronaRenderer::~CoronaRenderer() = default;

bool CoronaRenderer::IsCorona(AActor* thing) const
{
	return m_coronaClass != nullptr && thing->IsKindOf(m_coronaClass);
}

void CoronaRenderer::AddCorona(AActor* thing)
{
	m_coronas.Push(thing);
}

void CoronaRenderer::Clear()
{
	m_coronas.Clear();
	m_vbo.Clear();
}

// classic DrawCorona + the visibility part of DrawCoronas. Returns false when the corona is not drawn.
bool CoronaRenderer::ProcessCorona(AActor* corona, float timeElapsed, Shared::RenderInfo& renderInfo, DrawItem& item)
{
	const auto& vp = r_viewpoint;

	DVector3 cPos = corona->Vec3Offset(0., 0., corona->Height * 0.5);
	DVector3 direction = vp.Pos - cPos;
	double dist = direction.Length();

	// skip coronas that are too far
	if (dist > corona->RenderRadius())
		return false;

	static const float fadeSpeed = 9.0f;

	CoronaState& state = m_state[corona];
	state.LastFrame = m_frame;

	direction.MakeUnit();
	FTraceResults results;
	if (!Trace(cPos, corona->Sector, direction, dist, MF_SOLID, ML_BLOCKEVERYTHING, corona, results, 0, CheckForViewpointActor, (void*)&vp))
		state.Fade = std::min(state.Fade + timeElapsed * fadeSpeed, 1.0f);
	else
		state.Fade = std::max(state.Fade - timeElapsed * fadeSpeed, 0.0f);

	if (state.Fade <= 0.0f)
		return false;

	bool mirror = false;
	FTextureID patch = sprites[corona->sprite].GetSpriteFrame(corona->frame, 0, nullAngle, &mirror, false);
	if (!patch.isValid())
		return false;
	FGameTexture* tex = TexMan.GetGameTexture(patch, false);
	if (tex == nullptr || !tex->isValid())
		return false;

	const Texture::SpriteRotation* rotation = m_textureManager->GetSpriteRotation(patch, mirror, 0);
	if (rotation->RenderStore == nullptr)
		return false;

	// Project the corona sprite center. The sprite is a camera facing quad of its world size.
	const ::Helion::Geometry::Vec3F& dir = renderInfo.Camera->Direction;
	::Helion::Geometry::Vec3F right = ::Helion::Geometry::Vec3F(dir.Y, -dir.X, 0.0f);
	float rightLength = right.Length();
	if (rightLength <= 0.0001f)
		return false;
	right = right / rightLength;
	// up = right x dir
	::Helion::Geometry::Vec3F up(right.Y * dir.Z - right.Z * dir.Y, right.Z * dir.X - right.X * dir.Z, right.X * dir.Y - right.Y * dir.X);

	float halfWidth = tex->GetDisplayWidth() * (float)corona->Scale.X * 0.5f;
	float halfHeight = tex->GetDisplayHeight() * (float)corona->Scale.Y * 0.5f;

	auto project = [&renderInfo](const ::Helion::Geometry::Vec3F& p, float& x, float& y) -> bool
	{
		float world[4] = { p.X, p.Y, p.Z, 1.0f };
		float clip[4];
		renderInfo.Uniforms.Mvp.multMatrixPoint(world, clip);
		if (clip[3] <= 0.0f)
			return false;
		x = clip[0] / clip[3];
		y = clip[1] / clip[3];
		return true;
	};

	::Helion::Geometry::Vec3F center((float)cPos.X, (float)cPos.Y, (float)cPos.Z);
	float cx, cy, rx, ry, ux, uy;
	if (!project(center, cx, cy) || !project(center + right * halfWidth, rx, ry) || !project(center + up * halfHeight, ux, uy))
		return false;

	float alpha = state.Fade * float(corona->Alpha);

	// distance-based fade - looks better IMO
	float distNearFadeStart = float(corona->RenderRadius()) * 0.1f;
	float distFarFadeStart = float(corona->RenderRadius()) * 0.5f;
	float distFade = 1.0f;

	if (float(dist) < distNearFadeStart)
		distFade -= std::abs(((float(dist) - distNearFadeStart) / distNearFadeStart));
	else if (float(dist) >= distFarFadeStart)
		distFade -= (float(dist) - distFarFadeStart) / distFarFadeStart;

	alpha *= distFade;
	if (alpha <= 0.0f)
		return false;

	// The screen rotates with the roll of the view: the NDC offsets of the right / up axes are the quad axes.
	float rightX = rx - cx, rightY = ry - cy;
	float upX = ux - cx, upY = uy - cy;

	float u0 = mirror ? 1.0f : 0.0f;
	float u1 = mirror ? 0.0f : 1.0f;

	item.Texture = rotation->RenderStore;
	item.Layer = rotation->RenderStore->ArrayIndex;
	item.Alpha = alpha;
	item.Thing = corona;
	item.VertexIndex = m_vbo.Count();

	// triangle strip: lower left, upper left, lower right, upper right
	m_vbo.Add(CoronaVertex{ cx - rightX - upX, cy - rightY - upY, u0, 1.0f });
	m_vbo.Add(CoronaVertex{ cx - rightX + upX, cy - rightY + upY, u0, 0.0f });
	m_vbo.Add(CoronaVertex{ cx + rightX - upX, cy + rightY - upY, u1, 1.0f });
	m_vbo.Add(CoronaVertex{ cx + rightX + upX, cy + rightY + upY, u1, 0.0f });
	return true;
}

void CoronaRenderer::Render(Shared::RenderInfo& renderInfo)
{
	// The fade of a corona advances with the time between two frames, also when none is drawn.
	uint64_t frameTime = screen->FrameTime;
	float timeElapsed = m_lastFrameTime == 0 ? 0.0f : std::min((frameTime - m_lastFrameTime) / 1000.0f, 0.25f);
	m_lastFrameTime = frameTime;
	m_frame++;

	if (!gl_coronas || m_coronas.Size() == 0)
	{
		m_state.clear();
		return;
	}

	TArray<DrawItem> items;
	for (AActor* corona : m_coronas)
	{
		DrawItem item;
		if (ProcessCorona(corona, timeElapsed, renderInfo, item))
			items.Push(item);
	}

	// Forget the fade of coronas that were not handed over this frame (destroyed, out of view).
	for (auto it = m_state.begin(); it != m_state.end();)
	{
		if (it->second.LastFrame != m_frame)
			it = m_state.erase(it);
		else
			++it;
	}

	if (items.Size() == 0)
		return;

	FRenderState& state = *screen->RenderState();
	state.EnableDepthTest(false);
	state.SetDepthMask(false);

	CoronaProgram& program = *m_program;
	program.Bind();
	program.BoundTexture(BindTextures::BoundTexture);
	program.ColormapTexture(BindTextures::Colormap);
	program.PaletteIndex((int)renderInfo.Uniforms.PaletteIndex);

	m_vbo.UploadIfNeeded();
	m_pipeline->Bind(true);

	for (const DrawItem& item : items)
	{
		item.Texture->Bind(BindTextures::BoundTexture);
		program.CoronaLayer(item.Layer);
		program.CoronaAlpha(item.Alpha);
		Data::SortedTranslucentRenderer::SetStyle(state, item.Thing->RenderStyle);
		m_vbo.DrawArrays(Util::PrimitiveType::TriangleStrip, item.VertexIndex, 4);
	}

	state.ClearCustomBlend();
	state.SetRenderStyle(STYLE_Normal);
	state.SetDepthMask(true);
	state.EnableDepthTest(true);
}

}
