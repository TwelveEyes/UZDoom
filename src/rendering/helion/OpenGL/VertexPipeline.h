/*
** VertexPipeline.h
**
** Port of Helion.Render.OpenGL.VertexPipeline.
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

#include <array>
#include <span>
#include <vector>
#include "OpenGL/Buffer/VertexBufferObject.h"
#include "OpenGL/Shader/RenderProgram.h"
#include "OpenGL/Util/PrimitiveType.h"
#include "OpenGL/Vertex/Attributes.h"
#include "OpenGL/Vertex/VertexArrayObject.h"

namespace Helion::Render::OpenGL
{

// Wraps one vertex buffer with the attribute table of its vertex struct (TVertex::VertexAttributes()).
// The VAO of Helion is the layout stored in the IVertexBuffer plus the bind of the buffer as the vertex stream
// of the render state. Index buffers are not used by the world pipelines; indexed draws bind their
// IIndexBuffer on the render state themselves.
template<typename TVertex>
class VertexPipeline
{
public:
	Vertex::VertexArrayObject Vao;
	Buffer::Array::Vertex::VertexBufferObject<TVertex>& Vbo;

	VertexPipeline(Shader::RenderProgram& program, Buffer::Array::Vertex::VertexBufferObject<TVertex>& vbo, const FString& vaoLabel)
		: VertexPipeline(std::span<Shader::RenderProgram* const>(std::array<Shader::RenderProgram*, 1>{ &program }), vbo, vaoLabel)
	{
	}

	VertexPipeline(std::span<Shader::RenderProgram* const> programs, Buffer::Array::Vertex::VertexBufferObject<TVertex>& vbo, const FString& vaoLabel)
		: Vao(vaoLabel, Vertex::VertexArrayType::Legacy), Vbo(vbo)
	{
		Vao.Bind();
		for (Shader::RenderProgram* program : programs)
			Vertex::Attributes::BindAndApply(Vbo, Vao, program->Attributes);
	}

	virtual ~VertexPipeline() = default;

	void Bind(bool bindVbo = false)
	{
		Vao.Bind();
		if (bindVbo)
			Vbo.Bind();
	}

	void Unbind()
	{
		Vao.Unbind();
	}

	virtual void DrawArrays(Util::PrimitiveType primitiveType = Util::PrimitiveType::Triangles)
	{
		Vbo.DrawArrays(primitiveType);
	}

	// Helion's version of this overload ignores primitiveType and always draws GL_LINES (defect); the port
	// draws with the requested primitive type.
	virtual void DrawArrays(Util::PrimitiveType primitiveType, int first, int count)
	{
		Vbo.DrawArrays(primitiveType, first, count);
	}

	virtual void Clear() { Vbo.Clear(); }

	virtual bool Empty() const { return Vbo.Empty(); }
};

}
