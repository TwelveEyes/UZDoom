/*
** IRenderableSurfaceContext.h
**
** Port of Helion.Render.Common.Renderers.IRenderableSurfaceContext.
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** The HUD context (HudRenderContext/IHudRenderContext) is not part of the
** hardware scene port: UZDoom draws the HUD through F2DDrawer. The Hud()
** entry point is kept with forward-declared types so the interface stays
** shaped like Helion's.
*/

#pragma once

#include <functional>
#include <optional>
#include "Common/Context/WorldRenderContext.h"
#include "Common/Renderers/IRenderableSurface.h"
#include "Common/Renderers/IWorldRenderContext.h"
#include "World/TransitionType.h"

namespace Helion::Render::Common::Context
{
class HudRenderContext;
}

namespace Helion::Render::Common::Renderers
{

class IHudRenderContext;

/// <summary>
/// A renderable surface context which maintains state while allowing calls
/// to rendering submodules.
/// </summary>
class IRenderableSurfaceContext
{
public:
	virtual ~IRenderableSurfaceContext() = default;

	/// <summary>
	/// The surface this was created from.
	/// </summary>
	virtual IRenderableSurface& Surface() = 0;

	/// <summary>
	/// Clears the color, depth, and stencil buffer based on what the
	/// arguments are set to.
	/// </summary>
	/// <param name="color">The color to clear.</param>
	/// <param name="depth">True to clear the depth buffer.</param>
	/// <param name="stencil">True to clear the stencil buffer.</param>
	virtual void Clear(Geometry::Color color, bool depth, bool stencil) = 0;

	/// <summary>
	/// Clears the depth buffer only.
	/// </summary>
	virtual void ClearDepth() = 0;

	/// <summary>
	/// Clears the stencil buffer only.
	/// </summary>
	virtual void ClearStencil() = 0;

	/// <summary>
	/// Sets the viewport to the area specified.
	/// </summary>
	/// <param name="area">The restricted viewport area.</param>
	virtual void Viewport(const Geometry::Box2I& area) = 0;

	/// <summary>
	/// Sets the viewport to the area specified, runs the action, then
	/// restores back to the area before this call.
	/// </summary>
	/// <param name="area">The restricted viewport area.</param>
	/// <param name="action">The actions to run with the new area.</param>
	virtual void Viewport(const Geometry::Box2I& area, const std::function<void()>& action) = 0;

	/// <summary>
	/// Sets the scissor to the area specified.
	/// </summary>
	/// <param name="area">The restricted scissor area.</param>
	virtual void Scissor(const Geometry::Box2I& area) = 0;

	/// <summary>
	/// Sets the scissor to the area specified, runs the action, then
	/// restores back to the area before this call.
	/// </summary>
	/// <param name="area">The restricted scissor area.</param>
	/// <param name="enable">If scissor should be applied to the restricted area.</param>
	virtual void Scissor(const Geometry::Box2I& area, bool enable) = 0;

	/// <summary>
	/// Begins hud rendering actions.
	/// </summary>
	/// <param name="context">Contextual rendering information.</param>
	/// <param name="action">The hud rendering actions to issue.</param>
	virtual void Hud(Context::HudRenderContext& context, const std::function<void(IHudRenderContext&)>& action) = 0;

	/// <summary>
	/// Begins world rendering actions.
	/// </summary>
	/// <param name="context">Contextual rendering information.</param>
	/// <param name="action">The world rendering actions to issue.</param>
	virtual void World(Context::WorldRenderContext& context, const std::function<void(IWorldRenderContext&)>& action) = 0;

	virtual void Automap(Context::WorldRenderContext& context, const std::function<void(IWorldRenderContext&)>& action) = 0;

	virtual void DrawVirtualFrameBuffer() = 0;

	virtual void HandleTransition(::Helion::World::TransitionType type, std::optional<float> progress, std::optional<bool> init) = 0;
};

}
