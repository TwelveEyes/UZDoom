/*
** VertexArrayObject.h
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
** UZDoom's backend has no VAO object: the vertex layout lives in the IVertexBuffer
** (SetFormat) and the render state binds buffers per draw. This class keeps the
** pieces a VAO groups in GL (vertex buffer + layout) and Bind() makes them the
** current vertex stream of the render state.
**
*/

#pragma once

#include "zstring.h"
#include "hwrenderer/data/buffers.h"

namespace Helion::Render::OpenGL::Vertex
{

enum class VertexArrayType
{
	Legacy,
	Modern
};

class VertexArrayObject
{
public:
	const FString Label;

	VertexArrayObject(const FString& label, VertexArrayType type);

	// Associates the buffer; Attributes::Apply fills in the layout of the buffer.
	void SetVertexBuffer(IVertexBuffer* vbo, bool instanced);

	// True when the attributes of the buffer are fetched per instance (Divisor > 0), i.e. the
	// buffer is the second vertex stream (binding point 1) of the render state.
	bool Instanced() const { return m_instanced; }

	void Bind();
	void Unbind();

private:
	VertexArrayType m_type;
	IVertexBuffer* m_vbo = nullptr;
	bool m_instanced = false;
};

}
