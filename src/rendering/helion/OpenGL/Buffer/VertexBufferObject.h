/*
** VertexBufferObject.h
**
** Port of Helion.Render.OpenGL.Buffer.Array.Vertex.VertexBufferObject<T> and
** its Dynamic/Static/Stream flavours.
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** DrawArrays goes through the render state (FRenderState::Draw / DrawInstanced);
** the vertex buffer has to be bound with Bind() (FRenderState::SetVertexBuffer) and
** a render program set before drawing, which VertexPipeline does. Every draw first
** sends the uniforms / texture units that changed since the last draw to the render
** state (RenderProgram::FlushCurrent), the replacement of glUniform* taking effect
** immediately.
**
*/

#pragma once

#include <cassert>
#include "ArrayBufferObject.h"
#include "OpenGL/Util/PrimitiveType.h"
#include "OpenGL/Shader/RenderProgram.h"
#include "v_video.h"

namespace Helion::Render::OpenGL::Buffer::Array::Vertex
{

template<typename T>
class VertexBufferObject : public ArrayBufferObject<T>
{
public:
	IVertexBuffer* Vertex() const { return m_vertex; }

	// Makes this the vertex stream (binding point 0) of subsequent draws.
	void Bind()
	{
		screen->RenderState()->SetVertexBuffer(Vertex(), 0, 0);
	}

	// Makes this the per-instance stream (binding point 1) of subsequent instanced draws.
	void BindInstance()
	{
		screen->RenderState()->SetInstanceBuffer(Vertex(), 0);
	}

	void DrawArrays(Util::PrimitiveType type = Util::PrimitiveType::Triangles)
	{
		if (this->Count() == 0)
			return;

		assert(this->Uploaded && "Forgot to upload VBO data");
		Shader::RenderProgram::FlushCurrent();
		screen->RenderState()->Draw(Util::ToDrawType(type), 0, this->Count());
	}

	void DrawArrays(Util::PrimitiveType type, int first, int count)
	{
		if (this->Count() == 0)
			return;

		assert(this->Uploaded && "Forgot to upload VBO data");
		Shader::RenderProgram::FlushCurrent();
		screen->RenderState()->Draw(Util::ToDrawType(type), first, count);
	}

	void DrawArraysInstanced(Util::PrimitiveType type, int first, int count)
	{
		if (this->Count() == 0)
			return;

		assert(this->Uploaded && "Forgot to upload VBO data");
		Shader::RenderProgram::FlushCurrent();
		screen->RenderState()->DrawInstanced(Util::ToDrawType(type), first, count, this->Count());
	}

protected:
	explicit VertexBufferObject(const FString& label, int capacity = BufferObject<T>::DefaultCapacity) : ArrayBufferObject<T>(label, capacity)
	{
		m_vertex = screen->CreateVertexBuffer();
		this->m_buffer.reset(m_vertex);
	}

private:
	IVertexBuffer* m_vertex = nullptr;	// owned by m_buffer (IBuffer is a virtual base of IVertexBuffer)
};

template<typename T>
class DynamicVertexBuffer : public VertexBufferObject<T>
{
public:
	explicit DynamicVertexBuffer(const FString& label, int capacity = BufferObject<T>::DefaultCapacity) : VertexBufferObject<T>(label, capacity) { }

protected:
	BufferUsageHint Hint() const override { return BufferUsageHint::DynamicDraw; }
};

template<typename T>
class StaticVertexBuffer : public VertexBufferObject<T>
{
public:
	explicit StaticVertexBuffer(const FString& label, int capacity = BufferObject<T>::DefaultCapacity) : VertexBufferObject<T>(label, capacity) { }

protected:
	BufferUsageHint Hint() const override { return BufferUsageHint::StaticDraw; }
};

template<typename T>
class StreamVertexBuffer : public VertexBufferObject<T>
{
public:
	explicit StreamVertexBuffer(const FString& label) : VertexBufferObject<T>(label) { }

protected:
	BufferUsageHint Hint() const override { return BufferUsageHint::StreamDraw; }
};

}
