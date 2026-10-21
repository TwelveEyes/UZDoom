/*
** Entrypoint.cpp
**
** Entry point of the Helion-style renderer (UZDoom-only, plan M2.4).
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** Renders every frame (plan M2.4, M8): RenderView draws the canvases, the camera textures and the main view; the eye loop
** of Helion_RenderViewpoint (from RenderViewpoint of the deleted hw_entrypoint.cpp) does viewport rects, scene render
** target, view setup, scene, HUD, PostProcessScene with the 2D callback. The scene itself is Renderer::Render of a
** RenderCommands list (Clear, Viewport, World).
**
** Multisampling: the render program API cannot share a multisampled scene depth buffer with the OIT / plane clip
** targets (backend limitation), so the scene is not multisampled: HW_GetMultisample() (hw_cvars.h) is 0 in the render
** buffers of both backends and the post processing, and gl_multisample itself (an archived CVar) is not touched.
** SSAO needs the G-buffer pass the Helion programs do not write: it is off.
*/

#include <memory>
#include "Entrypoint.h"
#include "Renderer.h"
#include "Common/Commands/RenderCommands.h"
#include "HelionCVars.h"
#include "OpenGL/Shader/ProgramRegistry.h"
#include "OpenGL/Renderers/Legacy/World/Sky/LegacySkyRenderer.h"
#include "OpenGL/Renderers/Legacy/World/Shader/ShaderVars.h"
#include "OpenGL/Renderers/Legacy/World/Lights/ShadowMesh.h"
#include "World/SectorUpdates.h"

#include "g_levellocals.h"
#include "d_player.h"
#include "r_utility.h"
#include "v_video.h"
#include "hw_cvars.h"
#include "hw_vrmodes.h"
#include "hwrenderer/data/hw_renderstate.h"
#include "gi.h"
#include "texturemanager.h"
#include "a_dynlight.h"
#include "hw_shadowmap.h"
#include "hw_clock.h"
#include "hw_lightbuffer.h"
#include "hw_bonebuffer.h"
#include "hwrenderer/data/hw_viewpointbuffer.h"
#include "hwrenderer/postprocessing/hw_postprocess.h"
#include "flatvertices.h"
#include "m_png.h"
#include "i_time.h"
#include "v_palette.h"
#include "v_draw.h"
#include "g_cvars.h"
#include "doomstat.h"
#include "d_main.h"
#include "files.h"

EXTERN_CVAR(Bool, cl_capfps)
extern bool NoInterpolateView;

EXTERN_CVAR(Int, r_distance_cull_type)
EXTERN_CVAR(Bool, r_ab_fullbright)

namespace
{

std::unique_ptr<Helion::Render::Renderer> s_renderer;
std::unique_ptr<Helion::Render::OpenGL::Commands::RenderCommands> s_commands;
Helion::Render::OpenGL::Shared::OldCamera s_camera;
// TexMan.FlushCount() when s_renderer was created: the texture arrays are private copies of the images, so a flush
// (gl_texture_hqresize*, hires, flushtextures) rebuilds the renderer.
unsigned s_textureFlushCount = 0;

// classic SetFullbrightFlags: the colormap of the view and the fullbright state of the viewer.
int GetViewColormap(player_t* player, FRenderViewpoint& vp, Helion::Render::WorldView& view)
{
	if (r_ab_fullbright)
	{
		view.DrawFullBright = true;
		return CM_DEFAULT;
	}

	player_t* cplayer = player ? player->camera->player : nullptr;
	if (cplayer == nullptr)
		return CM_DEFAULT;

	int cm = CM_DEFAULT;
	if (cplayer->extralight == INT_MIN)
	{
		cm = static_cast<int>(CM_FIRSTSPECIALCOLORMAP) + static_cast<int>(REALINVERSECOLORMAP);
		vp.extralight = 0;
		view.DrawFullBright = true;
		view.DrawInvulnerableColorMap = true;
	}
	else if (cplayer->fixedcolormap == INVERSECOLORMAP && gl_altinvulnoverlay)
	{
		// Helion Player.DrawInvulnerableColorMap/DrawFullBright: the alternative overlay replaces the invulnerability
		// colormap and its fullbright; V_CalcBlend draws the gl_altinvulncolor tint instead.
		return CM_DEFAULT;
	}
	else if (cplayer->fixedcolormap != NOFIXEDCOLORMAP)
	{
		cm = CM_FIRSTSPECIALCOLORMAP + cplayer->fixedcolormap;
		view.DrawFullBright = true;
		// The invulnerability powerup's colormap (Powerup.Color InverseMap) is Helion's invulnerability colormap.
		if (cplayer->fixedcolormap == INVERSECOLORMAP)
			view.DrawInvulnerableColorMap = true;
		else
			view.HasLightAmp = true;
	}
	else if (cplayer->fixedlightlevel != -1 || cplayer->bForceFullbright)
	{
		view.DrawFullBright = cplayer->GetFullbrightMode() != FBMODE_NONE;
	}
	return cm;
}

// The acceleration structure the ray traced light shadows of the Vulkan backend (vk_raytrace) trace against (plan M6.3).
// The mesh holds the plane heights of the time it was built: a finished sector move (door, lift, crusher) marks it stale
// and the next view builds it again (VkRaytrace builds the acceleration structures again for the new mesh).
std::unique_ptr<Helion::Render::OpenGL::Renderers::Legacy::World::Lights::ShadowMesh> s_shadowMesh;
FLevelLocals* s_shadowMeshLevel = nullptr;
bool s_shadowMeshStale = false;
int s_shadowMeshMoveToken = 0;

void ReleaseShadowMesh()
{
	if (s_shadowMesh && screen)
		screen->SetLevelMesh(nullptr);
	s_shadowMesh.reset();
	s_shadowMeshLevel = nullptr;
	s_shadowMeshStale = false;
	if (s_shadowMeshMoveToken != 0)
		Helion::World::SectorUpdates::SectorMoveCompleteEvent.Unsubscribe(s_shadowMeshMoveToken);
	s_shadowMeshMoveToken = 0;
}

// hw_entrypoint.cpp CollectLights: the shadow map rows of the lights (1024 at most), in the order of the level's light list.
void CollectShadowmapLights(FLevelLocals* level)
{
	IShadowMap* sm = &screen->mShadowMap;
	int lightindex = 0;

	// Todo: this should go through the blockmap in a spiral pattern around the player so that closer lights are preferred.
	for (auto light = level->lights; light; light = light->next)
	{
		IShadowMap::LightsProcessed++;
		if (light->IsShadowMapped() && light->IsActive() && lightindex < 1024)
		{
			IShadowMap::LightsShadowmapped++;

			light->mShadowmapIndex = lightindex;
			sm->SetLight(lightindex, (float)light->X(), (float)light->Y(), (float)light->Z(), light->GetRadius());
			lightindex++;
		}
		else
		{
			light->mShadowmapIndex = 1024;
		}
	}

	for (; lightindex < 1024; lightindex++)
	{
		sm->SetLight(lightindex, 0, 0, 0, 0);
	}
}

// RenderViewpoint (hw_entrypoint.cpp): the light shadows and the attenuation of the light defaults of the level.
void SetupLightShadows(FLevelLocals* level, bool mainview, bool toscreen)
{
	if (mainview && toscreen && !(level->flags3 & LEVEL3_NOSHADOWMAP) && level->HasDynamicLights && gl_light_shadowmap && screen->allowSSBO() && (screen->hwcaps & RFL_SHADER_STORAGE_BUFFER))
	{
		screen->SetAABBTree(level->aabbTree);
		screen->mShadowMap.SetCollectLights([=] {
			CollectShadowmapLights(level);
		});
		screen->UpdateShadowMap();
	}
	else
	{
		// null all references to the level if we do not need a shadowmap. This will shortcut all internal calculations without further checks.
		screen->SetAABBTree(nullptr);
		screen->mShadowMap.SetCollectLights(nullptr);
	}

	// Update the attenuation flag of all light defaults for each viewpoint.
	// This function will only do something if the setting differs.
	FLightDefaults::SetAttenuationForLevel(!!(level->flags3 & LEVEL3_ATTENUATE));

	if (screen->RaytracingEnabled())
	{
		if (s_shadowMeshLevel != level || s_shadowMeshStale)
		{
			ReleaseShadowMesh();
			s_shadowMesh = Helion::Render::OpenGL::Renderers::Legacy::World::Lights::ShadowMesh::Build(*level);
			s_shadowMeshLevel = level;
			s_shadowMeshMoveToken = Helion::World::SectorUpdates::SectorMoveCompleteEvent.Subscribe([](sector_t*, int) { s_shadowMeshStale = true; });
		}
		screen->SetLevelMesh(s_shadowMesh.get());
	}
	else
	{
		ReleaseShadowMesh();
		screen->SetLevelMesh(nullptr);
	}
}

Helion::Render::Renderer& GetRenderer()
{
	// The programs are compiled at startup (CompileNextShader) from the CVars of that moment: a change before the first frame
	// (the -exec / +set commands run after the precompile) makes them stale as much as a change between two frames does.
	bool shaderVarsChanged = Helion::Render::OpenGL::Renderers::Legacy::World::Shader::ShaderVars::Changed();
	if (s_textureFlushCount != TexMan.FlushCount() || shaderVarsChanged)
	{
		s_commands.reset();
		s_renderer.reset();
		if (shaderVarsChanged)
		{
			// The programs are generated from ShaderVars: rebuild all of them (CompileNext re-runs ShaderVars::Initialize).
			Helion::Render::OpenGL::Renderers::Legacy::World::Sky::LegacySkyRenderer::ReleaseSharedPrograms();
			Helion::Render::OpenGL::Shader::HelionProgramRegistry::Reset();
			while (!Helion::Render::OpenGL::Shader::HelionProgramRegistry::CompileNext()) {}
		}
	}
	if (!s_renderer)
	{
		s_renderer = std::make_unique<Helion::Render::Renderer>();
		s_commands = std::make_unique<Helion::Render::OpenGL::Commands::RenderCommands>(Helion::Geometry::Dimension(1, 1), Helion::Geometry::Dimension(1, 1));
		s_textureFlushCount = TexMan.FlushCount();
	}
	return *s_renderer;
}

}

void Helion_ShutdownRenderer()
{
	ReleaseShadowMesh();
	s_commands.reset();
	s_renderer.reset();
	Helion::Render::OpenGL::Renderers::Legacy::World::Sky::LegacySkyRenderer::ReleaseSharedPrograms();
	Helion::Render::OpenGL::Shader::HelionProgramRegistry::Reset();
}

void Helion_LevelDestroyed(FLevelLocals* level)
{
	if (s_shadowMeshLevel == level)
		ReleaseShadowMesh();
	if (s_renderer)
		s_renderer->ReleaseWorld(level);
}

sector_t* Helion_RenderViewpoint(FRenderViewpoint& mainvp, AActor* camera, IntRect* bounds, float fov, float ratio, float fovratio, bool mainview, bool toscreen)
{
	using namespace Helion;
	auto& RenderState = *screen->RenderState();
	// Camera textures (mainview == false) are called inside DFrameBuffer::RenderTextureView: the view is drawn into the scene
	// buffers like the main view (the Helion framebuffers are views of the scene target) and then copied into the texture.

	Render::Renderer& renderer = GetRenderer();
	FLevelLocals* level = mainvp.ViewLevel;
	SetupLightShadows(camera->Level, mainview, toscreen);

	auto vrmode = VRMode::GetVRMode(mainview && toscreen);
	const int eyeCount = vrmode->mEyeCount;
	screen->FirstEye();
	for (int eye_ix = 0; eye_ix < eyeCount; ++eye_ix)
	{
		const auto& eye = vrmode->mEyes[eye_ix];
		screen->SetViewportRects(bounds);

		// Bind the scene frame buffer
		screen->SetSceneRenderTarget(false);
		RenderState.SetPassType(NORMAL_PASS);
		RenderState.EnableDrawBuffers(RenderState.GetPassDrawBufferCount(), true);

		FRenderViewpoint vp = mainvp;
		vp.FieldOfView = DAngle::fromDeg(fov);	// Set the real FOV for the current scene (it's not necessarily the same as the global setting in r_viewpoint)

		Render::WorldView view;
		view.Viewpoint = &vp;
		int cm = GetViewColormap(mainview ? vp.camera->player : nullptr, vp, view);
		// Palette mode and gl_emulateinvulncolormap: the world programs already apply the invulnerability colormap
		// (colormap 32 / the emulated gray ramp), the post-process special colormap would invert it a second time.
		if (view.DrawInvulnerableColorMap && (Render::OpenGL::Renderers::Legacy::World::Shader::ShaderVars::PaletteColorMode ||
			Render::OpenGL::Renderers::Legacy::World::Shader::ShaderVars::EmulateInvulnerabilityColorMap))
			cm = CM_DEFAULT;
		float flash = 1.f;
		RenderState.SetSpecialColormap(cm, flash);

		// Stereo mode specific perspective projection
		float inv_iso_dist = 1.0f;
		bool iso_ortho = (camera->ViewPos != NULL) && (camera->ViewPos->Flags & VPSF_ORTHOGRAPHIC);
		if (iso_ortho && (camera->ViewPos->Offset.Length() > 0)) inv_iso_dist = 1.0 / camera->ViewPos->Offset.Length();
		view.FieldOfView = fov;
		view.AspectRatio = ratio;
		view.FovRatio = fovratio * inv_iso_dist;
		view.HasProjectionOverride = true;
		view.ProjectionOverride = eye.GetProjection(fov, ratio, fovratio * inv_iso_dist, iso_ortho);
		view.ExtraLight = vp.extralight;
		view.ShowViewer = vp.showviewer;
		view.CullDistance = r_distance_cull_type > 0 ? vp.culldist : 0;
		view.MainView = mainview;

		// Stereo mode specific viewpoint adjustment
		vp.Pos += eye.GetViewShift(vp.HWAngles.Yaw.Degrees());

		const IntRect& sceneBounds = screen->mSceneViewport;
		Render::OpenGL::Commands::RenderCommands& commands = *s_commands;
		commands.UpdateRenderDimension(Geometry::Dimension(sceneBounds.width, sceneBounds.height), Geometry::Dimension(screen->GetWidth(), screen->GetHeight()));
		commands.Begin();
		// Set3DViewport: clear everything with the scissor disabled, then restrict to the scene viewport.
		commands.Clear(Geometry::Color(0, 0, 0), true, true);
		commands.Viewport(Geometry::Dimension(sceneBounds.width, sceneBounds.height), Geometry::Vec2I(sceneBounds.left, sceneBounds.top));

		s_camera = Render::OpenGL::Shared::OldCamera::FromViewpoint(vp);
		commands.DrawWorld(level, &s_camera, level->maptime, (float)vp.TicFrac, vp.camera, false, Geometry::Vec2I(0, 0), 1.0);

		renderer.SetWorldView(view);
		renderer.Render(commands);

		if (mainview)
		{
			if (toscreen)
			{
				// classic EndDrawScene: the HUD models are drawn over the scene (the weapon renderer clears the depth
				// buffer before the first one), then the standard rendering state is restored.
				RenderState.EnableFog(false);
				renderer.RenderPlayerSprites(true);
				RenderState.EnableStencil(false);
				RenderState.SetViewport(screen->mScreenViewport.left, screen->mScreenViewport.top, screen->mScreenViewport.width, screen->mScreenViewport.height);
				RenderState.SetRenderStyle(STYLE_Translucent);
				RenderState.ResetColor();
				RenderState.EnableTexture(true);
				RenderState.SetScissor(0, 0, -1, -1);
			}

			if (RenderState.GetPassType() == GBUFFER_PASS) // Turn off ssao draw buffers
			{
				RenderState.SetPassType(NORMAL_PASS);
				RenderState.EnableDrawBuffers(1);
			}

			screen->PostProcessScene(cm, flash, [&]() {
				// classic DrawEndScene2D: the 2D player sprites with the HUD projection (the weapon renderer's uniforms), then
				// restore standard rendering state for the 2D drawing. A view that does not go to the screen (save picture) has
				// no player sprites (classic CreateScene prepares them for the DM_MAINVIEW draw mode only).
				RenderState.EnableDepthTest(false);
				RenderState.EnableMultisampling(false);
				if (toscreen)
					renderer.RenderPlayerSprites(false);
				RenderState.SetNoSoftLightLevel();
				RenderState.SetRenderStyle(STYLE_Translucent);
				RenderState.ResetColor();
				RenderState.EnableTexture(true);
				RenderState.SetScissor(0, 0, -1, -1);
			});
		}
		else if (bounds != nullptr)
		{
			// classic EndDrawInfo of a camera texture: restore the standard state, then hand the view to the texture.
			RenderState.EnableStencil(false);
			RenderState.SetScissor(0, 0, -1, -1);
			screen->BlitSceneToTextureView(*bounds);
		}
		// Reset colormap so 2D drawing isn't affected
		RenderState.SetSpecialColormap(CM_DEFAULT, 1);

		if (eyeCount - eye_ix > 1)
			screen->NextEye(eyeCount);
	}

	return mainvp.sector;
}

namespace
{

// R_SetupFrame, then the Helion view (RenderViewpoint of the deleted hw_entrypoint.cpp).
sector_t* RenderViewpoint(FRenderViewpoint& mainvp, AActor* camera, IntRect* bounds, float fov, float ratio, float fovratio, bool mainview, bool toscreen)
{
	R_SetupFrame(mainvp, r_viewwindow, camera);
	return Helion_RenderViewpoint(mainvp, camera, bounds, fov, ratio, fovratio, mainview, toscreen);
}

void DoWriteSavePic(FileWriter* file, ESSType ssformat, uint8_t* scr, int width, int height, sector_t* viewsector, bool upsidedown)
{
	PalEntry palette[256];
	PalEntry modulateColor;
	auto blend = V_CalcBlend(viewsector, &modulateColor);
	int pixelsize = 1;
	// Apply the screen blend, because the renderer does not provide this.
	if (ssformat == SS_RGB)
	{
		int numbytes = width * height * 3;
		pixelsize = 3;
		if (modulateColor != 0xffffffff)
		{
			float r = modulateColor.r / 255.f;
			float g = modulateColor.g / 255.f;
			float b = modulateColor.b / 255.f;
			for (int i = 0; i < numbytes; i += 3)
			{
				scr[i] = uint8_t(scr[i] * r);
				scr[i + 1] = uint8_t(scr[i + 1] * g);
				scr[i + 2] = uint8_t(scr[i + 2] * b);
			}
		}
		float iblendfac = 1.f - blend.W;
		blend.X *= blend.W;
		blend.Y *= blend.W;
		blend.Z *= blend.W;
		for (int i = 0; i < numbytes; i += 3)
		{
			scr[i] = uint8_t(scr[i] * iblendfac + blend.X);
			scr[i + 1] = uint8_t(scr[i + 1] * iblendfac + blend.Y);
			scr[i + 2] = uint8_t(scr[i + 2] * iblendfac + blend.Z);
		}
	}
	else
	{
		// Apply the screen blend to the palette. The colormap related parts get skipped here because these are already part of the image.
		DoBlending(GPalette.BaseColors, palette, 256, uint8_t(blend.X), uint8_t(blend.Y), uint8_t(blend.Z), uint8_t(blend.W * 255));
	}

	int pitch = width * pixelsize;
	if (upsidedown)
	{
		scr += ((height - 1) * width * pixelsize);
		pitch *= -1;
	}

	M_CreatePNG(file, scr, ssformat == SS_PAL ? palette : nullptr, ssformat, width, height, pitch, vid_gamma);
}

void CheckTimer(FRenderState& state, uint64_t ShaderStartTime)
{
	// if firstFrame is not yet initialized, initialize it to current time
	// if we're going to overflow a float (after ~4.6 hours, or 24 bits), re-init to regain precision
	if ((state.firstFrame == 0) || (screen->FrameTime - state.firstFrame >= 1 << 24) || ShaderStartTime >= state.firstFrame)
		state.firstFrame = screen->FrameTime - 1;
}

}

//===========================================================================
//
// Render the view to a savegame picture
//
//===========================================================================

void WriteSavePic(player_t* player, FileWriter* file, int width, int height)
{
	IntRect bounds;
	bounds.left = 0;
	bounds.top = 0;
	bounds.width = width;
	bounds.height = height;
	auto& RenderState = *screen->RenderState();

	// we must be sure the GPU finished reading from the buffer before we fill it with new data.
	screen->WaitForCommands(false);

	// Switch to render buffers dimensioned for the savepic
	screen->SetSaveBuffers(true);
	screen->ImageTransitionScene(true);

	hw_postprocess.SetTonemapMode(level.info ? level.info->tonemap : ETonemapMode::None);
	screen->mVertexData->Reset();
	RenderState.SetVertexBuffer(screen->mVertexData);
	screen->mLights->Clear();
	screen->mBones->Clear();
	screen->mViewpoints->Clear();

	// This shouldn't overwrite the global viewpoint even for a short time.
	FRenderViewpoint savevp;
	sector_t* viewsector = RenderViewpoint(savevp, players[consoleplayer].camera, &bounds, r_viewpoint.FieldOfView.Degrees(), 1.6f, 1.6f, true, false);
	RenderState.EnableStencil(false);
	RenderState.SetNoSoftLightLevel();

	TArray<uint8_t> scr(width * height * 3, true);
	screen->CopyScreenToBuffer(width, height, scr.Data());

	DoWriteSavePic(file, SS_RGB, scr.Data(), width, height, viewsector, screen->FlipSavePic());

	// Switch back the screen render buffers
	screen->SetViewportRects(nullptr);
	screen->SetSaveBuffers(false);
}

//===========================================================================
//
// Renders the main view
//
//===========================================================================

sector_t* RenderView(player_t* player)
{
	auto RenderState = screen->RenderState();
	RenderState->SetVertexBuffer(screen->mVertexData);
	screen->mVertexData->Reset();
	hw_postprocess.SetTonemapMode(level.info ? level.info->tonemap : ETonemapMode::None);

	if (level.flags3 & LEVEL3_NOAMBIENTOCCLUSION)
	{
		hw_postprocess.SetNoAmbientOcclusion();
	}

	iter_dlightf = iter_dlight = draw_dlight = draw_dlightf = 0;

	checkBenchActive();

	// reset statistics counters
	ResetProfilingData();

	// Get this before everything else
	if (cl_capfps || r_NoInterpolate) r_viewpoint.TicFrac = 1.;
	else r_viewpoint.TicFrac = I_GetTimeFrac();

	screen->mLights->Clear();
	screen->mBones->Clear();
	screen->mViewpoints->Clear();

	// NoInterpolateView should have no bearing on camera textures, but needs to be preserved for the main view below.
	bool saved_niv = NoInterpolateView;
	NoInterpolateView = false;

	// Shader start time does not need to be handled per level. Just use the one from the camera to render from.
	if (player->camera)
		CheckTimer(*RenderState, player->camera->Level->ShaderStartTime);

	// Draw all canvases that changed
	for (FCanvas* canvas : AllCanvases)
	{
		if (canvas->Tex && canvas->Tex->CheckNeedsUpdate())
		{
			screen->RenderTextureView(canvas->Tex, [=](IntRect& bounds)
				{
					screen->SetViewportRects(&bounds);
					Draw2D(&canvas->Drawer, *screen->RenderState(), 0, 0, canvas->Tex->GetWidth(), canvas->Tex->GetHeight());
					canvas->Drawer.Clear();
				});
			canvas->Tex->SetUpdated(true);
		}
	}

	// prepare all camera textures that have been used in the last frame.
	// This must be done for all levels, not just the primary one!
	for (auto Level : AllLevels())
	{
		Level->canvasTextureInfo.UpdateAll([&](AActor* camera, FCanvasTexture* camtex, double fov)
			{
				screen->RenderTextureView(camtex, [=](IntRect& bounds)
					{
						FRenderViewpoint texvp;
						float ratio = camtex->aspectRatio / Level->info->pixelstretch;
						RenderViewpoint(texvp, camera, &bounds, fov, ratio, ratio, false, false);
					});
			});
	}
	NoInterpolateView = saved_niv;

	// now render the main view
	float fovratio;
	float ratio = r_viewwindow.WidescreenRatio;
	if (r_viewwindow.WidescreenRatio >= 1.3f)
	{
		fovratio = 1.333333f;
	}
	else
	{
		fovratio = ratio;
	}

	screen->ImageTransitionScene(true); // Only relevant for Vulkan.

	sector_t* retsec = RenderViewpoint(r_viewpoint, player->camera, NULL, r_viewpoint.FieldOfView.Degrees(), ratio, fovratio, true, true);
	All.Unclock();
	return retsec;
}
