/*
** VertexAttributes.cpp
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
*/

#include "VertexAttributes.h"

// VertexAttributeAttribute is a plain value type; everything lives in the header.
static_assert(Helion::Render::OpenGL::Vertex::VertexAttributeAttribute::InferFromField == -1);
