/*
** OitFrameBuffer.h
**
** Port of Helion.Render.OpenGL.Framebuffer.OitFrameBuffer.
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** The weighted blended OIT target: RGBA16F accumulation (attachment 0), RG16F accumulation count
** (attachment 1) and R8 fuzz (attachment 2), sharing the scene depth/stencil buffer of the opaque
** pass (RenderTargetDesc::shareSceneDepth). The target is created at scene size and the backend
** recreates it when the scene size changes; the dimension passed to CreateOrUpdate (the viewport of
** the view, smaller for camera textures) does not recreate it.
**
** Per attachment blending (One/One, add) is state of the render state: StartRender() and
** SetBlendEquations() set it with FRenderState::SetCustomBlendAttachment. It stays active until a
** program is unbound (FRenderState::SetProgram(nullptr)) or another SetCustomBlend / DisableBlend
** replaces it.
**
*/

#pragma once

#include <memory>
#include "hwrenderer/data/hw_renderprogram.h"
#include "OpenGL/Util/TextureUnit.h"
#include "Util/Geometry.h"
#include "GLFramebuffer.h"

namespace Helion::Render::OpenGL::Framebuffer
{

class OitFrameBuffer
{
public:
	// opaqueDepthTexture: the depth attachment of the opaque framebuffer. The render program API can only share the
	// scene depth, so it has to be the depth of the scene target (GLFramebuffer::IsMainBackBuffer).
	void CreateOrUpdate(Geometry::Dimension dimension, const FramebufferTexture& opaqueDepthTexture);

	// Binds the OIT target, clears the three attachments to 0 and sets the accumulation blending.
	void StartRender();

	// One/One, add, on all three attachments.
	static void SetBlendEquations();

	// GL.BindFramebuffer(m_oitFramebuffer).
	void BindFrameBuffer();

	// accum / accumCount / fuzz units hold the three attachments, opaqueTexture the colour of `framebuffer`
	// (for the scene target a copy of the scene colour, see FramebufferTexture::Bind).
	void BindTextures(TextureUnit accumTexture, TextureUnit accumCountTexture, TextureUnit fuzzTexture, TextureUnit opaqueTexture, const GLFramebuffer& framebuffer);

private:
	std::unique_ptr<IRenderTarget> m_target;
	Geometry::Dimension m_oitDimension;
};

}
