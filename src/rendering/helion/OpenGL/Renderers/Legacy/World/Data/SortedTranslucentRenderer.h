/*
** SortedTranslucentRenderer.h
**
** UZDoom-only (plan D10 / M4.6). The Helion shaped sorted back-to-front bucket: every draw that cannot go
** through the weighted blended OIT passes (render styles other than Translucent / Add / Fuzzy / ColorAdd, custom
** material shader surfaces, decals, translucent models, translucent portal planes) registers a draw closure
** with its eye space depth; Render sorts them back to front and runs them after the OIT composite.
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

#pragma once

#include <functional>
#include <vector>
#include "renderstyle.h"
#include "Common/Shared/RenderInfo.h"

class FRenderState;

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Data
{

class SortedTranslucentRenderer
{
public:
	// Draws one item. The renderer has set the render target (the scene framebuffer), depth test and no depth write; the
	// closure binds its own program and texture, sets the blend of its render style with SetStyle and draws. RenderInfo
	// holds the uniforms of the frame (the closure sets them on its program like every Helion renderer does).
	using DrawFunction = std::function<void(Shared::RenderInfo& renderInfo)>;

	// Squared distance from the viewer, the sort key used when the world is not walked in BSP order.
	static float DistanceSquared(float x, float y, float z, float viewX, float viewY, float viewZ)
	{
		float dx = x - viewX, dy = y - viewY, dz = z - viewZ;
		return dx * dx + dy * dy + dz * dz;
	}

	// The blend of a render style for the draws of a program (program mode has no FRenderState render style by default):
	// drops a custom blend a previous item left behind and selects the blend equation of the style, including the
	// fuzz style (the blend of ApplyBlendMode of the table path).
	static void SetStyle(FRenderState& state, FRenderStyle style);

	// The uTextureMode of a render style: TM_ALPHATEXTURE for STYLEF_RedIsAlpha, TM_STENCIL for STYLEF_ColorIsFixed,
	// TM_INVERSE for STYLEF_InvertSource, otherwise TM_NORMAL (FRenderState::SetTextureMode(FRenderStyle)).
	static int GetTextureMode(FRenderStyle style);

	// sortKey is the squared distance of the item from the viewer (DistanceSquared). In BSP mode (SetBspMode(true)) the world is
	// walked front to back, so the key of an item is its emission index and the item emitted last is drawn first.
	void Add(float sortKey, DrawFunction draw);

	// Selects which key a frame uses: the emission order of the BSP walk or the distance.
	void SetBspMode(bool bspMode) { m_bspMode = bspMode; }
	bool BspMode() const { return m_bspMode; }

	void Clear();
	bool Empty() const { return m_items.empty(); }
	int Count() const { return (int)m_items.size(); }

	// Sorts back to front (stable) and runs every item inside the translucent state (scene render target, depth test on,
	// depth write off, no custom blend). Clears nothing: the caller calls Clear() at the start of the next frame.
	// overlay: draws into the current render target without depth test (the 2D step of the player sprites, after the post
	// processing bloom; classic DrawEndScene2D).
	void Render(Shared::RenderInfo& renderInfo, bool overlay = false);

private:
	struct Item
	{
		float SortKey;
		int Index;
		DrawFunction Draw;
	};

	std::vector<Item> m_items;
	bool m_bspMode = false;
};

}
