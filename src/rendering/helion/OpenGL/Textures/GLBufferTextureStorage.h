/*
** GLBufferTextureStorage.h
**
** Port of Helion.Render.OpenGL.Textures.GLBufferTextureStorage<T>.
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** The persistent mapping paths of Helion (glBufferStorage + MapPersistentBit) do not exist
** behind the backend buffer interface: GetMappedBufferAndBind always returns the CPU shadow of
** the GLBufferTexture and Unbind uploads what the caller wrote. AssertFormat is unnecessary:
** SizedInternalFormat has no three component formats.
**
*/

#pragma once

#include "GLBufferTexture.h"

namespace Helion::Render::OpenGL::Textures
{

template<typename T>
class GLBufferTextureStorage
{
public:
	static constexpr int FourComponentLength = 4;

	GLBufferTextureStorage(const FString& label, TArray<T>&& data, SizedInternalFormat format, bool persistentBufferStorage)
		: m_bufferTexture(label, std::move(data), format, persistentBufferStorage)
	{
		m_dataLength = m_bufferTexture.Length();
	}

	int DataLength() const { return m_dataLength; }

	Util::GLMappedBuffer<T> GetMappedBufferAndBind()
	{
		m_mappedBuffer = m_bufferTexture.MapWithDisposable();
		m_mapped = true;
		return m_mappedBuffer;
	}

	void BindTexture(OpenGL::TextureUnit unit)
	{
		m_bufferTexture.BindTexture(unit);
	}

	// Uploads the data written through GetMappedBufferAndBind.
	void Unbind()
	{
		if (m_mapped)
			m_mappedBuffer.Dispose();
		m_mapped = false;
	}

	void Map(const std::function<void(T*)>& action)
	{
		m_bufferTexture.Map(action);
	}

	// UZDoom-only, see GLBufferTexture::SetDirtyRange.
	void SetDirtyRange(int firstElement, int count)
	{
		m_bufferTexture.SetDirtyRange(firstElement, count);
	}

	// UZDoom-only, see GLBufferTexture::Resize.
	void Resize(int newLength)
	{
		m_bufferTexture.Resize(newLength);
		m_dataLength = newLength;
	}

	void Dispose()
	{
		m_bufferTexture.Dispose();
	}

private:
	GLBufferTexture<T> m_bufferTexture;
	Util::GLMappedBuffer<T> m_mappedBuffer;
	bool m_mapped = false;
	int m_dataLength = 0;
};

}
