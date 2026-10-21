/*
** SkySphereComponent.h
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

#pragma once

#include <memory>
#include "OpenGL/Renderers/Legacy/World/Sky/ISkyComponent.h"
#include "OpenGL/Renderers/Legacy/World/Sky/SkyKey.h"
#include "OpenGL/VertexPipeline.h"
#include "SkySphereRenderer.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Sky::Sphere
{

class SkySphereComponent : public ISkyComponent
{
public:
	SkySphereComponent(Texture::Legacy::LegacyGLTextureManager* textureManager, const SkyInfo& info);

	bool HasGeometry() const override { return !m_vbo->Empty(); }
	Buffer::Array::Vertex::VertexBufferObject<SkyGeometryVertex>& Vbo() override { return *m_vbo; }

	void Clear() override;
	void Add(const SkyGeometryVertex* vertices, int length) override;
	void RenderWorldGeometry(Shared::RenderInfo& renderInfo) override;
	void RenderSky(Shared::RenderInfo& renderInfo) override;

	const SkyInfo& Info() const { return m_info; }

private:
	// Helion has one geometry program per component; the program is shared (SkySphereShared), the vertex data and
	// its pipeline are per component.
	std::unique_ptr<Buffer::Array::Vertex::StreamVertexBuffer<SkyGeometryVertex>> m_vbo;
	std::unique_ptr<VertexPipeline<SkyGeometryVertex>> m_pipeline;
	SkySphereRenderer m_skySphereRenderer;
	SkyInfo m_info;
};

}
