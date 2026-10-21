/*
** GLFramebuffer.h
**
** Port of Helion.Render.OpenGL.Framebuffer.GLFramebuffer.
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** There are no GL framebuffer objects here: a framebuffer is either the scene target of the backend
** (the main back buffer, IRenderTarget == nullptr, which is what the world is drawn into) or an
** IRenderTarget created with DFrameBuffer::CreateRenderTarget.
**
** Helion's attachments are GLTexture2D objects that the callers Bind() after glActiveTexture. The
** attachments are FramebufferTexture here: Bind(unit) records what the texture unit of
** Shader/TextureUnits.h holds. For the scene colour attachment that is a copy of the scene colour
** taken by FRenderState::CopySceneColor() at the time of the Bind (the scene colour cannot be
** sampled while it is drawn into); the depth attachment of the scene can be bound as SceneDepth.
** The own depth buffer of an IRenderTarget cannot be bound as a texture by the render program API.
**
*/

#pragma once

#include <memory>
#include "zstring.h"
#include "hwrenderer/data/hw_renderprogram.h"
#include "OpenGL/Util/TextureUnit.h"
#include "Util/Geometry.h"

namespace Helion::Render::OpenGL::Framebuffer
{

enum class GLFrameBufferOptions
{
	None,
	DepthStencilAttachment
};

// An attachment of a GLFramebuffer; what GLTexture2D is for Helion's callers.
class FramebufferTexture
{
public:
	Geometry::Dimension Dimension;

	FramebufferTexture() = default;
	FramebufferTexture(Geometry::Dimension dimension, IRenderTarget* target, int attachment, bool depth)
		: Dimension(dimension), m_target(target), m_attachment(attachment), m_depth(depth) { }

	// GLTexture2D.Bind() after GL.ActiveTexture(unit).
	// Scene colour: takes the CopySceneColor() copy first, so the render target in use has to be the one
	// that is drawn to afterwards (the copy is of the scene colour buffer regardless).
	void Bind(TextureUnit unit) const;

	// True for the attachments of the scene target.
	bool IsScene() const { return m_target == nullptr; }
	bool IsDepth() const { return m_depth; }
	// The render target the attachment belongs to (nullptr = scene).
	IRenderTarget* Target() const { return m_target; }
	int Attachment() const { return m_attachment; }

private:
	IRenderTarget* m_target = nullptr;
	int m_attachment = 0;
	bool m_depth = false;
};

class GLFramebuffer
{
public:
	const FString Label;
	const Geometry::Dimension Dimension;

	FramebufferTexture ColorAttachment0;
	FramebufferTexture DepthTexture;	// valid when HasDepthTexture()
	bool IsMainBackBuffer;

	// mainBackBuffer: the scene target; numColorAttachments is ignored and the attachments are those of the scene.
	// Otherwise numColorAttachments RGBA8 attachments (and with DepthStencilAttachment an own depth/stencil buffer)
	// of `dimension` are created.
	GLFramebuffer(const FString& label, Geometry::Dimension dimension, int numColorAttachments, GLFrameBufferOptions options = GLFrameBufferOptions::None, bool mainBackBuffer = false);
	~GLFramebuffer();

	GLFramebuffer(const GLFramebuffer&) = delete;
	GLFramebuffer& operator=(const GLFramebuffer&) = delete;

	bool HasDepthTexture() const { return m_hasDepth; }
	int NumColorAttachments() const { return m_numColor; }
	IRenderTarget* Target() const { return m_target.get(); }

	// GL.BindFramebuffer(Framebuffer, name): draws go to this framebuffer.
	void Bind();
	// GL.BindFramebuffer(Framebuffer, 0): back to the scene target.
	static void Unbind();
	// The render state has one framebuffer binding: BindDraw is Bind(); BindRead does nothing (a read
	// framebuffer is not a concept of the render state, blits are done by FramebufferRenderer).
	void BindRead() const { }
	void BindDraw() { Bind(); }

private:
	std::unique_ptr<IRenderTarget> m_target;
	int m_numColor = 0;
	bool m_hasDepth = false;
};

}
