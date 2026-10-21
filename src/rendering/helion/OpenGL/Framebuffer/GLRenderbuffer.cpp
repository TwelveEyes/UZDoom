/*
** GLRenderbuffer.cpp
**
** Port of Helion.Render.OpenGL.Framebuffer.GLRenderbuffer.
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
#include "GLRenderbuffer.h"
#include "hwrenderer/data/hw_renderstate.h"
#include "v_video.h"

namespace Helion::Render::OpenGL::Framebuffer
{

GLRenderbuffer::GLRenderbuffer(const FString& label, Geometry::Dimension dimension) : Label(label), Dimension(dimension)
{
	assert(dimension.Width > 0 && dimension.Height > 0 && "Must have a positive dimension for renderbuffer");

	RenderTargetDesc desc;
	desc.name.Format("Renderbuffer: %s", label.GetChars());
	desc.width = dimension.Width;
	desc.height = dimension.Height;
	desc.colorFormats.Push(RTF_R8);
	desc.shareSceneDepth = false;
	desc.ownDepth = true;

	m_target.reset(screen->CreateRenderTarget(desc));
	if (m_target == nullptr)
		throw std::runtime_error(std::string("Failed to create renderbuffer ") + label.GetChars());
}

GLRenderbuffer::~GLRenderbuffer() = default;

void GLRenderbuffer::Bind()
{
	screen->RenderState()->SetRenderTarget(m_target.get());
}

void GLRenderbuffer::Unbind()
{
	screen->RenderState()->SetRenderTarget(nullptr);
}

}
