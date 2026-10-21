/*
** GLMappedBuffer.h
**
** Port of Helion.Render.OpenGL.Util.GLMappedBuffer<T>.
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** Helion maps the GL buffer (glMapBufferRange) and writes through the pointer. The
** backend IBuffer::Lock invalidates the buffer contents, so a texel buffer keeps a CPU
** shadow copy instead (GLBufferTexture): Pointer/MappedMemoryPtr point into the shadow
** and Dispose() uploads the pending range with IBuffer::SetSubData.
**
*/

#pragma once

namespace Helion::Render::OpenGL::Util
{

template<typename T>
struct GLMappedBuffer
{
	// The mapped memory (Helion: Pointer / MappedMemoryPtr).
	T* MappedMemoryPtr = nullptr;
	// Number of elements of T that can be accessed.
	int Length = 0;

	GLMappedBuffer() = default;
	GLMappedBuffer(T* memory, int length, void (*unmap)(void*), void* owner)
		: MappedMemoryPtr(memory), Length(length), m_unmap(unmap), m_owner(owner)
	{
	}

	T* Pointer() const { return MappedMemoryPtr; }

	// GL.UnmapBuffer: uploads the written range to the backend buffer. Safe to call more than once.
	void Dispose()
	{
		if (m_unmap != nullptr)
		{
			m_unmap(m_owner);
			m_unmap = nullptr;
		}
	}

private:
	void (*m_unmap)(void*) = nullptr;
	void* m_owner = nullptr;
};

}
