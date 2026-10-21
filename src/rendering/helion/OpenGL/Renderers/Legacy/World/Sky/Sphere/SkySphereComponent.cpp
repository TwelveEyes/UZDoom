/*
** SkySphereComponent.cpp
**
** Port of Helion.Render.OpenGL.Renderers.Legacy.World.Sky.Sphere.SkySphereComponent.
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

#include "SkySphereComponent.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Sky::Sphere
{

SkySphereComponent::SkySphereComponent(Texture::Legacy::LegacyGLTextureManager* textureManager, const SkyInfo& info)
	: m_skySphereRenderer(textureManager, info), m_info(info)
{
	SkySphereShared& shared = SkySphereShared::Get();
	m_vbo = std::make_unique<Buffer::Array::Vertex::StreamVertexBuffer<SkyGeometryVertex>>("Sky geometry");
	m_pipeline = std::make_unique<VertexPipeline<SkyGeometryVertex>>(*shared.Geometry, *m_vbo, "Sky geometry");
}

void SkySphereComponent::Clear()
{
	m_pipeline->Clear();
}

void SkySphereComponent::Add(const SkyGeometryVertex* vertices, int length)
{
	m_vbo->AddMemoryCopy(std::span<const SkyGeometryVertex>(vertices, (size_t)length));
}

void SkySphereComponent::RenderWorldGeometry(Shared::RenderInfo& renderInfo)
{
	SkySphereGeometryShader& program = *SkySphereShared::Get().Geometry;
	program.Bind();

	program.Mvp(renderInfo.Uniforms.Mvp);
	program.TimeFrac(renderInfo.TickFraction);

	m_vbo->UploadIfNeeded();

	m_pipeline->Bind();
	m_pipeline->DrawArrays();
	m_pipeline->Unbind();

	program.Unbind();
}

void SkySphereComponent::RenderSky(Shared::RenderInfo& renderInfo)
{
	m_skySphereRenderer.Render(renderInfo);
}

}
