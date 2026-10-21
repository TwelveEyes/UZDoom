/*
** GLBufferTexture.h
**
** Port of Helion.Render.OpenGL.Textures.GLBufferTexture<T>.
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** A samplerBuffer / usamplerBuffer backing store. GL.GenBuffer + GL.GenTexture + glTexBuffer are
** replaced by the backend texel buffer (DFrameBuffer::CreateTexelBuffer); it is bound to a
** TextureUnit slot (Shader/TextureUnits.h) instead of GL.BindTexture. The BindBuffer, UnbindBuffer
** and BindTexBuffer state calls have no equivalent and are gone. The data array stays as a CPU
** shadow of the buffer (see GLMappedBuffer.h); PersistentBufferStorage is kept for the callers
** but the backend picks the buffer storage itself.
**
** SizedInternalFormat.R8ui is a one byte per texel format in OpenGL. Backend texel buffers have
** no byte format, R8ui therefore maps to a 32 bit unsigned texel and callers must instantiate
** the template with uint32_t (plan M3.3: one uint per sector floor/ceiling/wall slot).
**
*/

#pragma once

#include <functional>
#include <memory>
#include <type_traits>
#include <algorithm>
#include "tarray.h"
#include "engineerrors.h"
#include "zstring.h"
#include "v_video.h"
#include "hwrenderer/data/buffers.h"
#include "hwrenderer/data/hw_renderprogram.h"
#include "OpenGL/Util/GLMappedBuffer.h"
#include "OpenGL/Util/TextureUnit.h"
#include "OpenGL/Shader/TextureUnits.h"

namespace Helion::Render::OpenGL::Textures
{

// OpenTK SizedInternalFormat, the subset used by texel buffers (all are available as backend texel formats).
enum class SizedInternalFormat
{
	R32f,
	Rg32f,
	Rgba32f,
	R32ui,
	Rg32ui,
	Rgba32ui,
	R32i,
	Rgba8,
	Rgba8ui,
	R8ui,	// stored as R32ui, see the file comment
};

constexpr int ToTexelFormat(SizedInternalFormat format)
{
	switch (format)
	{
	case SizedInternalFormat::R32f: return RPFmt_Float;
	case SizedInternalFormat::Rg32f: return RPFmt_Float2;
	case SizedInternalFormat::Rgba32f: return RPFmt_Float4;
	case SizedInternalFormat::R32ui: return RPFmt_UInt;
	case SizedInternalFormat::Rg32ui: return RPFmt_UInt2;
	case SizedInternalFormat::Rgba32ui: return RPFmt_UInt4;
	case SizedInternalFormat::R32i: return RPFmt_Int;
	case SizedInternalFormat::Rgba8: return RPFmt_Byte4;
	case SizedInternalFormat::Rgba8ui: return RPFmt_Byte4_UInt;
	case SizedInternalFormat::R8ui: return RPFmt_UInt;
	}
	return RPFmt_Float4;
}

// See: https://gist.github.com/roxlu/5090067
template<typename T>
class GLBufferTexture
{
	static_assert(std::is_trivially_copyable_v<T>, "GLBufferTexture elements are copied to the GPU verbatim");

public:
	const FString Label;
	const bool PersistentBufferStorage;

	GLBufferTexture(const FString& label, TArray<T>&& data, SizedInternalFormat format, bool persistentBufferStorage)
		: Label(label), PersistentBufferStorage(persistentBufferStorage), m_data(std::move(data)), m_format(format)
	{
		m_buffer.reset(screen->CreateTexelBuffer(ToTexelFormat(format)));
		if (!m_buffer)
			I_FatalError("GLBufferTexture %s: the backend has no texel buffers", label.GetChars());

		m_buffer->SetData(DataBytes(), m_data.Data(), BufferUsageType::Stream);
	}

	GLBufferTexture(const GLBufferTexture&) = delete;
	GLBufferTexture& operator=(const GLBufferTexture&) = delete;
	~GLBufferTexture() { Dispose(); }

	int Length() const { return (int)m_data.Size(); }
	SizedInternalFormat Format() const { return m_format; }
	IDataBuffer* Backend() const { return m_buffer.get(); }

	// Helion maps the buffer, hands the pointer to the action and unmaps it.
	void Map(const std::function<void(T*)>& action)
	{
		action(m_data.Data());
		Upload(0, Length());
	}

	// You must call Dispose on the result, or else the written data never reaches the buffer.
	Util::GLMappedBuffer<T> MapWithDisposable()
	{
		m_dirtyStart = 0;
		m_dirtyEnd = Length();
		return Util::GLMappedBuffer<T>(m_data.Data(), Length(), &GLBufferTexture::FlushThunk, this);
	}

	// UZDoom-only: the mapped memory is a shadow copy, writers that touch a small part of a large
	// buffer narrow the range the next unmap uploads (elements of T, not texels).
	void SetDirtyRange(int firstElement, int count)
	{
		m_dirtyStart = firstElement;
		m_dirtyEnd = firstElement + count;
	}

	// Binds the buffer to the texture unit the program's samplerBuffer reads (glBindTexture + glTexBuffer).
	void BindTexture(OpenGL::TextureUnit unit)
	{
		OpenGL::Shader::TextureUnits::BindTexelBuffer(unit, m_buffer.get());
	}

	static void UnbindTexture(OpenGL::TextureUnit unit)
	{
		OpenGL::Shader::TextureUnits::Unbind(unit);
	}

	// UZDoom-only: grows or shrinks the buffer keeping the existing elements and uploads everything.
	void Resize(int newLength)
	{
		m_data.Resize((unsigned)newLength);
		m_buffer->SetData(DataBytes(), m_data.Data(), BufferUsageType::Stream);
	}

	void Dispose()
	{
		m_buffer.reset();
	}

private:
	TArray<T> m_data;
	std::unique_ptr<IDataBuffer> m_buffer;
	SizedInternalFormat m_format;
	int m_dirtyStart = 0;
	int m_dirtyEnd = 0;

	size_t DataBytes() const { return (size_t)m_data.Size() * sizeof(T); }

	void Upload(int firstElement, int count)
	{
		if (!m_buffer || count <= 0)
			return;

		m_buffer->SetSubData((size_t)firstElement * sizeof(T), (size_t)count * sizeof(T), m_data.Data() + firstElement);
	}

	static void FlushThunk(void* owner)
	{
		auto self = static_cast<GLBufferTexture*>(owner);
		int start = self->m_dirtyStart;
		int end = std::min(self->m_dirtyEnd, self->Length());
		self->m_dirtyStart = 0;
		self->m_dirtyEnd = 0;
		if (end > start)
			self->Upload(start, end - start);
	}
};

}
