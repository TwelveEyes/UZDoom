/*
** IRenderableTextureHandle.h
**
** Port of Helion.Render.Common.Textures.IRenderableTextureHandle.
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

#include "Util/Geometry.h"

namespace Helion::Render::Common::Textures
{

class IRenderableTextureHandle
{
public:
	virtual ~IRenderableTextureHandle() = default;

	/// <summary>
	/// A specific lookup index into a renderer table. Used for quick access
	/// of the texture information. This number is specific to whatever the
	/// rendering implementation uses.
	/// </summary>
	virtual int Index() const = 0;

	/// <summary>
	/// The box around the pixels on the supporting image. For atlases, this
	/// will be the location of the pixels relative to the origin. For any
	/// standalone images, it will be the entire thing.
	/// </summary>
	virtual Geometry::Box2I Area() const = 0;

	/// <summary>
	/// The UV coordinates of the image. This is based off of the
	/// <see cref="Area"/>'s location in the image. The numbers will be
	/// specific the renderer that implements it.
	/// </summary>
	virtual Geometry::Box2F UV() const = 0;

	/// <summary>
	/// The dimension of the image. This is a shortcut for the area.
	/// </summary>
	virtual Geometry::Dimension Dimension() const = 0;

	/// <summary>
	/// The offset that was used in the image this was built from.
	/// </summary>
	virtual Geometry::Vec2I Offset() const = 0;
};

}
