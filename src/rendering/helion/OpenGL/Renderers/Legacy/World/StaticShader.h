/*
** StaticShader.h
**
** Port of Helion.Render.OpenGL.Renderers.Legacy.World.StaticShader.
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

// Create with RenderProgram::Create<T>(...). The subclasses of Helion (StaticWallClipShader, StaticTransparentShader, ...)
// only differ by their name; this class picks the sources with `this is` checks like Helion does, which become dynamic_casts.
class StaticShader : public RenderProgramBase
{
public:
	// UZDoom-only: the material shader the program draws with (default: the Helion array texture program).
	StaticShader(const FString& name, const Shader::MaterialProgramOptions& material = Shader::MaterialProgramOptions());

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
