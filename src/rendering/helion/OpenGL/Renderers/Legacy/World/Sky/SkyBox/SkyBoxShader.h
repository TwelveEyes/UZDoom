/*
** SkyBoxShader.h
**
** UZDoom-only: draws the faces of a sky box (FSkyBox, GLDEFS skybox).
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
#include "OpenGL/Util/TextureUnit.h"
#include "Util/Geometry.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Sky::SkyBox
{

// Create with RenderProgram::Create<T>(). Vertex format: SkySphereVertex.
class SkyBoxShader : public OpenGL::Shader::RenderProgram
{
public:
	SkyBoxShader() : RenderProgram("Sky box") { }

	void BoundTexture(TextureUnit unit) { OpenGL::Shader::ProgramUniforms::Set(unit, m_boundTextureLocation); }
	void ColormapTexture(TextureUnit unit) { OpenGL::Shader::ProgramUniforms::Set(unit, m_colormapTextureLocation); }
	void HasInvulnerability(bool invul) { OpenGL::Shader::ProgramUniforms::Set(invul, m_hasInvulnerabilityLocation); }
	void Mvp(const ::Helion::Geometry::mat4& mat) { OpenGL::Shader::ProgramUniforms::Set(mat, m_mvpLocation); }
	void PaletteIndex(int index) { OpenGL::Shader::ProgramUniforms::Set(index, m_paletteIndexLocation); }
	void ColorMapIndex(int index) { OpenGL::Shader::ProgramUniforms::Set(index, m_colorMapIndexLocation); }
	void ColorMix(const ::Helion::Geometry::Vec3F& value) { OpenGL::Shader::ProgramUniforms::Set(value, m_colorMixLocation); }
	void GammaCorrection(float value) { OpenGL::Shader::ProgramUniforms::Set(value, m_gammaCorrectionLocation); }

protected:
	FString VertexShader() override;
	std::optional<FString> FragmentShader() override;
	const TArray<Vertex::VaoAttribute>* VertexLayout() const override;
	void ProgramCreated() override;

private:
	int m_boundTextureLocation = -1;
	int m_colormapTextureLocation = -1;
	int m_mvpLocation = -1;
	int m_hasInvulnerabilityLocation = -1;
	int m_paletteIndexLocation = -1;
	int m_colorMapIndexLocation = -1;
	int m_colorMixLocation = -1;
	int m_gammaCorrectionLocation = -1;
};

}
