/*
** ViewLights.h
**
** UZDoom-only (plan M6.1): the clustered dynamic lights of one view (main view, portal view, camera texture view).
**
**   Build(level, view, enabled)   at the start of the view, before its first program is bound (the compute path leaves
**                                 the render state in table shader mode): assigns the world lights (walls, flats,
**                                 3D floors, flood fills, decals) to the clusters
**   Bind(program)                 RenderProgramBase::Bind: the ClusterParams block and the cluster buffers
**
** Models and HUD models are not lit through the clusters (ModelLightList).
**
** The renderer keeps one ViewLights per portal recursion depth, so the views of an outer pass keep their buffers while a
** portal view is drawn in the middle of it, and makes the one of the view being drawn Current().
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

#include "OpenGL/Renderers/Legacy/World/Lights/ClusterLightBuffer.h"

struct FLevelLocals;

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Lights
{

class ViewLights
{
public:
	// enabled = false (fullbright scene, classic isFullbrightScene) gives the view no dynamic lights.
	void Build(FLevelLocals* level, const ClusterLightBuffer::ViewParams& view, bool enabled);
	void Bind(::Helion::Render::OpenGL::Shader::RenderProgram& program) const;

	static ViewLights* Current() { return s_current; }
	// Never built: binds "no lights" (programs bound outside of a view, ClusterLightSet::None).
	static ViewLights& Empty() { static ViewLights empty; return empty; }
	static void SetCurrent(ViewLights* lights) { s_current = lights; }

private:
	ClusterLightBuffer m_world;

	inline static ViewLights* s_current = nullptr;
};

}
