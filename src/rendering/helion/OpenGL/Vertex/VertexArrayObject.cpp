/*
** VertexArrayObject.cpp
**
** Port of Helion.Render.OpenGL.Vertex.VertexArrayObject.
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

#include "VertexArrayObject.h"
#include "v_video.h"
#include "hwrenderer/data/hw_renderstate.h"

namespace Helion::Render::OpenGL::Vertex
{

VertexArrayObject::VertexArrayObject(const FString& label, VertexArrayType type) : Label(label), m_type(type)
{
}

void VertexArrayObject::SetVertexBuffer(IVertexBuffer* vbo, bool instanced)
{
	m_vbo = vbo;
	m_instanced = instanced;
}

void VertexArrayObject::Bind()
{
	if (m_vbo == nullptr)
		return;

	FRenderState* state = screen->RenderState();
	if (m_instanced)
		state->SetInstanceBuffer(m_vbo, 0);
	else
		state->SetVertexBuffer(m_vbo, 0, 0);
}

void VertexArrayObject::Unbind()
{
	// Nothing is unbound: the next Bind() of another VAO replaces the vertex stream, and
	// draws only happen while a program (which fixes the expected layout) is set.
}

}
