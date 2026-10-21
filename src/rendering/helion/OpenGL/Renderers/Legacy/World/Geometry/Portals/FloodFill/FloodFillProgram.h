/*
** FloodFillProgram.h
**
** Port of Helion.Render.OpenGL.Renderers.Legacy.World.Geometry.Portals.FloodFill.FloodFillProgram and
** FloodFillWallClipProgram.
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** Create with RenderProgram::Create<T>(...) (HelionProgramRegistry: ProgramType::FloodFill / ProgramType::FloodFillWallClip).
**
*/

#pragma once

#include "OpenGL/Renderers/Legacy/World/RenderProgramBase.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Geometry::Portals::FloodFill
{

class FloodFillProgram : public RenderProgramBase
{
public:
	explicit FloodFillProgram(const FString& name);

	void CameraDirection(const ::Helion::Geometry::Vec3F& dir);
	void Camera(const ::Helion::Geometry::Vec3F& camera);

protected:
	FString VertexShader() override;
	std::optional<FString> FragmentShader() override;
	const TArray<Vertex::VaoAttribute>* VertexLayout() const override;
	void ProgramCreated() override;

private:
	int m_cameraLocation = -1;
	int m_cameraDirection = -1;
};

class FloodFillWallClipProgram : public FloodFillProgram
{
public:
	FloodFillWallClipProgram() : FloodFillProgram("WallClip")
	{
	}
};

}
