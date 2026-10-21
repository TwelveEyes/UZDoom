/*
** GLRenderbuffer.h
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
** Helion's class is an unattached GL renderbuffer of depth/stencil storage that nothing in the
** renderer uses. The render program API has no standalone depth storage and no attach calls, so
** this is a depth/stencil buffer in the only form the backend offers it: an IRenderTarget with its
** own depth/stencil buffer and a single R8 colour attachment (a render target needs one). Bind()
** makes it the render target of the render state, Unbind() returns to the scene target.
**
*/

#pragma once

#include <memory>
#include "zstring.h"
#include "hwrenderer/data/hw_renderprogram.h"
#include "Util/Geometry.h"

namespace Helion::Render::OpenGL::Framebuffer
{

class GLRenderbuffer
{
public:
	const FString Label;
	const Geometry::Dimension Dimension;

	GLRenderbuffer(const FString& label, Geometry::Dimension dimension);
	~GLRenderbuffer();

	GLRenderbuffer(const GLRenderbuffer&) = delete;
	GLRenderbuffer& operator=(const GLRenderbuffer&) = delete;

	IRenderTarget* Target() const { return m_target.get(); }

	void Bind();
	static void Unbind();

private:
	std::unique_ptr<IRenderTarget> m_target;
};

}
