/*
** SkyBoxComponent.h
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
** Port of FSkyVertexBuffer::RenderBox: the cube is 256 units wide (+-128), drawn around the viewer without
** translation, rotated by the sky scroll about Level->info->skyrotatevector(2) and scaled by the pixel stretch.
** Three face boxes use one wrapped side strip, six face boxes four sides; top / bottom always have a face each.
**
*/

#pragma once

#include <memory>
#include "OpenGL/Renderers/Legacy/World/Sky/ISkyComponent.h"
#include "OpenGL/Renderers/Legacy/World/Sky/SkyKey.h"
#include "OpenGL/Renderers/Legacy/World/Sky/Sphere/SkySphereVertex.h"
#include "OpenGL/Texture/Legacy/LegacyGLTextureManager.h"
#include "OpenGL/VertexPipeline.h"
#include "SkyBoxShader.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Sky::SkyBox
{

class SkyBoxComponent : public ISkyComponent
{
public:
	SkyBoxComponent(Texture::Legacy::LegacyGLTextureManager* textureManager, const SkyInfo& info);

	bool HasGeometry() const override { return !m_vbo->Empty(); }
	Buffer::Array::Vertex::VertexBufferObject<Sphere::SkyGeometryVertex>& Vbo() override { return *m_vbo; }

	void Clear() override;
	void Add(const Sphere::SkyGeometryVertex* vertices, int length) override;
	void RenderWorldGeometry(Shared::RenderInfo& renderInfo) override;
	void RenderSky(Shared::RenderInfo& renderInfo) override;

	// gl_noskyboxes: sector skyviewpoint portals (PORTS_SKYVIEWPOINT) are then drawn as the plain level sky by the
	// geometry renderer (HWWall::SkyPlane); the check lives here so that every caller uses the same CVar.
	static bool SkyBoxesDisabled();

	// Destroys the shared program and cube vertices (renderer shutdown / color mode change).
	static void ReleaseShared();

private:
	Texture::Legacy::LegacyGLTextureManager* m_textureManager;
	SkyInfo m_info;
	std::unique_ptr<Buffer::Array::Vertex::StreamVertexBuffer<Sphere::SkyGeometryVertex>> m_vbo;
	std::unique_ptr<VertexPipeline<Sphere::SkyGeometryVertex>> m_pipeline;
};

}
