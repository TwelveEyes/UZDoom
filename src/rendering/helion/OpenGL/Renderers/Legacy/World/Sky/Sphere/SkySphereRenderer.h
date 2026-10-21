/*
** SkySphereRenderer.h
**
** Port of Helion.Render.OpenGL.Renderers.Legacy.World.Sky.Sphere.SkySphereRenderer.
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** Differences to Helion:
**  - The programs, the sphere vertices and the vertex pipeline are shared by every sky (SkySphereShared). Helion
**    creates them per sky, which would compile three render programs per sky here.
**  - Skies have UZDoom's state (SkyInfo): a back layer, an optional front layer of a double sky (Helion's
**    foreground texture), scroll speeds from the level, the sky fog layer and sky transfer offsets. The layers are
**    placed like the classic sky dome (SkySphereTexture::CalcDome).
**
*/

#pragma once

#include <memory>
#include "Common/Shared/RenderInfo.h"
#include "OpenGL/Buffer/VertexBufferObject.h"
#include "OpenGL/VertexPipeline.h"
#include "OpenGL/Renderers/Legacy/World/Sky/SkyKey.h"
#include "SkySphereForegroundShader.h"
#include "SkySphereFogShader.h"
#include "SkySphereGeometryShader.h"
#include "SkySphereShader.h"
#include "SkySphereTexture.h"
#include "SkySphereVertex.h"

struct FLevelLocals;

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Sky::Sphere
{

// Created on first use, destroyed by Release() (renderer shutdown / color mode change: the program sources
// depend on ShaderVars::PaletteColorMode) and lazily recreated.
struct SkySphereShared
{
	SkySphereShader* Sky = nullptr;
	SkySphereForegroundShader* Foreground = nullptr;
	SkySphereFogShader* Fog = nullptr;
	SkySphereGeometryShader* Geometry = nullptr;
	std::unique_ptr<Buffer::Array::Vertex::StaticVertexBuffer<SkySphereVertex>> Vbo;
	std::unique_ptr<VertexPipeline<SkySphereVertex>> Pipeline;

	static SkySphereShared& Get();
	static void Release();
};

class SkySphereRenderer
{
public:
	static constexpr int HorizontalSpherePoints = 64;
	static constexpr int VerticalSpherePoints = 64;

	SkySphereRenderer(Texture::Legacy::LegacyGLTextureManager* textureManager, const SkyInfo& info);

	void Render(Shared::RenderInfo& renderInfo);

	// The sky fog layer over the sky, see SkyInfo::FadeColor. Also used after skyboxes.
	static void RenderFog(Texture::Legacy::LegacyGLTextureManager* textureManager, uint32_t fadeColor, Shared::RenderInfo& renderInfo, FLevelLocals* level);

private:
	Texture::Legacy::LegacyGLTextureManager* m_textureManager;
	SkySphereTexture m_texture;
	SkyInfo m_info;

	static ::Helion::Geometry::mat4 CalculateMvp(const Shared::RenderInfo& renderInfo);
	static void DrawSphere(SkySphereShared& shared, Texture::Legacy::GLLegacyTexture* texture);
	static void SetSkyUniforms(SkySphereShader& skyProgram, Shared::RenderInfo& renderInfo, const SkyTexture& skyTexture,
		const SkyTransformTexture& skyTransform, bool mirror, bool tiled);
	SkyTransformTexture CreateTransform(int textureIndex, int scrollLayer, FLevelLocals* level) const;
};

}
