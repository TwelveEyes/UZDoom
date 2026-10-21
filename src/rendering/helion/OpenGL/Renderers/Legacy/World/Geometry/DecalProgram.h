/*
** DecalProgram.h
**
** UZDoom-only (plan M4.5): the program of DecalRenderer. Follows StaticShader: the Helion array texture fragment
** program, palette (colormap fetch) or truecolor (with the gl_lightmode equations, UzdoomLightMode).
** The light level, sector colour map and sector fog come from the same sector buffers as the walls the decal lies on
** (the light buffer index of the wall slice is a uniform), so decals shade exactly like their wall.
** Decals are not drawn with custom material shaders. The truecolor program gets the clustered dynamic lights of the
** walls (plan D25; HWDecal::DrawDecal's SetLightIndex of the wall).
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

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Geometry
{

// Create with RenderProgram::Create<DecalProgram>() (HelionProgramRegistry ProgramType::Decal).
class DecalProgram : public RenderProgramBase
{
public:
	DecalProgram();

	// Per decal, vertex stage.
	void DecalAlpha(float alpha);
	// Layer of the array texture (GLLegacyTexture::ArrayIndex).
	void DecalLayer(int layer);
	// Index into the sector light / colormap / fog buffers (what the wall's vertices carry in RenderOptions).
	void DecalLightBufferIndex(int index);
	// Added to the sector light level (the rellight of the wall; 256 for RF_FULLBRIGHT decals).
	void DecalLightOffset(float offset);
	// Additively drawn decals must not apply the fog colour a second time (SetFog(0, -1) of DrawDecal).
	void DecalBlackFog(bool value);
	// LEVEL3_NOCOLOREDSPRITELIGHTING.
	void DecalDecolorize(bool value);

	// Per decal, fragment stage. textureMode is TM_NORMAL / TM_STENCIL / TM_OPAQUE / TM_INVERSE / TM_ALPHATEXTURE / TM_OPAQUEINVERSE
	// (the uTextureMode of FRenderState::SetTextureMode(FRenderStyle)), objectColor the modulation colour (FRenderState::SetObjectColor),
	// alphaThreshold the limit of the alpha test: pixels with a lower alpha are discarded (AlphaFunc(Alpha_GEqual, t); 0 = none).
	void DecalStyle(int textureMode, const FVector4& objectColor, float alphaThreshold);

protected:
	FString VertexShader() override;
	std::optional<FString> FragmentShader() override;
	const TArray<Vertex::VaoAttribute>* VertexLayout() const override;
	void ProgramCreated() override;

private:
	int m_alphaLocation = -1;
	int m_layerLocation = -1;
	int m_lightBufferIndexLocation = -1;
	int m_lightOffsetLocation = -1;
	int m_blackFogLocation = -1;
	int m_decolorizeLocation = -1;
	int m_objectColorLocation = -1;
	int m_textureModeLocation = -1;
	int m_alphaThresholdLocation = -1;
};

}
