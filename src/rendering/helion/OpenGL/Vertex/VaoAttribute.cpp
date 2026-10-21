/*
** VaoAttribute.cpp
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

#include "VaoAttribute.h"

namespace Helion::Render::OpenGL::Vertex
{

VaoAttribute::VaoAttribute(const FString& name, int index, int size, VertexAttribPointerType type, int offset, bool normalized, int stride, bool required, int divisor)
	: Name(name), Index(index), Size(size), PointerType(type), Offset(offset), Normalized(normalized), Required(required), Stride(stride), Divisor(divisor)
{
}

VaoAttribute::VaoAttribute(const FString& name, int index, int size, VertexAttribIntegerType type, int offset, int stride, bool required)
	: Name(name), Index(index), Size(size), IntegerType(type), Offset(offset), Required(required), Stride(stride)
{
}

FString VaoAttribute::ToString() const
{
	int type = PointerType ? (int)*PointerType : IntegerType ? (int)*IntegerType : -1;
	FString str;
	str.Format("[%d] %s (size: %d, offset: %d, type: %d%s, normalized: %d, stride: %d)", Index, Name.GetChars(), Size, Offset, type, PointerType ? "" : " (integer)", (int)Normalized, Stride);
	return str;
}

}
