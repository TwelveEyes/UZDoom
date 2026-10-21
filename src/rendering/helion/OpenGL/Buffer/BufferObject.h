/*
** BufferObject.h
**
** Port of Helion.Render.OpenGL.Buffer.BufferObject<T>.
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** Represents a buffer object on the GPU. The GL buffer id is replaced by an
** IBuffer created through the backend layer (DFrameBuffer::CreateVertexBuffer),
** Bind/Unbind do not exist because the render state binds buffers at draw time.
**
*/

#pragma once

#include <cstring>
#include <memory>
#include <span>
#include <type_traits>
#include "tarray.h"
#include "zstring.h"
#include "hwrenderer/data/buffers.h"

namespace Helion::Render::OpenGL::Buffer
{

// OpenTK BufferUsageHint.
enum class BufferUsageHint
{
	StreamDraw,
	StaticDraw,
	DynamicDraw,
};

/// <summary>
/// Represents a buffer object on the GPU.
/// </summary>
/// <typeparam name="T">The type to hold, which must be a struct that is packed.</typeparam>
template<typename T>
class BufferObject
{
	static_assert(std::is_trivially_copyable_v<T>, "BufferObject elements are copied to the GPU verbatim");

public:
	static constexpr int DefaultCapacity = 128;
	static constexpr int BytesPerElement = (int)sizeof(T);

	const FString Label;
	TArray<T> Data;

	int Count() const { return (int)Data.Size(); }
	bool Empty() const { return Count() == 0; }
	bool NeedsUpload() const { return !Uploaded && Count() > 0; }
	int TotalBytes() const { return Count() * BytesPerElement; }

	void SetNotUploaded() { Uploaded = false; }

	virtual ~BufferObject() = default;

	BufferObject(const BufferObject&) = delete;
	BufferObject& operator=(const BufferObject&) = delete;

	IBuffer* Backend() const { return m_buffer.get(); }

	void Add(const T& element)
	{
		Data.Push(element);
		Uploaded = false;
	}

	void AddMemoryCopy(std::span<const T> elements)
	{
		if (elements.empty())
			return;

		unsigned index = Data.Reserve(elements.size());
		memcpy(&Data[index], elements.data(), elements.size() * sizeof(T));
		Uploaded = false;
	}

	void Add(std::span<const T> elements)
	{
		AddMemoryCopy(elements);
	}

	void Add(const T* elements, int length)
	{
		AddMemoryCopy(std::span<const T>(elements, (size_t)length));
	}

	void Upload()
	{
		if (Uploaded)
			return;

		PerformUpload();
		Uploaded = true;
	}

	// Will attempt to use BufferSubData first using the underlying buffer's Length.
	// If the underlying buffer has not been uploaded or was reallocated then it will be flagged to upload.
	void UploadSubDataOrSetNotUploaded(int index, int length)
	{
		if (!BufferSubData(index, length))
		{
			Uploaded = false;
		}
	}

	void UploadSubData(int start, int length)
	{
		BufferSubData(start, length);
	}

	void UploadIfNeeded()
	{
		if (NeedsUpload())
			Upload();
	}

	void UploadCapacity()
	{
		if (NeedsUpload())
		{
			PerformUploadCapacity();
			Uploaded = true;
		}
	}

	void Clear()
	{
		Data.Clear();
		Uploaded = false;
	}

protected:
	bool Uploaded = false;
	std::unique_ptr<IBuffer> m_buffer;

	explicit BufferObject(const FString& label, int capacity = DefaultCapacity) : Label(label)
	{
		Data.Grow((unsigned)capacity);
	}

	virtual BufferUsageType UsageType() const = 0;

	void PerformUpload()
	{
		m_initialized = true;
		m_uploadedSize = (size_t)BytesPerElement * Data.Size();
		m_ptr = Data.Data();
		m_buffer->SetData(m_uploadedSize, Data.Data(), UsageType());
	}

	void PerformUploadCapacity()
	{
		m_initialized = true;
		m_uploadedSize = (size_t)BytesPerElement * Data.Max();
		m_ptr = Data.Data();
		m_buffer->SetData(m_uploadedSize, Data.Data(), UsageType());
	}

	bool BufferSubData(int index, int length)
	{
		if (!Uploaded || !m_initialized)
		{
			Uploaded = false;
			return false;
		}

		T* ptr = Data.Data();
		// If the underlying array was resized then the new array needs to be uploaded.
		// This should be handled with BufferObject.UploadIfNeeded
		if (ptr != m_ptr)
		{
			m_ptr = ptr;
			Uploaded = false;
			return false;
		}

		size_t offset = (size_t)BytesPerElement * (size_t)index;
		size_t size = (size_t)BytesPerElement * (size_t)length;

		assert(m_uploadedSize >= offset + size && "Offset and size are out of bounds for the GPU");
		// This shouldn't happen and should be handled upstream but never upload out of range for the GPU.
		if (m_uploadedSize < offset + size)
			return false;

		m_buffer->SetSubData(offset, size, reinterpret_cast<const uint8_t*>(ptr) + offset);
		return true;
	}

private:
	T* m_ptr = nullptr;
	bool m_initialized = false;
	size_t m_uploadedSize = 0;
};

}
