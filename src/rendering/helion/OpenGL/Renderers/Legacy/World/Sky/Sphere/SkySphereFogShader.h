/*
** SkySphereFogShader.h
**
** UZDoom-only: the sky fog layer (UZDoom HWSkyPortal::DrawContents fog / skymist dome).
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** The sky fog (Level->skyfog, r_distance_cull_type > 1) tints the sky with the fade colour of the sector. UZDoom draws
** either a solid dome of that colour or, with LEVEL3_SKYMIST, the mist texture (skymista) tinted with it. Both are
** alpha blended over the sky, inside the sky stencil. The mist texture is only used in truecolor mode.
**
*/

#pragma once

#include "OpenGL/Shader/RenderProgram.h"
#include "OpenGL/Util/TextureUnit.h"
#include "Util/Geometry.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Sky::Sphere
{

// Create with RenderProgram::Create<T>().
class SkySphereFogShader : public OpenGL::Shader::RenderProgram
{
public:
	SkySphereFogShader() : RenderProgram("Sky fog") { }

	void BoundTexture(TextureUnit unit) { OpenGL::Shader::ProgramUniforms::Set(unit, m_boundTextureLocation); }
	void Mvp(const ::Helion::Geometry::mat4& mat) { OpenGL::Shader::ProgramUniforms::Set(mat, m_mvpLocation); }
	void FlipU(bool flip) { OpenGL::Shader::ProgramUniforms::Set(flip, m_flipULocation); }
	// rgb = fade colour, a = fog amount.
	void FogColor(const FVector4& color) { OpenGL::Shader::ProgramUniforms::Set(color, m_fogColorLocation); }
	void UseTexture(bool use) { OpenGL::Shader::ProgramUniforms::Set(use, m_useTextureLocation); }
	// x = u scale, y = v scale, z = v offset in texture heights, w = u scroll (fraction of a turn).
	void MistParams(const FVector4& params) { OpenGL::Shader::ProgramUniforms::Set(params, m_mistParamsLocation); }
	void ColorMix(const ::Helion::Geometry::Vec3F& value) { OpenGL::Shader::ProgramUniforms::Set(value, m_colorMixLocation); }
	void GammaCorrection(float value) { OpenGL::Shader::ProgramUniforms::Set(value, m_gammaCorrectionLocation); }

protected:
	FString VertexShader() override;
	std::optional<FString> FragmentShader() override;
	const TArray<Vertex::VaoAttribute>* VertexLayout() const override;
	void ProgramCreated() override;

private:
	int m_boundTextureLocation = -1;
	int m_mvpLocation = -1;
	int m_flipULocation = -1;
	int m_fogColorLocation = -1;
	int m_useTextureLocation = -1;
	int m_mistParamsLocation = -1;
	int m_colorMixLocation = -1;
	int m_gammaCorrectionLocation = -1;
};

}
