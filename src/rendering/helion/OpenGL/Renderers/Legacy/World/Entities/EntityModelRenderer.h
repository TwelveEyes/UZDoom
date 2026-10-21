/*
** EntityModelRenderer.h
**
** UZDoom-only (plan M4.3): models and voxels of actors and of the player weapon. Helion has no model support. This is
** FHWModelRenderer (hw_models.cpp) on a ModelProgram, with the model part of HWSprite (Process: frame lookup, light, colour map,
** render style, PutSprite: dynamic model lights, DrawSprite: render state and the call of RenderModel).
** Opaque models are drawn by RenderOpaque, translucent ones (every render style that is not STYLE_Normal, alpha < 1) by the
** SortedTranslucentRenderer after the OIT composite, like every draw the OIT cannot do (plan D10).
**
**---------------------------------------------------------------------------
**
** Copyright 2005-2016 Christoph Oelckers
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
#include <vector>
#include "tarray.h"
#include "vectors.h"
#include "palentry.h"
#include "renderstyle.h"
#include "modelrenderer.h"
#include "hwrenderer/data/buffers.h"
#include "hwrenderer/data/hw_renderstate.h"
#include "Common/Shared/RenderInfo.h"
#include "OpenGL/Renderers/Legacy/World/Data/SortedTranslucentRenderer.h"
#include "OpenGL/Renderers/Legacy/World/Entities/ModelProgram.h"
#include "OpenGL/Renderers/Legacy/World/Entities/SpriteLight.h"
#include "Util/Geometry.h"

class AActor;
class DPSprite;
class FMaterial;
class FRenderState;
struct FSpriteModelFrame;
struct sector_t;

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Entities
{

class EntityModelRenderer : public FModelRenderer
{
public:
	// The program of a material shader index (FMaterial::GetShaderIndex(), UzdoomContract material shader table);
	// HelionProgramRegistry owns the programs. nullptr: the model is not drawn.
	using ProgramLookup = std::function<ModelProgram*(int materialShaderIndex)>;

	// Everything the weapon renderer decides for a player weapon model (HUDSprite of hw_weapon.cpp).
	struct HudModel
	{
		DPSprite* Weapon = nullptr;				// HUDSprite::weapon
		FSpriteModelFrame* Frame = nullptr;		// HUDSprite::mframe
		FVector3 Translation;					// HUDSprite::translation
		FVector3 Rotation;						// HUDSprite::rotation + (mx / 4, (my - WEAPONTOP) / -4, 0), as DrawPSprite passes it
		FVector3 Pivot;							// HUDSprite::pivot
		FRenderStyle RenderStyle;				// HUDSprite::RenderStyle
		float Alpha = 1.0f;						// HUDSprite::alpha
		int LightLevel = 255;					// HUDSprite::lightlevel
		bool Fullbright = false;				// the light is white and 255 (HUDSprite::SetBright / fullbright scene)
		bool NoColoredLight = false;			// LEVEL3_NOCOLOREDSPRITELIGHTING
		int Desaturation = 0;					// HUDSprite::cm.Desaturation
		int LightBufferIndex = 0;				// the sector buffer slot of the colour and fog of the player's sector
		PalEntry ObjectColor = 0xffffffff;		// HUDSprite::ObjectColor
		PalEntry AddColor = 0;					// sector AdditiveColors | 0xff000000
		bool PerPixelLights = false;			// hw_GetDynModelLight of the player (not MDL_NOPERPIXELLIGHTING)
		float DynLight[3] = { 0, 0, 0 };		// HUDSprite::dynrgb
		AActor* Camera = nullptr;				// playermo, the actor the dynamic lights are gathered for
		double TicFrac = 0;						// Net_ModifyObjectFrac(weapon, TicFrac)
	};

	explicit EntityModelRenderer(ProgramLookup programLookup);
	~EntityModelRenderer() override;

	EntityModelRenderer(const EntityModelRenderer&) = delete;
	EntityModelRenderer& operator=(const EntityModelRenderer&) = delete;

	// ---- wiring (once) ----
	void SetSortedRenderer(Data::SortedTranslucentRenderer* sortedRenderer) { m_sortedRenderer = sortedRenderer; }

	// True while the view is a mirror (flips the cull face of the models, RF2_INVISIBLEINMIRRORS is the EntityRenderer's).
	bool InMirror = false;

	// ---- per tick gather ----
	// Drops the model actors of the previous tick. Call at the start of the gather like EntityRenderer::Clear.
	void Clear();

	// Accepts an actor that has a model frame (FindModelFrame, r_drawvoxels) and is not drawn with a picnum override for the
	// tick. Returns false (nothing done) when the actor has no model. The visibility checks of HWSprite::Process (view actor,
	// distance, mirrors, ...) are the caller's. viewPosition, renderInfo and renderIndex are not used (the draws are built per frame).
	bool TryRenderActor(AActor* actor, const ::Helion::Geometry::Vec2D& viewPosition, Shared::RenderInfo& renderInfo, int renderIndex);

	// ---- per frame ----
	// HWSprite::Process + PutSprite for every accepted actor at the frame's interpolation: rebuilds the opaque list and queues
	// the translucent models into the sorted renderer. Call after SortedTranslucentRenderer::Clear() of the frame. renderInfo
	// has to stay valid and constant until RenderOpaque / the sorted renderer ran.
	void QueueFrame(Shared::RenderInfo& renderInfo);

	// ---- passes ----
	bool HasOpaqueModels() const { return !m_opaque.empty(); }
	// Draws the opaque models of the frame (GLDL_MODELS). The scene render target, depth test and depth write are the caller's.
	void RenderOpaque(Shared::RenderInfo& renderInfo);

	// RenderHUDModel of hw_weapon.cpp DrawPSprite: draws one weapon model right away over the world (the weapon renderer
	// clears the depth buffer before the first one). Alpha / style / lighting come from the HudModel.
	void RenderHUDModel(Shared::RenderInfo& renderInfo, const HudModel& hud);

	// ---- FModelRenderer (FHWModelRenderer) ----
	ModelRendererType GetType() const override { return GLModelRendererType; }
	void BeginDrawModel(FRenderStyle style, int smf_flags, const VSMatrix& objectToWorldMatrix, bool mirrored) override;
	void EndDrawModel(FRenderStyle style, int smf_flags) override;
	IModelVertexBuffer* CreateVertexBuffer(bool needindex, bool singleframe) override;
	VSMatrix GetViewToWorldMatrix() override;
	void BeginDrawHUDModel(FRenderStyle style, const VSMatrix& objectToWorldMatrix, bool mirrored, int smf_flags) override;
	void EndDrawHUDModel(FRenderStyle style, int smf_flags) override;
	void SetInterpolation(double interpolation) override;
	void SetMaterial(FGameTexture* skin, bool clampNoFilter, FTranslationID translation, AActor* act) override;
	void DrawArrays(int start, int count) override;
	void DrawElements(int numIndices, size_t offset) override;
	void SetupFrame(FModel* model, unsigned int frame1, unsigned int frame2, unsigned int size, int boneStartIndex) override;

private:
	// A model of the frame: the decisions of HWSprite::Process for the model (gather) and the data DrawSprite needs (draw).
	struct ModelDraw
	{
		AActor* Actor = nullptr;				// the actor (hud: the weapon owner camera)
		FSpriteModelFrame* Frame = nullptr;
		float X = 0, Y = 0, Z = 0;				// HWSprite::x, y, z
		double TicFrac = 0;						// Net_ModifyObjectFrac(actor, TicFrac)
		bool Hud = false;
		HudModel HudData;						// valid when Hud

		FRenderStyle RenderStyle;
		float Alpha = 1.0f;						// HWSprite::trans
		bool Translucent = false;
		bool NoAlphaTest = false;				// hw_styleflags == STYLEHW_NoAlphaTest
		bool Brightmap = true;
		int Options = 0;						// ModelOptions
		int LightLevel = 255;
		int LightBufferIndex = 0;
		int Desaturation = 0;
		PalEntry ObjectColor = 0xffffffff;
		PalEntry AddColor = 0;
		bool PerPixelLights = false;
		float DynLight[3] = { 0, 0, 0 };
		bool HasDynLight = false;
	};

	ProgramLookup m_programLookup;
	Data::SortedTranslucentRenderer* m_sortedRenderer = nullptr;
	std::vector<AActor*> m_actors;			// the model actors of the tick
	std::vector<ModelDraw> m_opaque;

	bool m_fullbrightValid = false;
	FullbrightState m_fullbrightState;

	// ---- the model being drawn ----
	Shared::RenderInfo* m_renderInfo = nullptr;
	FRenderState* m_state = nullptr;
	StreamData m_stream;

	static FSpriteModelFrame* FindActorModelFrame(AActor* thing);
	void ProcessActor(AActor* thing, Shared::RenderInfo& renderInfo);
	bool m_oldDepthClamp = false;
	const ModelDraw* m_draw = nullptr;
	ModelProgram* m_program = nullptr;
	FMaterial* m_material = nullptr;
	int m_translation = 0;
	int m_clampMode = 0;
	double m_interpolation = 0;
	VSMatrix m_modelMatrix;
	VSMatrix m_normalModelMatrix;
	bool m_mirrored = false;
	// SetupFrame
	IVertexBuffer* m_vertexBuffer = nullptr;
	IIndexBuffer* m_indexBuffer = nullptr;
	unsigned int m_frame1 = 0, m_frame2 = 0;
	int m_boneStartIndex = -1;
	// screen->mLights index of the draw's light list (ModelLightList), -1 = no dynamic lights.
	int m_lightIndex = -1;

	void DrawModel(Shared::RenderInfo& renderInfo, const ModelDraw& draw);
	void BindProgram(ModelProgram* program);
	void ApplyDrawUniforms();
	bool PrepareDraw();
	void FinishDraw();
	static float AlphaThreshold(const ModelDraw& draw);
};

}
