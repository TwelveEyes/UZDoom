/*
** PlaneClipFrameBuffer.h
**
** Port of Helion.Render.OpenGL.Framebuffer.PlaneClipFrameBuffer.
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** Writes clip info for vanilla sprite clipping emulation: RGBA32F for planes, RGBA16F for walls.
**
** Deviation from Helion: Helion can attach the plane texture next to the colour attachment of the
** world framebuffer (multiple render targets, `colorFramebuffer != null`, ColorAttachment0 =
** fragColor, ColorAttachment1 = outPlane, depth of that framebuffer). A render target of the
** render program API cannot take the scene colour buffer as an attachment, so the buffer is always
** a stand alone target with the plane texture as its only attachment (ColorAttachment0 = outPlane)
** and an own depth/stencil buffer: Helion's `colorFramebuffer == null` mode, which it uses itself
** when the vanilla buffer is downscaled. `colorFramebuffer` is accepted for call compatibility,
** IsMrt() is always false, and callers have to draw the plane clip pass with the non MRT
** programs (InterpolationPlaneClipAlphaShader / StaticPlaneClipAlphaShader) whatever they pass.
**
*/

#pragma once

#include <memory>
#include "zstring.h"
#include "hwrenderer/data/hw_renderprogram.h"
#include "OpenGL/Util/TextureUnit.h"
#include "Util/Geometry.h"
#include "GLFramebuffer.h"

namespace Helion::Render::OpenGL::Framebuffer
{

enum class PlaneClipType
{
	Plane,
	Wall
};

class PlaneClipFrameBuffer
{
public:
	PlaneClipFrameBuffer() = default;
	~PlaneClipFrameBuffer();

	PlaneClipFrameBuffer(const PlaneClipFrameBuffer&) = delete;
	PlaneClipFrameBuffer& operator=(const PlaneClipFrameBuffer&) = delete;

	// Creates or updates the target.
	// name: the debug label; dimension: size of the plane texture and its depth buffer;
	// colorFramebuffer: see the file comment; forceCreate: recreate even when the dimension is unchanged.
	void CreateOrUpdate(const FString& name, PlaneClipType type, Geometry::Dimension dimension, const GLFramebuffer* colorFramebuffer, bool forceCreate);

	// Always false, see the file comment.
	bool IsMrt() const { return false; }

	// Clears the plane texture to (-255, 1e30, -255, 1e30) and the depth buffer to 1. The buffer has to be bound.
	void Clear();

	// One/Zero blending (overwrite, the clip data must not be blended).
	static void StartRender();

	void BindPlaneTexture(TextureUnit textureUnit);
	void BindFrameBuffer();
	// Back to the scene target.
	static void UnbindFrameBuffer();

private:
	std::unique_ptr<IRenderTarget> m_target;
	Geometry::Dimension m_dimension;

	bool ShouldRecreate(Geometry::Dimension dimension, bool force) const;
	void DeleteData();
};

}
