/*
** ArrayBufferObject.h
**
** Port of Helion.Render.OpenGL.Buffer.Array.ArrayBufferObject<T>.
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** The upload logic (PerformUpload / PerformUploadCapacity / BufferSubData) lives in
** BufferObject<T> because the backend IBuffer replaces the GL target; this class
** adds the usage hint.
**
*/

#pragma once

#include "BufferObject.h"

namespace Helion::Render::OpenGL::Buffer::Array
{

template<typename T>
class ArrayBufferObject : public BufferObject<T>
{
protected:
	virtual BufferUsageHint Hint() const = 0;

	explicit ArrayBufferObject(const FString& objectLabel, int capacity = BufferObject<T>::DefaultCapacity) : BufferObject<T>(objectLabel, capacity)
	{
	}

	BufferUsageType UsageType() const override
	{
		// IBuffer::SetData: Static = initial data uploaded once, Stream = initial data uploaded, rewritten often.
		return Hint() == BufferUsageHint::StaticDraw ? BufferUsageType::Static : BufferUsageType::Stream;
	}
};

}
