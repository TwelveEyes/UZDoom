/*
** WorldRenderer.h
**
** Port of Helion.Render.OpenGL.Renderers.WorldRenderer.
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** IWorld is FLevelLocals*; the weak reference to the last rendered world is a plain pointer (the renderer
** is told about a new level through UpdateToNewWorld and drops it there). GLFramebuffer is the backend
** scene target facade of OpenGL/Framebuffer/GLFramebuffer.h; it is only forward declared here (nullptr
** while that class is not part of the build, see LegacyWorldRendererParts.h).
*/

#pragma once

#include "Common/Shared/RenderInfo.h"
#include "OpenGL/Texture/Legacy/GLTextureArrayBuilder.h"

struct FLevelLocals;

namespace Helion::Render::OpenGL::Framebuffer
{
class GLFramebuffer;
}

namespace Helion::Render::OpenGL::Renderers
{

/// <summary>
/// Responsible for rendering a world.
/// </summary>
class WorldRenderer
{
public:
	Texture::Legacy::TextureBuckets SpriteTextureBuckets;

	virtual ~WorldRenderer() = default;

	/// <summary>
	/// Performs rendering on the world provided with the information for
	/// rendering.
	/// </summary>
	/// <param name="world">The world to render.</param>
	/// <param name="renderInfo">The rendering metadata.</param>
	void Render(FLevelLocals* world, Shared::RenderInfo& renderInfo, Framebuffer::GLFramebuffer* framebuffer)
	{
		PerformRender(world, renderInfo, framebuffer);
	}

	virtual void ResetInterpolation(FLevelLocals* world) = 0;

	virtual void Dispose() = 0;

	/// <summary>
	/// Requests that the child implementations update to the world being
	/// provided.
	/// </summary>
	/// <param name="world">The world to update to.</param>
	virtual void UpdateToNewWorld(FLevelLocals* world) = 0;

protected:
	FLevelLocals* m_lastRenderedWorld = nullptr;

	/// <summary>
	/// Performs the actual rendering commands.
	/// </summary>
	/// <param name="world">The world.</param>
	/// <param name="renderInfo">The rendering metadata.</param>
	virtual void PerformRender(FLevelLocals* world, Shared::RenderInfo& renderInfo, Framebuffer::GLFramebuffer* framebuffer) = 0;
};

}
