/*
** ViewLights.cpp
**
** UZDoom-only (plan M6.1): see ViewLights.h.
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

#include "OpenGL/Renderers/Legacy/World/Lights/ViewLights.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Lights
{

void ViewLights::Build(FLevelLocals* level, const ClusterLightBuffer::ViewParams& view, bool enabled)
{
	if (!enabled)
	{
		m_world.Clear();
		return;
	}

	m_world.Build(level, view);
}

void ViewLights::Bind(::Helion::Render::OpenGL::Shader::RenderProgram& program) const
{
	m_world.Bind(program);
}

}
