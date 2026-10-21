/*
** VaoAttribute.h
**
** Port of Helion.Render.OpenGL.Vertex.VaoAttribute.
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

#include <optional>
#include "zstring.h"

namespace Helion::Render::OpenGL::Vertex
{

// OpenTK's VertexAttribPointerType / VertexAttribIntegerType, restricted to the types vertex structs use.
enum class VertexAttribPointerType
{
	Byte,
	UnsignedByte,
	Short,
	UnsignedShort,
	Int,
	UnsignedInt,
	Float,
};

enum class VertexAttribIntegerType
{
	Byte,
	UnsignedByte,
	Short,
	UnsignedShort,
	Int,
	UnsignedInt,
};

class VaoAttribute
{
public:
	FString Name;
	int Index;
	int Size;
	std::optional<VertexAttribPointerType> PointerType;
	std::optional<VertexAttribIntegerType> IntegerType;
	int Offset;
	bool Normalized = false;
	bool Required;
	int Stride;
	int Divisor = 0;

	VaoAttribute(const FString& name, int index, int size, VertexAttribPointerType type, int offset, bool normalized, int stride, bool required, int divisor);
	VaoAttribute(const FString& name, int index, int size, VertexAttribIntegerType type, int offset, int stride, bool required);

	FString ToString() const;
};

}
