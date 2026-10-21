/*
** GLFramebuffer.cpp
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
*/

#include <cassert>
#include <stdexcept>
#include "GLFramebuffer.h"
#include "OpenGL/Shader/TextureUnits.h"
#include "hwrenderer/data/hw_renderstate.h"
#include "v_video.h"

namespace Helion::Render::OpenGL::Framebuffer
{

void FramebufferTexture::Bind(TextureUnit unit) const
{
	using Shader::TextureUnits;

	if (m_target == nullptr)
	{
		if (!m_depth)
			screen->RenderState()->CopySceneColor();
		TextureUnits::BindScene(unit, m_depth);
		return;
	}

	if (m_depth)
		throw std::logic_error("The own depth buffer of a render target cannot be bound as a texture");

	TextureUnits::BindRenderTarget(unit, m_target, m_attachment);
}

GLFramebuffer::GLFramebuffer(const FString& label, Geometry::Dimension dimension, int numColorAttachments, GLFrameBufferOptions options, bool mainBackBuffer)
	: Label(label), Dimension(dimension), IsMainBackBuffer(mainBackBuffer)
{
	assert(numColorAttachments >= 0 && "Cannot have a negative amount of color attachments");
	assert(dimension.Width > 0 && dimension.Height > 0 && "Must have a positive dimension for framebuffer");
	assert((numColorAttachments > 0 || options != GLFrameBufferOptions::None) && "Cannot have no color attachments and no depth/stencil renderbuffer");

	bool depth = (options == GLFrameBufferOptions::DepthStencilAttachment);

	if (mainBackBuffer)
	{
		m_numColor = 1;
		m_hasDepth = depth;
		ColorAttachment0 = FramebufferTexture(dimension, nullptr, 0, false);
		if (depth)
			DepthTexture = FramebufferTexture(dimension, nullptr, 0, true);
		return;
	}

	// A render target needs a colour attachment; a depth only framebuffer gets one that is never read.
	m_numColor = numColorAttachments > 0 ? numColorAttachments : 1;
	m_hasDepth = depth;

	RenderTargetDesc desc;
	desc.name.Format("Framebuffer: %s", label.GetChars());
	desc.width = dimension.Width;
	desc.height = dimension.Height;
	for (int i = 0; i < m_numColor; i++)
		desc.colorFormats.Push(RTF_RGBA8);
	desc.shareSceneDepth = false;
	desc.ownDepth = depth;

	m_target.reset(screen->CreateRenderTarget(desc));
	if (m_target == nullptr)
		throw std::runtime_error(std::string("Failed to create framebuffer ") + label.GetChars());

	ColorAttachment0 = FramebufferTexture(dimension, m_target.get(), 0, false);
	if (depth)
		DepthTexture = FramebufferTexture(dimension, m_target.get(), 0, true);
}

GLFramebuffer::~GLFramebuffer() = default;

void GLFramebuffer::Bind()
{
	screen->RenderState()->SetRenderTarget(m_target.get());
}

void GLFramebuffer::Unbind()
{
	screen->RenderState()->SetRenderTarget(nullptr);
}

}
