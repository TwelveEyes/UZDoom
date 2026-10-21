/*
** RenderProgramBase.h
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

#pragma once

#include "OpenGL/Shader/RenderProgram.h"
#include "OpenGL/Util/TextureUnit.h"
#include "Util/Geometry.h"
#include "Util/Configs/Components/ConfigRender.h"
#include "OpenGL/Renderers/Legacy/World/Lights/ClusterLightList.h"

struct StreamData;
struct HWViewpointUniforms;

namespace Helion::Render::OpenGL::Renderers::Legacy::World
{

class RenderProgramBase : public ::Helion::Render::OpenGL::Shader::RenderProgram
{
public:
	void BoundTexture(TextureUnit unit) { ::Helion::Render::OpenGL::Shader::ProgramUniforms::Set(unit, m_boundTextureLocation); }
	void SectorLightTexture(TextureUnit unit) { ::Helion::Render::OpenGL::Shader::ProgramUniforms::Set(unit, m_sectorLightTextureLocation); }
	void ColormapTexture(TextureUnit unit) { ::Helion::Render::OpenGL::Shader::ProgramUniforms::Set(unit, m_colormapTextureLocation); }
	void SectorColormapTexture(TextureUnit unit) { ::Helion::Render::OpenGL::Shader::ProgramUniforms::Set(unit, m_sectorColormapTextureLocation); }
	void SectorFogTexture(TextureUnit unit) { ::Helion::Render::OpenGL::Shader::ProgramUniforms::Set(unit, m_sectorFogTextureLocation); }
	// UZDoom-only: the sector glow texel buffer (SectorLightBuffers, gl_glow) of the truecolor programs.
	void SectorGlowTexture(TextureUnit unit) { ::Helion::Render::OpenGL::Shader::ProgramUniforms::Set(unit, m_sectorGlowTextureLocation); }
	void BrightmapTexture(TextureUnit unit) { ::Helion::Render::OpenGL::Shader::ProgramUniforms::Set(unit, m_brightmapTextureLocation); }
	void AccumTexture(TextureUnit unit) { ::Helion::Render::OpenGL::Shader::ProgramUniforms::Set(unit, m_accumTextureLocation); }
	void AccumCountTexture(TextureUnit unit) { ::Helion::Render::OpenGL::Shader::ProgramUniforms::Set(unit, m_accumCountTextureLocation); }
	void PlaneClipTexture(TextureUnit unit) { ::Helion::Render::OpenGL::Shader::ProgramUniforms::Set(unit, m_planeClipTextureLocation); }
	void WallClipTexture(TextureUnit unit) { ::Helion::Render::OpenGL::Shader::ProgramUniforms::Set(unit, m_wallClipTextureLocation); }

	void HasInvulnerability(bool invul) { ::Helion::Render::OpenGL::Shader::ProgramUniforms::Set(invul, m_hasInvulnerabilityLocation); }
	void Mvp(const ::Helion::Geometry::mat4& mvp) { ::Helion::Render::OpenGL::Shader::ProgramUniforms::Set(mvp, m_mvpLocation); }
	void MvpNoPitch(const ::Helion::Geometry::mat4& mvpNoPitch) { ::Helion::Render::OpenGL::Shader::ProgramUniforms::Set(mvpNoPitch, m_mvpNoPitchLocation); }
	void TimeFrac(float frac) { ::Helion::Render::OpenGL::Shader::ProgramUniforms::Set(frac, m_timeFracLocation); }
	void LightLevelMix(float lightLevelMix) { ::Helion::Render::OpenGL::Shader::ProgramUniforms::Set(lightLevelMix, m_lightLevelMixLocation); }
	void ExtraLight(int extraLight) { ::Helion::Render::OpenGL::Shader::ProgramUniforms::Set(extraLight, m_extraLightLocation); }
	void DistanceOffset(float distance) { ::Helion::Render::OpenGL::Shader::ProgramUniforms::Set(distance, m_distanceOffsetLocation); }
	void ColorMix(const ::Helion::Geometry::Vec3F& color) { ::Helion::Render::OpenGL::Shader::ProgramUniforms::Set(color, m_colorMixLocation); }
	void PaletteIndex(int index) { ::Helion::Render::OpenGL::Shader::ProgramUniforms::Set(index, m_paletteIndexLocation); }
	void ColorMapIndex(int index) { ::Helion::Render::OpenGL::Shader::ProgramUniforms::Set(index, m_colorMapIndexLocation); }
	void LightMode(::Helion::Util::Configs::Components::RenderLightMode mode) { ::Helion::Render::OpenGL::Shader::ProgramUniforms::Set((int)mode, m_lightModeLocation); }
	void GammaCorrection(float value) { ::Helion::Render::OpenGL::Shader::ProgramUniforms::Set(value, m_gammaCorrectionLocation); }
	void VertexGapClampUV(bool value) { ::Helion::Render::OpenGL::Shader::ProgramUniforms::Set(value, m_vertexGapClampUV); }
	void UseBrightmaps(bool value) { ::Helion::Render::OpenGL::Shader::ProgramUniforms::Set(value, m_useBrightmapsLocation); }
	void CheckPlaneClip(bool value) { ::Helion::Render::OpenGL::Shader::ProgramUniforms::Set(value, m_checkPlaneClipLocation); }
	void SetSpriteClipDownScaleAmount(float value) { ::Helion::Render::OpenGL::Shader::ProgramUniforms::Set(value, m_downScaleAmountLocation); }
	void ScreenBounds(const ::Helion::Geometry::Vec2I& value) { ::Helion::Render::OpenGL::Shader::ProgramUniforms::Set(value, m_screenBoundsLocation); }
	void FogBarrier(bool value) { ::Helion::Render::OpenGL::Shader::ProgramUniforms::Set(value, m_fogBarrierLocation); }
	void UseSectorColor(bool value) { ::Helion::Render::OpenGL::Shader::ProgramUniforms::Set(value, m_useSectorColorLocation); }
	void UseSectorFog(int value) { ::Helion::Render::OpenGL::Shader::ProgramUniforms::Set(value, m_useSectorFogLocation); }

	// UZDoom-only: the uniforms of the mod shader contract and of the gl_lightmode equations. The programs that have them
	// (per material programs, and truecolor programs with Shader::FragColorFunctionOptions::UzdoomLightMode) ignore the
	// calls on programs that do not (like glUniform on location -1).

	// The viewpoint block (HWViewpointUniforms): uCameraPos, uGlobVis, uPalLightLevels, uLightBlendMode, NormalViewMatrix, ...
	// The renderer fills it from the view setup that HWViewpointBuffer would normally hold.
	void ViewpointUniforms(const HWViewpointUniforms& viewpoint);
	// The per draw material state (StreamData): object / fog / add / texture blend colours, desaturation, glow and gradient planes, timer.
	void MaterialStreamData(const StreamData& data);
	void TextureMode(int textureMode) { ::Helion::Render::OpenGL::Shader::ProgramUniforms::Set(textureMode, m_textureModeLocation); }
	// uLightAttr: x = uLightDist, y = uLightFactor, z = uFogDensity, w = uLightLevel.
	void LightAttr(const FVector4& lightAttr) { ::Helion::Render::OpenGL::Shader::ProgramUniforms::Set(lightAttr, m_lightAttrLocation); }
	// The alpha test of the material programs; < 0 disables it (the table shaders' NO_ALPHATEST variants).
	void AlphaThreshold(float threshold) { ::Helion::Render::OpenGL::Shader::ProgramUniforms::Set(threshold, m_alphaThresholdLocation); }
	void FogEnabled(int fogEnabled) { ::Helion::Render::OpenGL::Shader::ProgramUniforms::Set(fogEnabled, m_fogEnabledLocation); }
	// Plan D25: true = the draws that follow get no clustered dynamic lights (the old FRenderState::SetLightIndex(-1) cases:
	// additive surfaces without lightadditivesurfaces, fullbright, RF2_NODYNAMICLIGHTING models, ...). Bind resets it to the
	// default of the program's light set.
	void ClusterLightsOff(bool off) { ::Helion::Render::OpenGL::Shader::ProgramUniforms::Set((int)off, m_clusterLightsOffLocation); }
	// Camera textures are stored upside down (FGameTexture negates their vertical scale): 1 negates the V coordinate.
	void MaterialFlipY(bool flip) { ::Helion::Render::OpenGL::Shader::ProgramUniforms::Set((int)flip, m_materialFlipYLocation); }

	// Texture units of the contract samplers: tex (the material texture), ShadowMap, LightMap, texture2 .. texture12.
	void Tex(TextureUnit unit) { ::Helion::Render::OpenGL::Shader::ProgramUniforms::Set(unit, m_texLocation); }
	void ShadowMap(TextureUnit unit) { ::Helion::Render::OpenGL::Shader::ProgramUniforms::Set(unit, m_shadowMapLocation); }
	void LightMap(TextureUnit unit) { ::Helion::Render::OpenGL::Shader::ProgramUniforms::Set(unit, m_lightMapLocation); }
	// n in 2..12
	void MaterialTexture(int n, TextureUnit unit);

protected:
	explicit RenderProgramBase(const FString& label) : RenderProgram(label) { }

	void ProgramCreated() override;
	// Plan D25: binds the clustered lights of the current view (Lights::ViewLights) and the shadow map.
	void OnBound() override;
	// The light list the program's surfaces are lit by. None: the program's dynamic lights are always off (sprites use the
	// SetDynLight probe of their instance instead).
	virtual Lights::ClusterLightSet LightSet() const { return Lights::ClusterLightSet::World; }

private:
	int m_boundTextureLocation = -1;
	int m_sectorLightTextureLocation = -1;
	int m_colormapTextureLocation = -1;
	int m_sectorColormapTextureLocation = -1;
	int m_sectorFogTextureLocation = -1;
	int m_brightmapTextureLocation = -1;
	int m_mvpLocation = -1;
	int m_timeFracLocation = -1;
	int m_hasInvulnerabilityLocation = -1;
	int m_mvpNoPitchLocation = -1;
	int m_lightLevelMixLocation = -1;
	int m_sectorGlowTextureLocation = -1;
	int m_extraLightLocation = -1;
	int m_distanceOffsetLocation = -1;
	int m_colorMixLocation = -1;
	int m_paletteIndexLocation = -1;
	int m_colorMapIndexLocation = -1;
	int m_lightModeLocation = -1;
	int m_gammaCorrectionLocation = -1;
	int m_vertexGapClampUV = -1;
	int m_useBrightmapsLocation = -1;
	int m_accumTextureLocation = -1;
	int m_accumCountTextureLocation = -1;
	int m_planeClipTextureLocation = -1;
	int m_checkPlaneClipLocation = -1;
	int m_wallClipTextureLocation = -1;
	int m_downScaleAmountLocation = -1;
	int m_screenBoundsLocation = -1;
	int m_fogBarrierLocation = -1;
	int m_useSectorColorLocation = -1;
	int m_useSectorFogLocation = -1;

	int m_textureModeLocation = -1;
	int m_lightAttrLocation = -1;
	int m_alphaThresholdLocation = -1;
	int m_fogEnabledLocation = -1;
	int m_clusterLightsOffLocation = -1;
	bool m_hasClusterLights = false;
	int m_materialFlipYLocation = -1;
	int m_texLocation = -1;
	int m_shadowMapLocation = -1;
	int m_lightMapLocation = -1;
	int m_materialTextureLocations[11];
};

}
