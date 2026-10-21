/*
** EntityProgram.h
**
** Port of Helion.Render.OpenGL.Renderers.Legacy.World.Entities.EntityProgram
** (+ EntityTransparentProgram, EntityCompositeProgram, EntityFuzzRefractionProgram).
** The health bar program of Helion is not ported (plan D20).
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

#include "OpenGL/Renderers/Legacy/World/RenderProgramBase.h"
#include "OpenGL/Renderers/Legacy/World/Shader/FragFunction.h"
#include "OpenGL/Renderers/Legacy/World/Shader/UzdoomContract.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Entities
{

// Create with RenderProgram::Create<T>(...) (or HelionProgramRegistry). One quad is generated per instance (EntityVertex,
// divisor 1) with gl_VertexID 0..3 (triangle strip), drawn with DrawArraysInstanced(TriangleStrip, 0, 4).
// The subclasses only differ by their name, like in Helion; the sources are picked with dynamic_cast.
class EntityProgram : public RenderProgramBase
{
public:
	// UZDoom-only: `material` selects a per material program (custom GLDEFS material / hardware shader, per texture
	// binding instead of the array texture), which is drawn by SortedTranslucentRenderer / the weapon / HUD paths.
	explicit EntityProgram(const FString& name, const Shader::MaterialProgramOptions& material = Shader::MaterialProgramOptions());

	void FuzzTexture(TextureUnit unit);
	void OpaqueTexture(TextureUnit unit);
	void MapDataTexture(TextureUnit unit);
	void LineHeightsTexture(TextureUnit unit);
	void FuzzFrac(float frac);
	void ViewRightNormal(const ::Helion::Geometry::Vec2F& viewRightNormal);
	void FuzzDiv(float div);
	void MaxDistanceSquared(float value);
	void FadeDistance(float value);
	void ViewPos(const ::Helion::Geometry::Vec3F& pos);
	void RenderFuzz(bool value);
	void RenderFuzzRefractionColor(bool value);
	void ColorClamp(float value);

	// UZDoom-only uniforms of the quad expansion and of the sprite shading.
	// View yaw / pitch / roll in degrees (FRenderViewpoint::HWAngles), for the billboard modes.
	void ViewAngles(const ::Helion::Geometry::Vec3F& yawPitchRoll);
	// Level->pixelstretch (EntityOptions::StretchPixels).
	void PixelStretch(float pixelStretch);
	// r_viewpoint.bDoOrtho: enables the RF2_ISOMETRICSPRITES tilt (EntityOptions::IsometricPitch) of Y-billboard sprites.
	void OrthoView(bool value);
	// 1 = sprites use the SetDynLight probe of the instance, 0 = light from the cluster / light buffer only (gl_light_sprites off).
	void UseLightProbe(bool value);
	// gl_enhanced_nightvision / gl_enhanced_nv_stealth: fixed colormap (nightvision) active, and the stealth fuzz branch.
	void EnhancedNightvision(bool value);
	// gl_mask_sprite_threshold (alpha test of non OIT passes); the OIT passes use the OIT alpha rules of Helion.
	void MaskThreshold(float value);
	// gl_cullcolor (rgb 0..1) used by the thick fog distance cull (r_distance_cull_type), alpha = 0 disables the colour fade.
	void CullColor(const FVector4& color);
	// Seconds of the shader timer (StreamData::timer) that animates the gl_fuzztype 1..8 fuzz patterns.
	void FuzzTimer(float seconds);

protected:
	FString VertexShader() override;
	std::optional<FString> FragmentShader() override;
	// Sprites keep the SetDynLight probe of their instance (plan D25); the material programs get no clustered lights.
	Lights::ClusterLightSet LightSet() const override { return Lights::ClusterLightSet::None; }
	const TArray<Vertex::VaoAttribute>* VertexLayout() const override;

private:
	Shader::MaterialProgramOptions m_material;

	int m_fuzzFracLocation = -1;
	int m_viewRightNormalLocation = -1;
	int m_fuzzDivLocation = -1;
	int m_maxDistanceLocation = -1;
	int m_fadeDistanceLocation = -1;
	int m_viewPosLocation = -1;
	int m_fuzzTextureLocation = -1;
	int m_opaqueTextureLocation = -1;
	int m_renderFuzzLocation = -1;
	int m_renderFuzzRefractionColorLocation = -1;
	int m_mapDataTextureLocation = -1;
	int m_lineHeightsTextureLocation = -1;
	int m_colorClampLocation = -1;
	int m_viewAnglesLocation = -1;
	int m_pixelStretchLocation = -1;
	int m_orthoViewLocation = -1;
	int m_useLightProbeLocation = -1;
	int m_enhancedNightvisionLocation = -1;
	int m_maskThresholdLocation = -1;
	int m_cullColorLocation = -1;
	int m_fuzzTimerLocation = -1;

	void ProgramCreated() override;
	Shader::OitOptions GetOitOptions() const;
};

class EntityTransparentProgram : public EntityProgram
{
public:
	EntityTransparentProgram() : EntityProgram("OIT Transparent") { }
};

class EntityCompositeProgram : public EntityProgram
{
public:
	EntityCompositeProgram() : EntityProgram("OIT Composite") { }
};

class EntityFuzzRefractionProgram : public EntityProgram
{
public:
	EntityFuzzRefractionProgram() : EntityProgram("OIT Fuzz Refraction") { }
};

}
