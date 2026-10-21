/*
** VertexAttributes.h
**
** Port of Helion.Render.OpenGL.Vertex.VertexAttributeAttribute.
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** Helion reads [VertexAttribute] annotations from vertex struct fields by
** reflection. C++ has none, so every vertex struct lists its annotated fields
** in a static VertexAttributes() table, in field order, next to the struct
** definition (see StaticVertex.h). An entry describes one annotated field
** and the unannotated fields following it that it covers through Size.
**
*/

#pragma once

#include "VaoAttribute.h"

namespace Helion::Render::OpenGL::Vertex
{

struct VertexAttributeAttribute
{
	static constexpr int InferFromField = -1;

	const char* Name = nullptr;
	int Index = InferFromField;
	int Size = 1;
	VertexAttribPointerType Type = VertexAttribPointerType::Float;
	bool IsIntegral = false;
	bool Normalized = false;
	bool Required = true;
	int Divisor = -1;

	constexpr bool InferIndex() const { return Index == InferFromField; }
};

}
