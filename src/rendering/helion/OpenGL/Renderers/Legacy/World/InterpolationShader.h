/*
** InterpolationShader.h
**
** Port of Helion.Render.OpenGL.Renderers.Legacy.World.InterpolationShader.
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

#include "RenderProgramBase.h"
#include "Shader/FragFunction.h"
#include "Shader/UzdoomContract.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World
{

// Create with RenderProgram::Create<T>(...). See StaticShader for the subclass checks.
class InterpolationShader : public RenderProgramBase
{
public:
	// UZDoom-only: the material shader the program draws with (default: the Helion array texture program).
	InterpolationShader(const FString& name, const Shader::MaterialProgramOptions& material = Shader::MaterialProgramOptions());

protected:
	FString VertexShader() override;
	std::optional<FString> FragmentShader() override;
	const TArray<Vertex::VaoAttribute>* VertexLayout() const override;

private:
	Shader::MaterialProgramOptions m_material;

	FString VertexShaderInternal(bool planeClip);
	Shader::OitOptions GetOitOptions() const;
	FString GetOutTargets(bool planeClip) const;
};

}
