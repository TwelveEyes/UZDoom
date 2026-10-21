/*
** SkySphereShader.h
**
** Port of Helion.Render.OpenGL.Renderers.Legacy.World.Sky.Sphere.SkySphereShader.
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

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Sky::Sphere
{

// Create with RenderProgram::Create<T>(...).
class SkySphereShader : public OpenGL::Shader::RenderProgram
{
public:
	explicit SkySphereShader(const FString& name = "Sky sphere") : RenderProgram(name) { }

	void BoundTexture(TextureUnit unit) { OpenGL::Shader::ProgramUniforms::Set(unit, m_boundTextureLocation); }
	void ColormapTexture(TextureUnit unit) { OpenGL::Shader::ProgramUniforms::Set(unit, m_colormapTextureLocation); }
	void HasInvulnerability(bool invul) { OpenGL::Shader::ProgramUniforms::Set(invul, m_hasInvulnerabilityLocation); }
	void Mvp(const ::Helion::Geometry::mat4& mat) { OpenGL::Shader::ProgramUniforms::Set(mat, m_mvpLocation); }
	void FlipU(bool flip) { OpenGL::Shader::ProgramUniforms::Set(flip, m_flipULocation); }
	void PaletteIndex(int index) { OpenGL::Shader::ProgramUniforms::Set(index, m_paletteIndexLocation); }
	void ColorMapIndex(int index) { OpenGL::Shader::ProgramUniforms::Set(index, m_colorMapIndexLocation); }
	// FSkyVertexBuffer::SetupMatrices of the layer, see SkyDome.
	void DomeTexture(const FVector4& value) { OpenGL::Shader::ProgramUniforms::Set(value, m_domeTextureLocation); }
	void DomeModel(const FVector3& value) { OpenGL::Shader::ProgramUniforms::Set(value, m_domeModelLocation); }
	void TopColor(const FVector4& topColor) { OpenGL::Shader::ProgramUniforms::Set(topColor, m_topColorLocation); }
	void BottomColor(const FVector4& bottomColor) { OpenGL::Shader::ProgramUniforms::Set(bottomColor, m_bottomColorLocation); }
	void ColorMix(const ::Helion::Geometry::Vec3F& value) { OpenGL::Shader::ProgramUniforms::Set(value, m_colorMixLocation); }
	void GammaCorrection(float value) { OpenGL::Shader::ProgramUniforms::Set(value, m_gammaCorrectionLocation); }

	static FString FetchTopBottomColors();
	// UZDoom: GLSL of the classic sky dome mapping (domeCoord), shared by the sky and foreground programs.
	static FString DomeFunctions();

protected:
	FString VertexShader() override;
	std::optional<FString> FragmentShader() override;
	const TArray<Vertex::VaoAttribute>* VertexLayout() const override;
	void ProgramCreated() override;

	// The vertex shader shared by the sphere programs.
	static FString SphereVertexShader();

private:
	int m_boundTextureLocation = -1;
	int m_colormapTextureLocation = -1;
	int m_mvpLocation = -1;
	int m_hasInvulnerabilityLocation = -1;
	int m_flipULocation = -1;
	int m_paletteIndexLocation = -1;
	int m_colorMapIndexLocation = -1;
	int m_domeTextureLocation = -1;
	int m_domeModelLocation = -1;
	int m_topColorLocation = -1;
	int m_bottomColorLocation = -1;
	int m_colorMixLocation = -1;
	int m_gammaCorrectionLocation = -1;
};

}
