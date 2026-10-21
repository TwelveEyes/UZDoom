/*
** SortedTranslucentRenderer.cpp
**
** UZDoom-only (plan D10 / M4.6), see SortedTranslucentRenderer.h.
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
#include <utility>
#include "SortedTranslucentRenderer.h"
#include "hwrenderer/data/hw_renderstate.h"
#include "v_video.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Data
{

void SortedTranslucentRenderer::SetStyle(FRenderState& state, FRenderStyle style)
{
	// In program mode a custom blend overrides the render style, an item must not inherit the one of a previous program.
	state.ClearCustomBlend();
	state.SetRenderStyle(style);
}

int SortedTranslucentRenderer::GetTextureMode(FRenderStyle style)
{
	if (style.Flags & STYLEF_RedIsAlpha)
		return TM_ALPHATEXTURE;
	if (style.Flags & STYLEF_ColorIsFixed)
		return TM_STENCIL;
	if (style.Flags & STYLEF_InvertSource)
		return TM_INVERSE;
	return TM_NORMAL;
}

void SortedTranslucentRenderer::Add(float sortKey, DrawFunction draw)
{
	Item item;
	item.SortKey = sortKey;
	item.Index = (int)m_items.size();
	item.Draw = std::move(draw);
	m_items.push_back(std::move(item));
}

void SortedTranslucentRenderer::Clear()
{
	m_items.clear();
}

void SortedTranslucentRenderer::Render(Shared::RenderInfo& renderInfo, bool overlay)
{
	if (m_items.empty())
		return;

	// Back to front. BSP order is front to back, so the later an item was emitted the farther it is; otherwise the larger
	// the squared distance the farther. std::stable_sort keeps the emission order of items with the same key.
	if (m_bspMode)
		std::stable_sort(m_items.begin(), m_items.end(), [](const Item& a, const Item& b) { return a.Index > b.Index; });
	else
		std::stable_sort(m_items.begin(), m_items.end(), [](const Item& a, const Item& b) { return a.SortKey > b.SortKey; });

	FRenderState& state = *screen->RenderState();
	if (!overlay)
		state.SetRenderTarget(nullptr);
	state.EnableDepthTest(!overlay);
	state.SetDepthMask(false);
	state.ClearCustomBlend();

	for (Item& item : m_items)
		item.Draw(renderInfo);

	// The next pass of the frame starts from the opaque defaults.
	state.SetDepthMask(true);
	state.ClearDepthBias();
	state.ClearCustomBlend();
}

}
