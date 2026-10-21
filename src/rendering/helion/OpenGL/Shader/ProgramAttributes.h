/*
** ProgramAttributes.h
**
** Port of Helion.Render.OpenGL.Shader.ProgramAttributes.
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** Helion populates the list by querying the linked GL program (ActiveAttributes).
** The render program API has no introspection, so the list is populated from the
** vertex inputs the program declares in its GLSL (RenderProgram parses them).
**
*/

#pragma once

#include "tarray.h"
#include "zstring.h"
#include "hwrenderer/data/hw_renderprogram.h"

namespace Helion::Render::OpenGL::Shader
{

struct ProgramAttribute
{
	FString Name;
	int Index;	// attribute location
	int Size;
	UniformType Type;
};

class ProgramAttributes
{
public:
	int Count() const { return (int)m_attributes.Size(); }
	const ProgramAttribute& operator[](int index) const { return m_attributes[index]; }

	void Populate(const TArray<RenderProgramIO>& inputsOutputs);

	const ProgramAttribute* begin() const { return m_attributes.Data(); }
	const ProgramAttribute* end() const { return m_attributes.Data() + m_attributes.Size(); }

private:
	TArray<ProgramAttribute> m_attributes;
};

}
