/*
** SkyBoxComponent.cpp
**
** UZDoom-only: ISkyComponent that draws an FSkyBox (GLDEFS skybox / skybox sky texture) as a cube.
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

#include <vector>
#include "g_levellocals.h"
#include "g_level.h"
#include "gametexture.h"
#include "hw_renderstate.h"
#include "r_defs.h"
#include "skyboxtexture.h"
#include "texturemanager.h"
#include "v_video.h"
#include "SkyBoxComponent.h"
#include "BindTextures.h"
#include "OpenGL/Renderers/Legacy/World/Sky/SkyProjection.h"
#include "OpenGL/Shader/ProgramRegistry.h"
#include "OpenGL/Renderers/Legacy/World/Sky/Sphere/SkySphereRenderer.h"

CVAR(Bool,gl_noskyboxes, false, 0)

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Sky::SkyBox
{

using namespace ::Helion::Geometry;
using Sphere::SkySphereShared;
using Sphere::SkySphereVertex;
using Texture::Legacy::GLLegacyTexture;

namespace
{

// Vertex ranges of FSkyVertexBuffer: the wrapped side strip, then north, east, south, west, bottom, top and flipped top.
constexpr int SideStart = 0;
constexpr int FaceStart[7] = { 10, 14, 18, 22, 26, 30, 34 };

struct SkyBoxShared
{
	SkyBoxShader* Program = nullptr;
	std::unique_ptr<Buffer::Array::Vertex::StaticVertexBuffer<SkySphereVertex>> Vbo;
	std::unique_ptr<VertexPipeline<SkySphereVertex>> Pipeline;
};

std::unique_ptr<SkyBoxShared> s_shared;

SkyBoxShared& GetShared()
{
	if (s_shared != nullptr)
		return *s_shared;

	auto shared = std::make_unique<SkyBoxShared>();
	shared->Program = OpenGL::Shader::HelionProgramRegistry::Get<SkyBoxShader>(OpenGL::Shader::ProgramType::SkyBox);

	// FSkyVertexBuffer::CreateDome
	const SkySphereVertex vertices[] =
	{
		// all sides
		{ 128.f, 128.f, -128.f, 0, 0 },
		{ 128.f, -128.f, -128.f, 0, 1 },
		{ -128.f, 128.f, -128.f, 0.25f, 0 },
		{ -128.f, -128.f, -128.f, 0.25f, 1 },
		{ -128.f, 128.f, 128.f, 0.5f, 0 },
		{ -128.f, -128.f, 128.f, 0.5f, 1 },
		{ 128.f, 128.f, 128.f, 0.75f, 0 },
		{ 128.f, -128.f, 128.f, 0.75f, 1 },
		{ 128.f, 128.f, -128.f, 1, 0 },
		{ 128.f, -128.f, -128.f, 1, 1 },

		// north face
		{ 128.f, 128.f, -128.f, 0, 0 },
		{ -128.f, 128.f, -128.f, 1, 0 },
		{ 128.f, -128.f, -128.f, 0, 1 },
		{ -128.f, -128.f, -128.f, 1, 1 },

		// east face
		{ -128.f, 128.f, -128.f, 0, 0 },
		{ -128.f, 128.f, 128.f, 1, 0 },
		{ -128.f, -128.f, -128.f, 0, 1 },
		{ -128.f, -128.f, 128.f, 1, 1 },

		// south face
		{ -128.f, 128.f, 128.f, 0, 0 },
		{ 128.f, 128.f, 128.f, 1, 0 },
		{ -128.f, -128.f, 128.f, 0, 1 },
		{ 128.f, -128.f, 128.f, 1, 1 },

		// west face
		{ 128.f, 128.f, 128.f, 0, 0 },
		{ 128.f, 128.f, -128.f, 1, 0 },
		{ 128.f, -128.f, 128.f, 0, 1 },
		{ 128.f, -128.f, -128.f, 1, 1 },

		// bottom face
		{ 128.f, -128.f, -128.f, 0, 0 },
		{ -128.f, -128.f, -128.f, 1, 0 },
		{ 128.f, -128.f, 128.f, 0, 1 },
		{ -128.f, -128.f, 128.f, 1, 1 },

		// top face
		{ 128.f, 128.f, -128.f, 0, 0 },
		{ -128.f, 128.f, -128.f, 1, 0 },
		{ 128.f, 128.f, 128.f, 0, 1 },
		{ -128.f, 128.f, 128.f, 1, 1 },

		// top face flipped
		{ 128.f, 128.f, -128.f, 0, 1 },
		{ -128.f, 128.f, -128.f, 1, 1 },
		{ 128.f, 128.f, 128.f, 0, 0 },
		{ -128.f, 128.f, 128.f, 1, 0 },
	};

	shared->Vbo = std::make_unique<Buffer::Array::Vertex::StaticVertexBuffer<SkySphereVertex>>("Sky box", (int)std::size(vertices));
	shared->Vbo->AddMemoryCopy(std::span<const SkySphereVertex>(vertices, std::size(vertices)));
	shared->Vbo->UploadIfNeeded();
	shared->Pipeline = std::make_unique<VertexPipeline<SkySphereVertex>>(*shared->Program, *shared->Vbo, "Sky box");

	s_shared = std::move(shared);
	return *s_shared;
}

void DrawFace(SkyBoxShared& shared, Texture::Legacy::LegacyGLTextureManager* textureManager, FGameTexture* face, int first, int count)
{
	// CLAMP_XY: the faces must not wrap or the filter bleeds the opposite edge into the seams. A texture of its own,
	// so the clamp does not change the wall texture (or a shared array layer) of the same image.
	GLLegacyTexture* texture = face != nullptr ? textureManager->GetStandaloneTexture(face->GetID().GetIndex(), true) : textureManager->NullTexture;
	texture->Bind(BindTextures::BoundTexture);
	shared.Pipeline->DrawArrays(Util::PrimitiveType::TriangleStrip, first, count);
}

}

SkyBoxComponent::SkyBoxComponent(Texture::Legacy::LegacyGLTextureManager* textureManager, const SkyInfo& info)
	: m_textureManager(textureManager), m_info(info)
{
	m_vbo = std::make_unique<Buffer::Array::Vertex::StreamVertexBuffer<Sphere::SkyGeometryVertex>>("Sky box geometry");
	m_pipeline = std::make_unique<VertexPipeline<Sphere::SkyGeometryVertex>>(*SkySphereShared::Get().Geometry, *m_vbo, "Sky box geometry");
}

bool SkyBoxComponent::SkyBoxesDisabled()
{
	return gl_noskyboxes;
}

void SkyBoxComponent::ReleaseShared()
{
	s_shared.reset();
}

void SkyBoxComponent::Clear()
{
	m_pipeline->Clear();
}

void SkyBoxComponent::Add(const Sphere::SkyGeometryVertex* vertices, int length)
{
	m_vbo->AddMemoryCopy(std::span<const Sphere::SkyGeometryVertex>(vertices, (size_t)length));
}

void SkyBoxComponent::RenderWorldGeometry(Shared::RenderInfo& renderInfo)
{
	Sphere::SkySphereGeometryShader& program = *SkySphereShared::Get().Geometry;
	program.Bind();

	program.Mvp(renderInfo.Uniforms.Mvp);
	program.TimeFrac(renderInfo.TickFraction);

	m_vbo->UploadIfNeeded();

	m_pipeline->Bind();
	m_pipeline->DrawArrays();
	m_pipeline->Unbind();

	program.Unbind();
}

void SkyBoxComponent::RenderSky(Shared::RenderInfo& renderInfo)
{
	FLevelLocals* level = renderInfo.ViewSector != nullptr ? renderInfo.ViewSector->Level : primaryLevel;

	FGameTexture* gameTexture = TexMan.GetGameTexture(FSetTextureID(m_info.Texture), false);
	FSkyBox* box = gameTexture != nullptr ? dynamic_cast<FSkyBox*>(gameTexture->GetTexture()) : nullptr;
	if (box == nullptr)
		return;

	SkyBoxShared& shared = GetShared();
	FRenderState& state = *screen->RenderState();

	bool sky2 = m_info.ScrollLayer == 1;
	float scrollDegrees = m_info.ScrollLayer == 0 ? level->hw_sky1pos : (sky2 ? level->hw_sky2pos : 0.0f);
	float xOffset = m_info.Offset.X + scrollDegrees;
	float stretch = renderInfo.PixelStretch;
	FVector3 rotateVector = sky2 ? level->info->skyrotatevector2 : level->info->skyrotatevector;

	// The view of classic SetViewMatrix at the origin: rotation only.
	float yawDegrees = 270.0f - (float)(renderInfo.Camera->YawRadians * (180.0 / 3.14159265358979323846));
	float pitchDegrees = -(float)(renderInfo.Camera->PitchRadians * (180.0 / 3.14159265358979323846));
	mat4 mvp = SkyProjection(renderInfo, 1.0f, 1000.0f);
	mvp.rotate(pitchDegrees, 1.0f, 0.0f, 0.0f);
	mvp.rotate(yawDegrees, 0.0f, 1.0f, 0.0f);
	mvp.scale(-1.0f, stretch, 1.0f);
	// Undo the map's vertical scaling as skyboxes are true cubes.
	mvp.scale(1.0f, 1.0f / stretch, 1.0f);
	mvp.rotate(-180.0f + xOffset, rotateVector.X, rotateVector.Z, rotateVector.Y);

	SkyBoxShader& program = *shared.Program;
	program.Bind();
	state.DisableBlend();
	program.Mvp(mvp);
	program.BoundTexture(BindTextures::BoundTexture);
	program.ColormapTexture(BindTextures::Colormap);
	program.ColorMix(renderInfo.Uniforms.ColorMix.Sky);
	program.GammaCorrection(renderInfo.Uniforms.GammaCorrection);
	program.HasInvulnerability(renderInfo.Uniforms.DrawInvulnerability);
	program.PaletteIndex((int)renderInfo.Uniforms.PaletteIndex);
	program.ColorMapIndex(renderInfo.Uniforms.ColorMapUniforms.SkyIndex);

	shared.Pipeline->Bind();

	int faces;
	if (box->GetSkyFace(5) != nullptr)
	{
		faces = 4;
		for (int i = 0; i < 4; i++)
			DrawFace(shared, m_textureManager, box->GetSkyFace(i), FaceStart[i], 4);
	}
	else
	{
		faces = 1;
		DrawFace(shared, m_textureManager, box->GetSkyFace(0), SideStart, 10);
	}

	// top
	DrawFace(shared, m_textureManager, box->GetSkyFace(faces), FaceStart[box->GetSkyFlip() ? 6 : 5], 4);
	// bottom
	DrawFace(shared, m_textureManager, box->GetSkyFace(faces + 1), FaceStart[4], 4);

	shared.Pipeline->Unbind();
	GLLegacyTexture::Unbind(BindTextures::BoundTexture);
	program.Unbind();

	Sphere::SkySphereRenderer::RenderFog(m_textureManager, m_info.FadeColor, renderInfo, level);
}

}
