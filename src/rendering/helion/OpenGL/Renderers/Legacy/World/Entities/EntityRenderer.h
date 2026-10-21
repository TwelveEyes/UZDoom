/*
** EntityRenderer.h
**
** Port of Helion.Render.OpenGL.Renderers.Legacy.World.Entities.EntityRenderer (health bars are not ported, plan D20)
** with the CPU side of UZDoom's HWSprite (Process / ProcessParticle / AdjustVisualThinker / PutSprite).
** Partial class files: EntityRenderer.cpp (actors), EntityRenderer.Particles.cpp, EntityRenderer.VisualThinkers.cpp.
**
**---------------------------------------------------------------------------
**
** Copyright 2002-2016 Christoph Oelckers
** Copyright 2017-2025 GZDoom Maintainers and Contributors
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
*/

#pragma once

#include <functional>
#include <memory>
#include <vector>
#include "tarray.h"
#include "renderstyle.h"
#include "palentry.h"
#include "vectors.h"
#include "OpenGL/Renderers/Legacy/World/VertexOptions.h"
#include "Common/Shared/RenderInfo.h"
#include "OpenGL/Renderers/IStyleRenderer.h"
#include "OpenGL/Renderers/Legacy/World/Data/RenderDataManager.h"
#include "OpenGL/Renderers/Legacy/World/Data/RenderDataPool.h"
#include "OpenGL/Renderers/Legacy/World/Data/SortedTranslucentRenderer.h"
#include "OpenGL/Renderers/Legacy/World/Entities/EntityProgram.h"
#include "OpenGL/Renderers/Legacy/World/Entities/EntityVertex.h"
#include "OpenGL/Renderers/Legacy/World/Entities/SpriteLight.h"
#include "OpenGL/Texture/Legacy/LegacyGLTextureManager.h"
#include "Resources/Definitions/Decorate/Properties/Enums/RenderStyle.h"
#include "World/Geometry/Sectors/TransferHeightsView.h"
#include "Util/Geometry.h"

class AActor;
class DVisualThinker;
struct FLevelLocals;
struct particle_t;
struct sector_t;
struct subsector_t;
struct spritedef_t;

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Entities
{

class EntityModelRenderer;

class EntityRenderer : public StyleRendererBase
{
public:
	static constexpr int RenderPoolSize = 512;

	// Returns the program of the material shader (GLDEFS hardware shader / material, Warp, Specular, PBR) an entity sprite is
	// drawn with, nullptr when there is none. Sprites whose texture has such a shader are drawn through SortedTranslucentRenderer.
	using MaterialProgramLookup = std::function<EntityProgram*(int materialShaderIndex)>;

	// The programs come from HelionProgramRegistry, the renderer does not own them.
	EntityRenderer(Texture::Legacy::LegacyGLTextureManager& textureManager, EntityProgram* program, EntityTransparentProgram* programTransparent,
		EntityCompositeProgram* programComposite, EntityFuzzRefractionProgram* programFuzzRefraction);
	~EntityRenderer();

	EntityRenderer(const EntityRenderer&) = delete;
	EntityRenderer& operator=(const EntityRenderer&) = delete;

	// ---- wiring (once) ----
	void SetSortedRenderer(Data::SortedTranslucentRenderer* sortedRenderer) { m_sortedRenderer = sortedRenderer; }
	void SetModelRenderer(EntityModelRenderer* modelRenderer) { m_modelRenderer = modelRenderer; }
	void SetMaterialProgramLookup(MaterialProgramLookup lookup) { m_materialProgramLookup = std::move(lookup); }

	// ---- per tick gather ----
	// The RenderInfo of the view being gathered: the transfer heights view and the camera used by the CPU side (models, culling).
	// Call every gather, before Clear/RenderEntity/RenderParticles of a tick: also latches the per frame state (fullbright scene, sprite clip).
	void SetRenderInfo(Shared::RenderInfo& renderInfo);

	// True while the view is a mirror (RF2_INVISIBLEINMIRRORS / RF2_ONLYVISIBLEINMIRRORS).
	bool InMirror = false;

	bool HasDataToRenderByStyle(Data::RenderDataStyle style) const { return m_dataManager->HasDataToRenderByStyle(style); }
	bool HasStyleToRender(Data::RenderDataStyle style) override { return m_dataManager->HasDataToRenderByStyle(style); }
	void Render(Data::RenderDataStyle style) override { m_dataManager->RenderByRenderStyle(style); }

	void UpdateTo(FLevelLocals* world);
	void Clear(FLevelLocals* world);
	void ResetInterpolation(FLevelLocals* world) { Clear(world); }

	// HWSprite::Process + PutSprite for one actor. viewPosition is the interpolated view position (the sprite rotation is chosen from it).
	void RenderEntity(AActor* entity, const ::Helion::Geometry::Vec2D& viewPosition, int renderIndex);

	// Queues the sorted draws of the tick (D10 styles) into the SortedTranslucentRenderer. Call once per frame after
	// SortedTranslucentRenderer::Clear() and before its Render(); the draws are re-queued because the entity data lives a whole tick.
	void QueueSortedDraws(Shared::RenderInfo& renderInfo);

	// Static render mode (render blockmap, no BSP): every active particle / VisualThinker of the level, culled by distance and
	// the half space in front of the camera.
	void RenderParticles(FLevelLocals* level, Shared::RenderInfo& renderInfo, int renderIndex);
	// BSP mode: the particles and VisualThinkers of one visited subsector (classic RenderParticles(sub, front)).
	void RenderParticles(subsector_t* subsector, Shared::RenderInfo& renderInfo, int renderIndex);

	// ---- passes ----
	void RenderOpaque(Shared::RenderInfo& renderInfo);
	void RenderOitTransparentPass(Shared::RenderInfo& renderInfo);
	void RenderOitTransparentFuzzPass(Shared::RenderInfo& renderInfo);
	void StartRenderOitCompositePass(Shared::RenderInfo& renderInfo);
	void RenderOitFuzzRefractionPass(Shared::RenderInfo& renderInfo, bool renderColor);

private:
	using GLLegacyTexture = Texture::Legacy::GLLegacyTexture;

	// STYLEHW_Normal / Solid / NoAlphaTest of hw_drawstructs.h
	static constexpr int HwStyleNormal = 0;
	static constexpr int HwStyleSolid = 1;
	static constexpr int HwStyleNoAlphaTest = 2;

	// How an instance is drawn (the D10 split).
	struct StyleChoice
	{
		bool Sorted = false;
		Helion::Resources::Definitions::Decorate::Properties::Enums::RenderStyle Style = Helion::Resources::Definitions::Decorate::Properties::Enums::RenderStyle::Normal;
		FRenderStyle Legacy = {};	// the blend of the sorted draw
		int Fuzz = 0;				// SurfaceOptions fuzz bit
		int FuzzType = 0;
		int Options = 0;			// EntityOptions bits of the style (NoAlphaTest, SubtractiveStyle, Stencil, ...)
		float Alpha = 1.0f;
		int MaterialShader = -1;	// >= 0: material program of the sorted draw
	};

	// Everything the CPU decides for one instance of an actor / particle / VisualThinker.
	struct SpriteInstance
	{
		EntityVertex Vertex = {};
		GLLegacyTexture* Texture = nullptr;
		GLLegacyTexture* Brightmap = nullptr;
		StyleChoice Style;
		float SortX = 0, SortY = 0, SortZ = 0;
		float ObjectColor = 0, AddColor = 0;
	};

	// A sorted draw of the tick: queued into the SortedTranslucentRenderer every frame (QueueSortedDraws).
	struct SortedItem
	{
		Data::RenderData<EntityVertex>* Data = nullptr;
		float X = 0, Y = 0, Z = 0;
		FRenderStyle Legacy = {};
		int MaterialShader = -1;
		float ObjectColor = 0, AddColor = 0;
		FVector3 LightProbe = FVector3(0, 0, 0);
	};

	struct SpriteLight
	{
		int LightLevel = 0;					// the sector light level of the vertex (the shader adds ExtraLight and the buffer light)
		int LightBufferIndex = 0;
		sector_t* RenderSector = nullptr;	// the sector the colours come from (fake flat copy for heightsec sectors)
		sector_t* LightSector3D = nullptr;	// the control sector of the 3D light slab, if any
	};

	// Size and offsets of a sprite texture in image pixels (the units the EntityVertex offsets and TextureInfo use) and the
	// factor that converts pixels to world units at scale 1 (1 / FTexture scale; hires textures are smaller in the world).
	struct TextureGeometry
	{
		float FoldX = 1, FoldY = 1;
		int ImageWidth = 1, ImageHeight = 1;
		int OffsetX = 0;		// mirrored (ImageWidth - Offset.X) when the picture is flipped
		int OffsetY = 0;		// texture top offset
	};

	Texture::Legacy::LegacyGLTextureManager& m_textureManager;
	EntityProgram* m_program;
	EntityTransparentProgram* m_programTransparent;
	EntityCompositeProgram* m_programComposite;
	EntityFuzzRefractionProgram* m_programFuzzRefraction;
	std::unique_ptr<Data::RenderDataPool<EntityVertex>> m_renderDataPoolArray;
	std::unique_ptr<Data::RenderDataPool<EntityVertex>> m_renderDataPoolOverflow;
	// One single instance RenderData per sorted draw of the tick.
	std::unique_ptr<Data::RenderDataPool<EntityVertex>> m_renderDataPoolSorted;
	std::vector<Data::RenderData<EntityVertex>*> m_sortedUsed;
	std::vector<SortedItem> m_sortedItems;
	std::unique_ptr<Data::RenderDataManager<EntityVertex>> m_dataManager;

	Data::SortedTranslucentRenderer* m_sortedRenderer = nullptr;
	EntityModelRenderer* m_modelRenderer = nullptr;
	MaterialProgramLookup m_materialProgramLookup;
	Shared::RenderInfo* m_renderInfo = nullptr;

	::Helion::World::Geometry::Sectors::TransferHeightView m_transferHeightView = ::Helion::World::Geometry::Sectors::TransferHeightView::Middle;
	FLevelLocals* m_level = nullptr;
	FullbrightState m_fullbrightState;
	bool m_spriteAlpha = true;
	int m_spriteClip = -1;							// gl_spriteclip: -1 = anamorphosis, 0 = none, > 0 = HWSprite::PerformSpriteClipAdjustment
	bool m_vanillaRender = false;					// gl_vanillarender: the shader clips sprites with the clip textures
	bool m_paletteColorMode = false;

	void SetUniforms(EntityProgram& program, Shared::RenderInfo& renderInfo);

	// ---- shared helpers (EntityRenderer.cpp) ----
	// Appends the instance to the bucket of its style or stores a sorted draw.
	void Emit(const SpriteInstance& instance);
	// FRenderStyle (after the HWSprite adjustments) -> D10 bucket. fuzzOverride >= 0: gl_fuzztype variant.
	StyleChoice ClassifyStyle(FRenderStyle renderStyle, float trans, int hwStyleFlags, bool fullbright, bool translucentTexture, int fuzzOverride);
	SpriteLight GetSpriteLight(sector_t* sector, AActor* thing, DVisualThinker* spr, const DVector3& pos, bool fullbright, bool allowLightList);
	void SetLightProbe(EntityVertex& vertex, AActor* actor, particle_t* particle, bool fullbright);
	static void SetColors(EntityVertex& vertex, sector_t* renderSector, PalEntry thingColor, bool fullbright);
	static TextureGeometry GetTextureGeometry(GLLegacyTexture* texture, bool flip);
	static float PackColor(PalEntry color);
	static PalEntry UnpackColor(float packed);
	// The billboard decision of HWSprite::CalculateVertices for actors.
	static EntityOrientation GetActorOrientation(AActor* thing, uint32_t spritetype);
	// gl_spriteclip > 0: HWSprite::PerformSpriteClipAdjustment; returns the shift of the sprite along z.
	static float GetSpriteClipShift(AActor* thing, const DVector2& thingPos, float spriteHeight, float z1, float z2);
	// gl_spriteclip == -1: the sprite anamorphosis of HWSprite::Process: returns the bias towards the viewpoint (1 = none).
	static float GetAnamorphosisBias(AActor* thing, const DVector3& thingPos, float z1, float z2);
	// Puts the finished vertex of a particle / VisualThinker into its bucket.
	void EmitParticle(EntityVertex& vertex, GLLegacyTexture* texture, GLLegacyTexture* brightmap, const StyleChoice& style, const DVector3& pos);

	// ---- actors ----
	void ProcessSprite(AActor* thing, const ::Helion::Geometry::Vec2D& position, int renderIndex, bool isSpriteShadow);

	// ---- particles (EntityRenderer.Particles.cpp) ----
	// Particle / VisualThinker particle: HWSprite::ProcessParticle.
	void RenderParticle(particle_t* particle, sector_t* sector, DVisualThinker* visualThinker, int renderIndex);
	bool ParticleCulled(const DVector3& pos) const;
	void RenderParticlesInSubsector(subsector_t* subsector, int renderIndex);

	// ---- visual thinkers (EntityRenderer.VisualThinkers.cpp) ----
	// HWSprite::AdjustVisualThinker: a VisualThinker that is a textured sprite.
	void RenderVisualThinker(DVisualThinker* visualThinker, sector_t* sector, int renderIndex);
};

}
