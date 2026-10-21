/*
** ProgramAttributes.cpp
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
*/

#include "ProgramAttributes.h"

namespace Helion::Render::OpenGL::Shader
{

void ProgramAttributes::Populate(const TArray<RenderProgramIO>& inputsOutputs)
{
	for (const RenderProgramIO& io : inputsOutputs)
	{
		if (io.position != RenderProgramIO::VInput)
			continue;

		ProgramAttribute attr{ io.name, io.location, 1, io.type };
		m_attributes.Push(attr);
	}
}

}
