/*
** IRenderableSurface.h
**
** Port of Helion.Render.Common.Renderers.IRenderableSurface.
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
#include "Util/Geometry.h"

namespace Helion::Render::Common::Renderers
{

class IRenderableSurfaceContext;

/// <summary>
/// An object that acts as a place where commands can be issued to render
/// upon.
/// </summary>
class IRenderableSurface
{
public:
	/// <summary>
	/// The name the default framebuffer uses.
	/// </summary>
	static inline const char* const DefaultName = "Default";

	virtual ~IRenderableSurface() = default;

	/// <summary>
	/// The width and height of the surface. This will likely be the
	/// size of the native window if it is the default surface, or it
	/// will be the dimension of the surface's texture.
	/// </summary>
	virtual Geometry::Dimension Dimension() const = 0;

	/// <summary>
	/// Invokes rendering of all commands in the action, which should use
	/// the context provided.
	/// </summary>
	/// <param name="action">The rendering actions.</param>
	virtual void Render(const std::function<void(IRenderableSurfaceContext&)>& action) = 0;

	virtual void SetOverrideDimension(const Geometry::Dimension& dimension) = 0;
	virtual void ClearOverrideDimension() = 0;
};

}
