/*
** ISkyComponent.h
**
** Port of Helion.Render.OpenGL.Renderers.Legacy.World.Sky.ISkyComponent.
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

#include "Common/Shared/RenderInfo.h"
#include "OpenGL/Buffer/VertexBufferObject.h"
#include "Sphere/SkyGeometryVertex.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Sky
{

/// <summary>
/// A sky that can be rendered. This might be a skybox, or a sphere to
/// emulate the vanilla sky, or a sky portal, etc.
/// </summary>
class ISkyComponent
{
public:
	virtual ~ISkyComponent() = default;

	/// <summary>
	/// True if there is geometry to render with; otherwise if not then we
	/// shouldn't bother doing anything else since nothing can be drawn.
	/// </summary>
	virtual bool HasGeometry() const = 0;

	/// <summary>
	/// Clears all the geometry in the world out so new geometry can be
	/// loaded.
	/// </summary>
	virtual void Clear() = 0;

	/// <summary>
	/// Adds the triangle in the world where a sky should be drawn. This
	/// should be counter-clockwise.
	/// </summary>
	/// <param name="vertices">Sky vertices.</param>
	/// <param name="length">The number of vertices to copy.</param>
	virtual void Add(const Sphere::SkyGeometryVertex* vertices, int length) = 0;

	/// <summary>
	/// Renders the world geometry. It is assumed the stencil buffer can be
	/// written to and will be with this invocation.
	/// </summary>
	/// <param name="renderInfo">The position for rendering.</param>
	virtual void RenderWorldGeometry(Shared::RenderInfo& renderInfo) = 0;

	/// <summary>
	/// To be called after <see cref="RenderWorldGeometry"/>, such that it
	/// will draw the sky component wherever the stencil buffer was set to
	/// be written to.
	/// </summary>
	/// <param name="renderInfo">The position for rendering.</param>
	virtual void RenderSky(Shared::RenderInfo& renderInfo) = 0;

	// C# property Vbo. The buffer the sky geometry is added to (SkyGeometryManager keeps indices into it).
	virtual Buffer::Array::Vertex::VertexBufferObject<Sphere::SkyGeometryVertex>& Vbo() = 0;
};

}
