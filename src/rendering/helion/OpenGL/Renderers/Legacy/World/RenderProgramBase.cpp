/*
** RenderProgramBase.cpp
**
** Port of Helion.Render.OpenGL.Renderers.Legacy.World.RenderProgramBase.
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

#include "RenderProgramBase.h"
#include "OpenGL/Renderers/Legacy/World/Lights/ClusterFragPrelude.h"
#include "OpenGL/Renderers/Legacy/World/Lights/ViewLights.h"
#include "hwrenderer/data/hw_renderstate.h"
#include "hwrenderer/data/hw_viewpointuniforms.h"
#include "v_video.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World
{

// Helion reads the locations in the constructor (the base constructor has already linked the program). Here the
// program is described after construction (RenderProgram::Create), so the locations are read in ProgramCreated.
void RenderProgramBase::ProgramCreated()
{
	m_boundTextureLocation = Uniforms.GetLocation("boundTexture");
	m_sectorLightTextureLocation = Uniforms.GetLocation("sectorLightTexture");
	m_colormapTextureLocation = Uniforms.GetLocation("colormapTexture");
	m_sectorColormapTextureLocation = Uniforms.GetLocation("sectorColormapTexture");
	m_sectorFogTextureLocation = Uniforms.GetLocation("sectorFogTexture");
	m_sectorGlowTextureLocation = Uniforms.GetLocation("sectorGlowTexture");
	m_brightmapTextureLocation = Uniforms.GetLocation("brightmapTexture");
	m_mvpLocation = Uniforms.GetLocation("mvp");
	m_timeFracLocation = Uniforms.GetLocation("timeFrac");
	m_hasInvulnerabilityLocation = Uniforms.GetLocation("hasInvulnerability");
	m_mvpNoPitchLocation = Uniforms.GetLocation("mvpNoPitch");
	m_lightLevelMixLocation = Uniforms.GetLocation("lightLevelMix");
	m_extraLightLocation = Uniforms.GetLocation("extraLight");
	m_distanceOffsetLocation = Uniforms.GetLocation("distanceOffset");
	m_colorMixLocation = Uniforms.GetLocation("colorMix");
	m_paletteIndexLocation = Uniforms.GetLocation("paletteIndex");
	m_colorMapIndexLocation = Uniforms.GetLocation("colormapIndex");
	m_lightModeLocation = Uniforms.GetLocation("lightMode");
	m_gammaCorrectionLocation = Uniforms.GetLocation("gammaCorrection");
	m_vertexGapClampUV = Uniforms.GetLocation("vertexGapClampUV");
	m_useBrightmapsLocation = Uniforms.GetLocation("useBrightmaps");
	m_accumTextureLocation = Uniforms.GetLocation("accum");
	m_accumCountTextureLocation = Uniforms.GetLocation("accumCount");
	m_planeClipTextureLocation = Uniforms.GetLocation("planeClipTexture");
	m_checkPlaneClipLocation = Uniforms.GetLocation("checkPlaneClip");
	m_wallClipTextureLocation = Uniforms.GetLocation("wallClipTexture");
	m_downScaleAmountLocation = Uniforms.GetLocation("downScaleAmount");
	m_screenBoundsLocation = Uniforms.GetLocation("screenBounds");
	m_fogBarrierLocation = Uniforms.GetLocation("fogBarrier");
	m_useSectorColorLocation = Uniforms.GetLocation("useSectorColor");
	m_useSectorFogLocation = Uniforms.GetLocation("useSectorFog");

	m_textureModeLocation = Uniforms.GetLocation("uTextureMode");
	m_lightAttrLocation = Uniforms.GetLocation("uLightAttr");
	m_alphaThresholdLocation = Uniforms.GetLocation("uAlphaThreshold");
	m_fogEnabledLocation = Uniforms.GetLocation("uFogEnabled");
	m_clusterLightsOffLocation = Uniforms.GetLocation(Lights::ClusterFragPrelude::DisableUniform);
	m_hasClusterLights = m_clusterLightsOffLocation >= 0;
	m_materialFlipYLocation = Uniforms.GetLocation("materialFlipY");
	m_texLocation = Uniforms.GetLocation("tex");
	m_shadowMapLocation = Uniforms.GetLocation("ShadowMap");
	m_lightMapLocation = Uniforms.GetLocation("LightMap");
	for (int i = 0; i < 11; i++)
	{
		FString name;
		name.Format("texture%d", i + 2);
		m_materialTextureLocations[i] = Uniforms.GetLocation(name);
	}
}

void RenderProgramBase::MaterialTexture(int n, TextureUnit unit)
{
	assert(n >= 2 && n <= 12);
	::Helion::Render::OpenGL::Shader::ProgramUniforms::Set(unit, m_materialTextureLocations[n - 2]);
}

void RenderProgramBase::ViewpointUniforms(const HWViewpointUniforms& viewpoint)
{
	Uniforms.SetBlock("ViewpointUBO", &viewpoint, sizeof(HWViewpointUniforms));
}

void RenderProgramBase::MaterialStreamData(const StreamData& data)
{
	Uniforms.SetBlock("StreamData", &data, sizeof(StreamData));
}

void RenderProgramBase::OnBound()
{
	if (m_hasClusterLights)
	{
		Lights::ClusterLightSet set = LightSet();
		Lights::ViewLights* lights = Lights::ViewLights::Current();
		if (lights == nullptr || set == Lights::ClusterLightSet::None)
			lights = &Lights::ViewLights::Empty();
		lights->Bind(*this);
		ClusterLightsOff(set == Lights::ClusterLightSet::None);
	}

	int shadowMap = GetBinding("ShadowMap");
	if (shadowMap >= 0)
		screen->RenderState()->SetProgramShadowMapTexture(shadowMap);
}

}
