/*
** SkySphereGeometryShader.h
**
** Port of Helion.Render.OpenGL.Renderers.Legacy.World.Sky.Sphere.SkySphereGeometryShader.
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

#include "OpenGL/Shader/RenderProgram.h"
#include "Util/Geometry.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Sky::Sphere
{

// Create with RenderProgram::Create<T>(). Draws the geometry the sky is visible through (the stencil mask).
class SkySphereGeometryShader : public OpenGL::Shader::RenderProgram
{
public:
	SkySphereGeometryShader() : RenderProgram("Sky sphere geometry") { }

	void Mvp(const ::Helion::Geometry::mat4& mat) { OpenGL::Shader::ProgramUniforms::Set(mat, m_mvpLocation); }
	void TimeFrac(float value) { OpenGL::Shader::ProgramUniforms::Set(value, m_timeFracLocation); }

protected:
	FString VertexShader() override;
	std::optional<FString> FragmentShader() override;
	const TArray<Vertex::VaoAttribute>* VertexLayout() const override;
	void ProgramCreated() override;

private:
	int m_mvpLocation = -1;
	int m_timeFracLocation = -1;
};

}
