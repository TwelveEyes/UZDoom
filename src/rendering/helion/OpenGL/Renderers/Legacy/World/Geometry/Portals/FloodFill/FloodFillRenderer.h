/*
** FloodFillRenderer.h
**
** Port of Helion.Render.OpenGL.Renderers.Legacy.World.Geometry.Portals.FloodFill.FloodFillRenderer.
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** UZDoom mapping: Helion's SectorPlane is RenderPlane (World/Geometry/Sectors/RenderSector.h: SectorPlane + texture handle
** = FTextureID::GetIndex() of the flat, resolved for animation when the texture is bound, + LightLevelAdd). The two programs come from HelionProgramRegistry
** (ProgramType::FloodFill / FloodFillWallClip), not from the renderer's constructor. The static / dynamic free list of
** Helion (LinkedList + node pool) is a std::list.
**
*/

#pragma once

#include <functional>
#include <list>
#include <memory>
#include <unordered_map>
#include <vector>
#include "FloodFillInfo.h"
#include "FloodFillProgram.h"
#include "FloodFillRenderMode.h"
#include "FloodGeometry.h"
#include "Common/Shared/RenderInfo.h"
#include "Common/Shared/World/WallVertices.h"
#include "OpenGL/Texture/Legacy/LegacyGLTextureManager.h"
#include "World/Static/SideTexture.h"
#include "World/Geometry/Sectors/RenderSector.h"

struct sector_t;
struct FLevelLocals;

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Geometry::Portals::FloodFill
{

using RenderPlane = ::Helion::World::Geometry::Sectors::RenderPlane;

class FloodFillRenderer
{
public:
	// UZDoom-only: set by the world renderer. Applies the uniforms of the UZDoom shader contract and of the gl_lightmode
	// light stage (the part of the static program uniform setup that RenderInfo does not cover) to a flood program.
	static inline std::function<void(RenderProgramBase&, Shared::RenderInfo&)> ContractUniforms;

	FloodFillRenderer(Texture::Legacy::LegacyGLTextureManager& glTextureManager, FloodFillRenderMode renderMode);
	~FloodFillRenderer();

	FloodFillRenderer(const FloodFillRenderer&) = delete;
	FloodFillRenderer& operator=(const FloodFillRenderer&) = delete;

	void UpdateTo(FLevelLocals* world);

	void UpdateStaticWall(int floodKey, const RenderPlane& floodPlane, const Shared::World::WallVertices& vertices, double minPlaneZ, double maxPlaneZ,
		::Helion::World::Static::SideTexture sideTexture, int mapId, bool isFloodFillPlane = false);

	// Returns the flood key (0 when none: sky planes, and always in Dynamic mode).
	int AddStaticWall(const RenderPlane& sectorPlane, const Shared::World::WallVertices& vertices, double minPlaneZ, double maxPlaneZ,
		::Helion::World::Static::SideTexture sideTexture, int mapId, bool isFloodFillPlane = false);

	void ClearStaticWall(int floodKey);

	void Render(Shared::RenderInfo& renderInfo);
	void RenderWallClip(Shared::RenderInfo& renderInfo);

	void ClearVertices();
	// UZDoom (plan M5.2, HWHorizonPortal::DrawContents): the plane (texture, height, light) drawn immediately over the given triangles
	// (map coordinates, three corners each), each pixel showing the plane where the view ray meets it, out to the true horizon.
	void RenderHorizon(Shared::RenderInfo& renderInfo, const RenderPlane& plane, const std::vector<::Helion::Geometry::Vec3F>& triangles);


private:
	static constexpr int FloodPlaneAddCount = 2;
	static constexpr int VerticesPerWall = 6;

	Texture::Legacy::LegacyGLTextureManager& m_glTextureManager;
	FloodFillRenderMode m_renderMode;
	FLevelLocals* m_world = nullptr;
	std::vector<std::unique_ptr<FloodFillInfo>> m_floodFillInfos;
	std::unordered_map<int, int> m_textureHandleToFloodFillInfoIndex;
	std::vector<FloodGeometry> m_floodGeometry;
	std::list<FloodGeometry> m_freeData;

	FloodGeometry m_noGeometry;
	std::unique_ptr<Buffer::Array::Vertex::StreamVertexBuffer<FloodFillVertex>> m_horizonVbo;
	std::unique_ptr<VertexPipeline<FloodFillVertex>> m_horizonPipeline;

	FloodFillProgram* GetProgram();
	FloodFillWallClipProgram* GetWallClipProgram();

	FloodFillInfo& CreateFloodFillInfo(const RenderPlane& plane);
	FloodFillInfo& GetOrCreateFloodFillInfo(const RenderPlane& plane, int arrayTextureHandle);
	FloodGeometry* TryGetFloodGeometry(int floodKey);
	bool TryGetFloodFillInfoIndex(int textureHandle, int& index);
	bool IsSkyTexture(int textureHandle) const;

	static void ProjectFloodPlane(Buffer::Array::Vertex::VertexBufferObject<FloodFillVertex>& vbo, int startIndex,
		const Shared::World::WallVertices& vertices, float minZ, float maxZ, float planeZ, float prevPlaneZ, int overrideLightIndex, int addHeight,
		bool add, float renderOptions, int mapId, int textureIndex);

	static void OverwriteAndSubUploadVboWithZero(Buffer::Array::Vertex::VertexBufferObject<FloodFillVertex>& vbo, int bufferOffset, int vertices);

	void Render(FloodFillProgram& program, Shared::RenderInfo& renderInfo, bool wallClip);
	static void SetUniforms(FloodFillProgram& program, Shared::RenderInfo& renderInfo);
	void ClearData();

	static FString GetLabel(const RenderPlane& plane);
};

}
