/*
** PrimitiveVbo.h
**
** Port of Helion.Render.OpenGL.Renderers.Legacy.World.Primitives.PrimitiveVbo.
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

#include "OpenGL/Buffer/VertexBufferObject.h"
#include "OpenGL/VertexPipeline.h"
#include "PrimitiveShader.h"
#include "PrimitiveVertex.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Primitives
{

// The pipeline references its vertex buffer, so the buffer is a member that is declared first.
class PrimitiveVbo
{
public:
	PrimitiveVbo(PrimitiveShader& program, const FString& name, int lineWidth)
		: m_vbo("Primitive"), Pipeline(program, m_vbo, "Primitive"), LineWidth(lineWidth)
	{
	}

	PrimitiveVbo(const PrimitiveVbo&) = delete;
	PrimitiveVbo& operator=(const PrimitiveVbo&) = delete;

private:
	Buffer::Array::Vertex::StreamVertexBuffer<PrimitiveVertex> m_vbo;

public:
	VertexPipeline<PrimitiveVertex> Pipeline;
	const int LineWidth;
};

}
