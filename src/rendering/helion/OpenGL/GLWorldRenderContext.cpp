/*
** GLWorldRenderContext.cpp
**
** Port of Helion.Render.OpenGL.GLWorldRenderContext.
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

#include "OpenGL/GLWorldRenderContext.h"
#include "g_levellocals.h"

namespace Helion::Render::OpenGL
{

void GLWorldRenderContext::Draw(FLevelLocals* world)
{
	if (m_context == nullptr)
		return;

	Common::World::Camera* camera = m_context->Camera;
	m_oldCamera.Set(camera->PositionInterpolated, camera->Position, camera->YawRadians(), camera->PitchRadians());

	// Note: We never draw the automap for this, that should be handled
	// elsewhere.
	m_commands.DrawWorld(world, &m_oldCamera, world->maptime, m_context->InterpolationFrac,
		m_cameraPlayer, m_context->DrawAutomap, m_context->AutomapOffset,
		m_context->AutomapScale);
}

}
