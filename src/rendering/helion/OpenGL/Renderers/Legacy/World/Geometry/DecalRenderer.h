/*
** DecalRenderer.h
**
** UZDoom-only (plan M4.5), Helion style port of src/rendering/hwrenderer/scene/hw_decal.cpp (HWWall::ProcessDecal /
** ProcessDecals, HWDecal::DrawDecal, classic DrawDecals / DrawDecalsForMirror). Helion has no decals.
**
** Flow of a frame:
**   GeometryRenderer emits a wall slice of a side and calls RenderDecals(wall, renderInfo) for it: the decals of the side
**   (side_t::AttachedDecals, DBaseDecal) are projected onto the slice (clipped to its top / bottom, with the light buffer
**   index and light offset of the slice) and gathered: decals of a render style that needs no blending go to the opaque list,
**   every other style registers a draw closure with Data::SortedTranslucentRenderer (sorted back to front with the
**   other translucent items); decals on mirror surfaces go to the mirror list (RenderMirrorDecals).
**   LegacyWorldRenderer calls RenderOpaque(renderInfo) after the opaque walls, SortedTranslucentRenderer::Render runs the others.
**   Clear() at the start of the next frame.
**
** A wall that is split in several slices (3D floor light slices, transfer heights, the pieces of a two sided wall) needs one
** RenderDecals call per slice: a decal crossing slices is clipped into each of them and takes the light of each (this replaces the
** lightlist / SetSplitPlanes loop of HWDecal::DrawDecal). Decals are drawn without depth write and with polygon offset (-1, -128)
** like the original. The player's extra light is the extraLight uniform of the program, not part of the light offset.
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

#include <bit>
#include <cstdint>
#include <functional>
#include <memory>
#include "tarray.h"
#include "vectors.h"
#include "palentry.h"
#include "renderstyle.h"
#include "Common/Shared/RenderInfo.h"
#include "OpenGL/Buffer/VertexBufferObject.h"
#include "OpenGL/Renderers/Legacy/World/Data/SortedTranslucentRenderer.h"
#include "OpenGL/Renderers/Legacy/World/Geometry/DecalProgram.h"
#include "OpenGL/Renderers/Legacy/World/Geometry/DecalVertex.h"
#include "OpenGL/Texture/Legacy/GLLegacyTexture.h"
#include "OpenGL/Texture/Legacy/LegacyGLTextureManager.h"
#include "OpenGL/VertexPipeline.h"
#include "World/Geometry/Walls/WallLocation.h"

struct side_t;
struct sector_t;
class DBaseDecal;
class FGameTexture;

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Geometry
{

// One emitted wall slice a side's decals are projected on: the data HWWall carries (glseg, ztop / zbottom, light of the wall).
struct DecalWall
{
	using WallLocation = ::Helion::World::Geometry::Walls::WallLocation;

	side_t* Side = nullptr;
	// Upper / Middle / Lower / Middle3D (the wall of a 3D floor: RENDERWALL_FFBLOCK).
	WallLocation Location = WallLocation::Middle;
	// RENDERWALL_MIRRORSURFACE: the decals are drawn by RenderMirrorDecals when the mirror is drawn.
	bool Mirror = false;
	// The texture of the wall is masked (FGameTexture::isMasked): no decals on masked 3D floor sides.
	bool MaskedTexture = false;
	sector_t* FrontSector = nullptr;
	// null for one sided walls
	sector_t* BackSector = nullptr;

	// glseg of the slice: the end points on the map and the part of the whole side they cover (0..1 along the side).
	float X1 = 0, Y1 = 0, X2 = 0, Y2 = 0;
	float FracLeft = 0, FracRight = 1;
	// ztop / zbottom of HWWall: the heights of the slice at (X1, Y1) and (X2, Y2).
	float ZTopLeft = 0, ZTopRight = 0, ZBottomLeft = 0, ZBottomRight = 0;

	// What the wall's vertices carry: the index into the sector light / colormap / fog buffers, and the light added to the sector light
	// level (the vertexLightLevel of the wall vertices: HWWall::rellight, the fake contrast of the wall).
	int LightBufferIndex = 0;
	float LightLevelOffset = 0;

	// The light buffer index of a wall vertex (VertexOptions::PackRender: 22 bits at bit 10 of RenderOptions).
	static int LightBufferIndexOf(float renderOptions) { return (std::bit_cast<int>(renderOptions) >> 10) & 0x3FFFFF; }
};

class DecalRenderer
{
public:
	// UZDoom-only: set by the world renderer, the same hook as FloodFillRenderer::ContractUniforms. Applies the uniforms of the gl_lightmode
	// light stage / viewpoint block (the part of the static program uniform setup that RenderInfo does not cover) to the decal program.
	static inline std::function<void(RenderProgramBase&, Shared::RenderInfo&)> ContractUniforms;

	// program from HelionProgramRegistry (ProgramType::Decal).
	DecalRenderer(DecalProgram* program, Texture::Legacy::LegacyGLTextureManager* textureManager, Data::SortedTranslucentRenderer* sortedTranslucentRenderer);
	~DecalRenderer();

	DecalRenderer(const DecalRenderer&) = delete;
	DecalRenderer& operator=(const DecalRenderer&) = delete;

	// HWWall::ProcessDecals: gathers the decals of wall.Side projected on the slice.
	void RenderDecals(const DecalWall& wall, Shared::RenderInfo& renderInfo);

	// classic DrawDecals for the opaque list: draws the decals of the render styles that need no blending, gathered this frame.
	// In vanilla render mode also the blended decals, back to front: the world pass clears the depth buffer before the entities, so
	// the decals have to be drawn while the depth of the walls is still there (the sorted bucket would draw them through walls).
	void RenderOpaque(Shared::RenderInfo& renderInfo);

	// gl_vanillarender (and not a portal view): set before the decals are gathered.
	void SetVanillaRender(bool vanillaRender) { m_vanillaRender = vanillaRender; }

	// HWWall::DrawDecalsForMirror: draws the decals gathered for the mirror surfaces of side (every render style).
	void RenderMirrorDecals(Shared::RenderInfo& renderInfo, const side_t* side);

	void Clear();
	bool Empty() const { return m_opaque.Size() == 0 && m_translucent.Size() == 0 && m_mirror.Size() == 0; }

private:
	// HWDecal
	struct Decal
	{
		const side_t* Side = nullptr;
		Texture::Legacy::GLLegacyTexture* Texture = nullptr;
		FRenderStyle Style;
		PalEntry ObjectColor = 0xffffffff;
		float Alpha = 1;
		int TextureMode = 0;
		float AlphaThreshold = 0;
		int LightBufferIndex = 0;
		float LightOffset = 0;
		// palette color mode: the colormap of the decal's translation
		int ColorMapIndex = 0;
		bool BlackFog = false;
		bool Decolorize = false;
		// RF_FULLBRIGHT: HWDecal's dynlightindex = -1
		bool NoDynamicLights = false;
		// first of the four vertices (triangle strip) in the vertex buffer
		int VertexIndex = 0;
	};

	DecalProgram* m_program;
	Texture::Legacy::LegacyGLTextureManager* m_textureManager;
	Data::SortedTranslucentRenderer* m_sortedTranslucentRenderer;
	bool m_paletteColorMode;

	Buffer::Array::Vertex::DynamicVertexBuffer<DecalVertex> m_vbo;
	std::unique_ptr<VertexPipeline<DecalVertex>> m_pipeline;
	TArray<Decal> m_opaque;
	TArray<Decal> m_translucent;
	// Vanilla render mode: the squared view distance of each m_translucent decal (sort key of RenderOpaque).
	TArray<float> m_translucentKeys;
	bool m_vanillaRender = false;
	TArray<Decal> m_mirror;

	void ProcessDecal(const DecalWall& wall, DBaseDecal* decal, Shared::RenderInfo& renderInfo);
	static bool IsOpaqueStyle(FRenderStyle style);
	Texture::Legacy::GLLegacyTexture* GetTexture(DBaseDecal* decal, FGameTexture* gameTexture);

	void SetUniforms(Shared::RenderInfo& renderInfo);
	void DrawDecal(const Decal& decal, Shared::RenderInfo& renderInfo);
};

}
