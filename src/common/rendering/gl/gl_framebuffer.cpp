/*
** gl_framebuffer.cpp
**
** Implementation of non-hardware specific parts of the OpenGL frame buffer
**
**---------------------------------------------------------------------------
**
** Copyright 2010-2020 Christoph Oelckers
** Copyright 2017-2025 GZDoom Maintainers and Contributors
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** Code written prior to 2026 is also licensed under:
**
** SPDX-License-Identifier: BSD-3-Clause
**
**---------------------------------------------------------------------------
**
*/

#include "gl_system.h"
#include "v_video.h"
#include "m_png.h"

#include "i_time.h"

#include "gl_interface.h"
#include "gl_framebuffer.h"
#include "gl_renderer.h"
#include "gl_renderbuffers.h"

#include "d_abcapture.h"
#include "gl_samplers.h"
#include "hw_clock.h"
#include "hw_vrmodes.h"
#include "hw_skydome.h"
#include "hw_viewpointbuffer.h"
#include "hw_lightbuffer.h"
#include "hw_bonebuffer.h"
#include "gl_shaderprogram.h"
#include "gl_debug.h"
#include "r_videoscale.h"
#include "gl_buffers.h"
#include "gl_levelmesh.h"
#include "gl_postprocessstate.h"
#include "v_draw.h"
#include "printf.h"
#include "gl_hwtexture.h"

#include "flatvertices.h"
#include "hw_cvars.h"
#include "g_levellocals.h"
#include "texturemanager.h"
#include "hw_portal.h"
#include "r_utility.h"

EXTERN_CVAR (Bool, vid_vsync)
EXTERN_CVAR(Int, gl_tonemap)
EXTERN_CVAR(Bool, cl_capfps)
EXTERN_CVAR(Int, gl_pipeline_depth);
// Ticket 03 (level-mesh): cvars feeding the levelmesh uniforms.
// (gl_mask_threshold and gl_fogmode come from hw_cvars.h.)
EXTERN_CVAR(Color, gl_cullcolor);
EXTERN_CVAR(Int, r_distance_cull_type);
EXTERN_CVAR(Float, r_visibility);
EXTERN_CVAR(Int, r_extralight);
EXTERN_CVAR(Int, gl_weaponlight);
EXTERN_CVAR(Int, r_fakecontrast);

void gl_LoadExtensions();
void gl_PrintStartupLog();

extern bool vid_hdr_active;

namespace OpenGLRenderer
{
	FGLRenderer *GLRenderer;

//==========================================================================
//
//
//
//==========================================================================

OpenGLFrameBuffer::OpenGLFrameBuffer(void *hMonitor, bool fullscreen) :
	Super(hMonitor, fullscreen)
{
	// SetVSync needs to be at the very top to workaround a bug in Nvidia's OpenGL driver.
	// If wglSwapIntervalEXT is called after glBindFramebuffer in a frame the setting is not changed!
	Super::SetVSync(vid_vsync);
	FHardwareTexture::InitGlobalState();

	// Make sure all global variables tracking OpenGL context state are reset..
	gl_RenderState.Reset();

	GLRenderer = nullptr;
}

OpenGLFrameBuffer::~OpenGLFrameBuffer()
{
	PPResource::ResetAll();

	if (mVertexData != nullptr) delete mVertexData;
	if (mSkyData != nullptr) delete mSkyData;
	if (mViewpoints != nullptr) delete mViewpoints;
	if (mLights != nullptr) delete mLights;
	if (mBones != nullptr) delete mBones;
	mShadowMap.Reset();

	if (GLLevelMesh)
	{
		delete GLLevelMesh;
		GLLevelMesh = nullptr;
	}
	if (GLRenderer)
	{
		delete GLRenderer;
		GLRenderer = nullptr;
	}
}

//==========================================================================
//
// Initializes the GL renderer
//
//==========================================================================

void OpenGLFrameBuffer::InitializeState()
{
	static bool first=true;

	if (first)
	{
		if (ogl_LoadFunctions() == ogl_LOAD_FAILED)
		{
			I_FatalError("Failed to load OpenGL functions.");
		}
	}

	gl_LoadExtensions();

	mPipelineNbr = clamp(*gl_pipeline_depth, 1, HW_MAX_PIPELINE_BUFFERS);
	mPipelineType = gl_pipeline_depth > 0;

	// Move some state to the framebuffer object for easier access.
	hwcaps = gl.flags;
	glslversion = gl.glslversion;
	uniformblockalignment = gl.uniformblockalignment;
	maxuniformblock = gl.maxuniformblock;
	vendorstring = gl.vendorstring;

	if (first)
	{
		first=false;
		gl_PrintStartupLog();
	}

	glDepthFunc(GL_LESS);

	glEnable(GL_DITHER);
	glDisable(GL_CULL_FACE);
	glDisable(GL_POLYGON_OFFSET_FILL);
	glEnable(GL_POLYGON_OFFSET_LINE);
	glEnable(GL_BLEND);
	glEnable(GL_DEPTH_CLAMP);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_LINE_SMOOTH);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glClearDepth(1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	SetViewportRects(nullptr);

	mVertexData = new FFlatVertexBuffer(GetWidth(), GetHeight(), screen->mPipelineNbr);
	mSkyData = new FSkyVertexBuffer;
	mViewpoints = new HWViewpointBuffer(screen->mPipelineNbr);
	mLights = new FLightBuffer(screen->mPipelineNbr);
	mBones = new BoneBuffer(screen->mPipelineNbr);
	GLRenderer = new FGLRenderer(this);
	GLRenderer->Initialize(GetWidth(), GetHeight());
	GLLevelMesh = new FGLLevelMesh;
	static_cast<GLDataBuffer*>(mLights->GetBuffer())->BindBase();
	static_cast<GLDataBuffer*>(mBones->GetBuffer())->BindBase();

	mDebug = std::make_unique<FGLDebug>();
	mDebug->Update();
}

//==========================================================================
//
// Updates the screen
//
//==========================================================================

void OpenGLFrameBuffer::Update()
{
	twoD.Reset();
	Flush3D.Reset();

	Flush3D.Clock();
	GLRenderer->Flush();
	Flush3D.Unclock();

	// A/B capture (ticket 01): the frame is composed and the GPU is idle,
	// so the back buffer is valid for readback before Swap() recycles it.
	AbCapture_FrameComposed();

	Swap();
	Super::Update();
}

void OpenGLFrameBuffer::CopyScreenToBuffer(int width, int height, uint8_t* scr)
{
	IntRect bounds;
	bounds.left = 0;
	bounds.top = 0;
	bounds.width = width;
	bounds.height = height;
	GLRenderer->CopyToBackbuffer(&bounds, false);

	// strictly speaking not needed as the glReadPixels should block until the scene is rendered, but this is to safeguard against shitty drivers
	glFinish();
	glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, scr);
}

//===========================================================================
//
// Camera texture rendering
//
//===========================================================================

void OpenGLFrameBuffer::RenderTextureView(FCanvasTexture* tex, std::function<void(IntRect &)> renderFunc)
{
	GLRenderer->StartOffscreen();
	GLRenderer->BindToFrameBuffer(tex);

	IntRect bounds;
	bounds.left = bounds.top = 0;
	bounds.width = FHardwareTexture::GetTexDimension(tex->GetWidth());
	bounds.height = FHardwareTexture::GetTexDimension(tex->GetHeight());

	renderFunc(bounds);
	GLRenderer->EndOffscreen();

	tex->SetUpdated(true);
	static_cast<OpenGLFrameBuffer*>(screen)->camtexcount++;
}

//===========================================================================
//
//
//
//===========================================================================

const char* OpenGLFrameBuffer::DeviceName() const
{
	return gl.modelstring;
}

//==========================================================================
//
// Swap the buffers
//
//==========================================================================

CVAR(Bool, gl_finishbeforeswap, false, CVAR_ARCHIVE|CVAR_GLOBALCONFIG);

void OpenGLFrameBuffer::Swap()
{
	bool swapbefore = gl_finishbeforeswap && camtexcount == 0;
	Finish.Reset();
	Finish.Clock();
	if (gl_pipeline_depth < 1)
	{
		if (swapbefore) glFinish();
		FPSLimit();
		SwapBuffers();
		if (!swapbefore) glFinish();
	}
	else
	{
		mVertexData->DropSync();

		FPSLimit();
		SwapBuffers();

		mVertexData->NextPipelineBuffer();
		mVertexData->WaitSync();
	}
	Finish.Unclock();
	camtexcount = 0;
	FHardwareTexture::UnbindAll();
	gl_RenderState.ClearLastMaterial();
	mDebug->Update();
}

//==========================================================================
//
// Enable/disable vertical sync
//
//==========================================================================

void OpenGLFrameBuffer::SetVSync(bool vsync)
{
	// Switch to the default frame buffer because some drivers associate the vsync state with the bound FB object.
	GLint oldDrawFramebufferBinding = 0, oldReadFramebufferBinding = 0;
	glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &oldDrawFramebufferBinding);
	glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &oldReadFramebufferBinding);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);

	Super::SetVSync(vsync);

	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, oldDrawFramebufferBinding);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, oldReadFramebufferBinding);
}

//===========================================================================
//
//
//===========================================================================

void OpenGLFrameBuffer::SetTextureFilterMode()
{
	if (GLRenderer != nullptr && GLRenderer->mSamplerManager != nullptr) GLRenderer->mSamplerManager->SetTextureFilterMode();
}

IHardwareTexture *OpenGLFrameBuffer::CreateHardwareTexture(int numchannels)
{
	return new FHardwareTexture(numchannels);
}

void OpenGLFrameBuffer::PrecacheMaterial(FMaterial *mat, int translation)
{
	if (mat->Source()->GetUseType() == ETextureType::SWCanvas) return;

	int numLayers = mat->NumLayers();
	MaterialLayerInfo* layer;
	auto base = static_cast<FHardwareTexture*>(mat->GetLayer(0, translation, &layer));

	if (base->BindOrCreate(layer->layerTexture, 0, CLAMP_NONE, translation, layer->scaleFlags))
	{
		for (int i = 1; i < numLayers; i++)
		{
			auto systex = static_cast<FHardwareTexture*>(mat->GetLayer(i, 0, &layer));
			systex->BindOrCreate(layer->layerTexture, i, CLAMP_NONE, 0, layer->scaleFlags);
		}
	}
	// unbind everything.
	FHardwareTexture::UnbindAll();
	gl_RenderState.ClearLastMaterial();
}

void OpenGLFrameBuffer::SetLevelMeshData(levelmesh::FLevelMesh *mesh)
{
	if (GLLevelMesh)
		GLLevelMesh->SetMesh(mesh);
}

void OpenGLFrameBuffer::UploadLevelMeshSlot(FLevelLocals *level, int slot)
{
	// Called from D_Render's frame build right after PackSnapshot. The F-level
	// draw list is already in level->levelMeshFrame; chunk E2 consumes it.
	if (GLLevelMesh != nullptr && GLLevelMesh->IsRecordsReady() &&
	    level != nullptr && level->levelMeshData != nullptr)
	{
		GLLevelMesh->UploadSectorSlot(slot, level->levelMeshData->state.GetSectorSlot(slot));
		GLLevelMesh->UploadLightData(level->levelMeshData->state.GetLightData(),
		    level->levelMeshData->state.LightDataSize());
	}
}

//==========================================================================
//
// Ticket 03 (chunk E2): first GL33 draw of the prebuilt level-mesh.
//
// Walks the per-frame culled sub-range list built by D_Render's frame build
// and draws each (region, texture) sub-range as one indexed draw from the
// region VAO. The material is the baked surface texture; masked sub-ranges
// keep the solid pass's source-over style with the gl_mask_threshold test,
// and truly translucent ones use STYLE_Translucent without a threshold -
// mirroring the classic part 1/part 2 split for masks, and approximating the
// GLDL_TRANSLUCENT treatment without per-segment sorting (a later chunk).
// The record pools stay bound on units 12-14 for the whole pass; their unit
// assignments live in FShader::Load because they are program-local.
//
//==========================================================================

bool OpenGLFrameBuffer::DrawLevelMesh(FRenderState &state, FLevelLocals *level)
{
	levelmesh::FLevelMesh *mesh = level->levelMeshData;
	if (GLLevelMesh == nullptr || !GLLevelMesh->IsReady() || !GLLevelMesh->IsRecordsReady() ||
	    mesh == nullptr)
	{
		return false;   // classic fallback: geometry passes still draw the level
	}

	const levelmesh::FLevelMeshFrame &frame = level->levelMeshFrame;
	if (frame.EntryCount() == 0)
	{
		return true;    // culled out entirely: nothing to draw, no fallback
	}
	// No frame built for this level yet (first frame before the first
	// D_Render build): fall back to the classic passes for that one frame.
	if (frame.currentSlot < 0)
	{
		return false;
	}

	// The levelmesh effect for the whole pass; restored to EFF_NONE at the end
	// of the pass because mSpecialEffect persists across draws.
	state.SetEffect(EFF_LEVELMESH);

	// Record pools: bind once; they are constant for the level frame. The
	// sampler units were fixed at 12-14 in FShader::Load for the active
	// program, so binding the textures to those units here is all that is
	// left. Material layers own units 0-11 and nothing else uses 12-15.
	glActiveTexture(GL_TEXTURE12);
	glBindTexture(GL_TEXTURE_BUFFER, GLLevelMesh->GetSurfaceTexture());
	glActiveTexture(GL_TEXTURE13);
	glBindTexture(GL_TEXTURE_BUFFER, GLLevelMesh->GetSectorStateTexture());
	glActiveTexture(GL_TEXTURE14);
	glBindTexture(GL_TEXTURE_BUFFER, GLLevelMesh->GetLightTexture());
	glActiveTexture(GL_TEXTURE0);   // leave the unit the rest of the pass expects

	// Per-frame levelmesh parameters: constant across every sub-range of this
	// frame; pushed into the shader once by SetLevelMeshParams (the GL render
	// state forwards it on each ApplyShader, cheaply buffered).
	LevelMeshDrawParams p;
	p.sectorStateSlot = frame.currentSlot;
	p.surfaceCount = (int)mesh->SurfaceCount();
	p.sectorCount = (int)mesh->state.sectorCount;
	p.fogDensity = level->fogdensity;
	p.outsideFogDensity = level->outsidefogdensity;
	p.outsideFog = level->outsidefog;
	p.flags3 = level->flags3;
	p.flags2 = level->flags2;
	p.flags = level->flags;
	p.skyfog = level->skyfog;
	p.culldist = level->culldist;
	p.skyPos[0] = (float)level->hw_sky1pos;   // per-frame sky scroll: the VS
	p.skyPos[1] = (float)level->hw_sky2pos;   // adds uLevelSkyPos[kind] to the
	p.skyPos[2] = (float)level->hw_skymistpos;// baked sky UVs, replacing the
	                                          // classic texture-matrix scroll.
	// The real light mode: FLevelLocals stores the user setting, the classic
	// pass resolves it through getRealLightmode (hardware path, for3d=true).
	p.lightMode = (int)getRealLightmode(level, true);
	p.distanceCullType = r_distance_cull_type;
	PalEntry cullcolor = (uint32_t)gl_cullcolor;
	p.cullColor[0] = cullcolor.r * (1.f / 255.f);
	p.cullColor[1] = cullcolor.g * (1.f / 255.f);
	p.cullColor[2] = cullcolor.b * (1.f / 255.f);
	p.cullColor[3] = cullcolor.a * (1.f / 255.f);
	p.fogMode = gl_fogmode;
	p.visibility = r_visibility;
	p.extralight = r_extralight;
	p.weaponLight = (r_viewpoint.extralight > 0) ? (int)(r_viewpoint.extralight * gl_weaponlight) : 0;
	p.fakeContrast = r_fakecontrast;
	p.wallHorizLight = level->WallHorizLight;
	p.wallVertLight = level->WallVertLight;
	p.insybox = portalState.inskybox;
	state.SetLevelMeshParams(p);

	const TArray<levelmesh::LevelMeshDrawEntry> &drawList = frame.drawList;
	for (size_t i = 0; i < drawList.Size(); ++i)
	{
		const levelmesh::LevelMeshDrawEntry &entry = drawList[i];
		if (entry.regionIndex >= GLLevelMesh->RegionCount() ||
		    entry.texRangeIndex >= mesh->texRanges.Size())
		{
			continue;   // stale entry: the frame is rebuilt every frame, skip
		}
		const levelmesh::LevelMeshTexRange &tr = mesh->texRanges[entry.texRangeIndex];
		FGameTexture *tex = TexMan.GetGameTexture(FSetTextureID(tr.textureIndex));
		if (tex == nullptr)
		{
			continue;
		}

		// Classic part 1/part 2 split: masked textures keep the source-over
		// style of the solid pass and only add the alpha threshold test; truly
		// translucent ones get STYLE_Translucent without a threshold (what the
		// classic GLDL_TRANSLUCENT pass does for them, minus sorting). Both
		// write depth like the classic solid walls (depth mask set by
		// RenderScene before this call).
		if (tex->isMasked())
		{
			state.SetRenderStyle(STYLE_Source);
			state.AlphaFunc(Alpha_GEqual, gl_mask_threshold);
		}
		else if (tex->GetTranslucency())
		{
			state.SetRenderStyle(STYLE_Translucent);
			state.AlphaFunc(Alpha_GEqual, 0.f);
		}
		else
		{
			state.SetRenderStyle(STYLE_Source);
			state.AlphaFunc(Alpha_GEqual, 0.f);
		}

		// The baked surface texture; CLAMP_NONE because the sub-range table is
		// static and per-region clamp state would need a separate pass.
		state.SetMaterial(tex, UF_Texture, 0, CLAMP_NONE, NO_TRANSLATION, -1, nullptr);
		state.DrawLevelMesh(GLLevelMesh->GetVAO(entry.regionIndex), tr.iboOffset, tr.iboCount);
	}

	// mSpecialEffect persists across draws until the next SetEffect: classic
	// draw functions follow a set-and-restore discipline (HWWall always leaves
	// EFF_NONE behind) and sprites/models/decals rely on that ambient state.
	state.SetEffect(EFF_NONE);

	return true;
}

IVertexBuffer *OpenGLFrameBuffer::CreateVertexBuffer()
{
	return new GLVertexBuffer;
}

IIndexBuffer *OpenGLFrameBuffer::CreateIndexBuffer()
{
	return new GLIndexBuffer;
}

IDataBuffer *OpenGLFrameBuffer::CreateDataBuffer(int bindingpoint, bool ssbo, bool needsresize)
{
	return new GLDataBuffer(bindingpoint, ssbo);
}

void OpenGLFrameBuffer::BlurScene(float amount)
{
	GLRenderer->BlurScene(amount);
}

void OpenGLFrameBuffer::InitLightmap(int LMTextureSize, int LMTextureCount, TArray<uint16_t>& LMTextureData)
{
	if (LMTextureData.Size() > 0)
	{
		GLint activeTex = 0;
		glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTex);
		glActiveTexture(GL_TEXTURE0 + 17);

		if (GLRenderer->mLightMapID == 0)
			glGenTextures(1, (GLuint*)&GLRenderer->mLightMapID);

		glBindTexture(GL_TEXTURE_2D_ARRAY, GLRenderer->mLightMapID);
		glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGB16F, LMTextureSize, LMTextureSize, LMTextureCount, 0, GL_RGB, GL_HALF_FLOAT, &LMTextureData[0]);
		glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glGenerateMipmap(GL_TEXTURE_2D_ARRAY);

		glActiveTexture(activeTex);

		LMTextureData.Reset(); // We no longer need this, release the memory
	}
}

void OpenGLFrameBuffer::SetViewportRects(IntRect *bounds)
{
	Super::SetViewportRects(bounds);
	if (!bounds)
	{
		auto vrmode = VRMode::GetVRMode(true);
		vrmode->AdjustViewport(this);
	}
}

void OpenGLFrameBuffer::UpdatePalette()
{
	if (GLRenderer)
		GLRenderer->ClearTonemapPalette();
}

FRenderState* OpenGLFrameBuffer::RenderState()
{
	return &gl_RenderState;
}

void OpenGLFrameBuffer::AmbientOccludeScene(float m5)
{
	gl_RenderState.EnableDrawBuffers(1);
	GLRenderer->AmbientOccludeScene(m5);
	glViewport(screen->mSceneViewport.left, mSceneViewport.top, mSceneViewport.width, mSceneViewport.height);
	GLRenderer->mBuffers->BindSceneFB(true);
	gl_RenderState.EnableDrawBuffers(gl_RenderState.GetPassDrawBufferCount());
	gl_RenderState.Apply();
}

void OpenGLFrameBuffer::FirstEye()
{
	GLRenderer->mBuffers->CurrentEye() = 0;  // always begin at zero, in case eye count changed
}

void OpenGLFrameBuffer::NextEye(int eyecount)
{
	GLRenderer->mBuffers->NextEye(eyecount);
}

void OpenGLFrameBuffer::SetSceneRenderTarget(bool useSSAO)
{
	GLRenderer->mBuffers->BindSceneFB(useSSAO);
}

void OpenGLFrameBuffer::UpdateShadowMap()
{
	if (mShadowMap.PerformUpdate())
	{
		FGLDebug::PushGroup("ShadowMap");

		FGLPostProcessState savedState;

		static_cast<GLDataBuffer*>(screen->mShadowMap.mLightList)->BindBase();
		static_cast<GLDataBuffer*>(screen->mShadowMap.mNodesBuffer)->BindBase();
		static_cast<GLDataBuffer*>(screen->mShadowMap.mLinesBuffer)->BindBase();

		GLRenderer->mBuffers->BindShadowMapFB();

		GLRenderer->mShadowMapShader->Bind();
		GLRenderer->mShadowMapShader->Uniforms->ShadowmapQuality = gl_shadowmap_quality;
		GLRenderer->mShadowMapShader->Uniforms->NodesCount = screen->mShadowMap.NodesCount();
		GLRenderer->mShadowMapShader->Uniforms.SetData();
		static_cast<GLDataBuffer*>(GLRenderer->mShadowMapShader->Uniforms.GetBuffer())->BindBase();

		glViewport(0, 0, gl_shadowmap_quality, 1024);
		GLRenderer->RenderScreenQuad();

		const auto& viewport = screen->mScreenViewport;
		glViewport(viewport.left, viewport.top, viewport.width, viewport.height);

		GLRenderer->mBuffers->BindShadowMapTexture(16);
		FGLDebug::PopGroup();
		screen->mShadowMap.FinishUpdate();
	}
}

void OpenGLFrameBuffer::WaitForCommands(bool finish)
{
	glFinish();
}

void OpenGLFrameBuffer::SetSaveBuffers(bool yes)
{
	if (!GLRenderer) return;
	if (yes) GLRenderer->mBuffers = GLRenderer->mSaveBuffers;
	else GLRenderer->mBuffers = GLRenderer->mScreenBuffers;
}

//===========================================================================
//
//
//
//===========================================================================

void OpenGLFrameBuffer::BeginFrame()
{
	SetViewportRects(nullptr);
	mViewpoints->Clear();
	if (GLRenderer != nullptr)
		GLRenderer->BeginFrame();
}

//===========================================================================
//
//	Takes a screenshot
//
//===========================================================================

TArray<uint8_t> OpenGLFrameBuffer::GetScreenshotBuffer(int &pitch, ESSType &color_type, float &gamma)
{
	const auto &viewport = mOutputLetterbox;

	// Grab what is in the back buffer.
	// We cannot rely on SCREENWIDTH/HEIGHT here because the output may have been scaled.
	TArray<uint8_t> pixels;
	pixels.Resize(viewport.width * viewport.height * 3);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glReadPixels(viewport.left, viewport.top, viewport.width, viewport.height, GL_RGB, GL_UNSIGNED_BYTE, &pixels[0]);
	glPixelStorei(GL_PACK_ALIGNMENT, 4);

	// Copy to screenshot buffer:
	int w = SCREENWIDTH;
	int h = SCREENHEIGHT;

	TArray<uint8_t> ScreenshotBuffer(w * h * 3, true);

	float rcpWidth = 1.0f / w;
	float rcpHeight = 1.0f / h;
	for (int y = 0; y < h; y++)
	{
		for (int x = 0; x < w; x++)
		{
			float u = (x + 0.5f) * rcpWidth;
			float v = (y + 0.5f) * rcpHeight;
			int sx = u * viewport.width;
			int sy = v * viewport.height;
			int sindex = (sx + sy * viewport.width) * 3;
			int dindex = (x + (h - y - 1) * w) * 3;
			ScreenshotBuffer[dindex] = pixels[sindex];
			ScreenshotBuffer[dindex + 1] = pixels[sindex + 1];
			ScreenshotBuffer[dindex + 2] = pixels[sindex + 2];
		}
	}

	pitch = w * 3;
	color_type = SS_RGB;

	// Screenshot should not use gamma correction if it was already applied to rendered image
	gamma = 1;
	if (vid_hdr_active && vid_fullscreen)
		gamma *= 2.2f;
	return ScreenshotBuffer;
}

//===========================================================================
//
// 2D drawing
//
//===========================================================================

void OpenGLFrameBuffer::Draw2D()
{
	if (GLRenderer != nullptr)
	{
		GLRenderer->mBuffers->BindCurrentFB();
		::Draw2D(twod, gl_RenderState);
	}
}

void OpenGLFrameBuffer::PostProcessScene(int fixedcm, float flash, const std::function<void()> &afterBloomDrawEndScene2D)
{
	GLRenderer->mBuffers->BlitSceneToTexture(); // Copy the resulting scene to the current post process texture
	GLRenderer->PostProcessScene(fixedcm, flash, afterBloomDrawEndScene2D);
}

bool OpenGLFrameBuffer::CompileNextShader()
{
	return GLRenderer->mShaderManager->CompileNextShader();
}

//==========================================================================
//
// OpenGLFrameBuffer :: WipeStartScreen
//
// Called before the current screen has started rendering. This needs to
// save what was drawn the previous frame so that it can be animated into
// what gets drawn this frame.
//
//==========================================================================

FTexture *OpenGLFrameBuffer::WipeStartScreen()
{
	const auto &viewport = screen->mScreenViewport;

	auto tex = new FWrapperTexture(viewport.width, viewport.height, 1);
	tex->GetSystemTexture()->CreateTexture(nullptr, viewport.width, viewport.height, 0, false, "WipeStartScreen");
	glFinish();
	static_cast<FHardwareTexture*>(tex->GetSystemTexture())->Bind(0, false);

	GLRenderer->mBuffers->BindCurrentFB();
	glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, viewport.left, viewport.top, viewport.width, viewport.height);
	return tex;
}

//==========================================================================
//
// OpenGLFrameBuffer :: WipeEndScreen
//
// The screen we want to animate to has just been drawn.
//
//==========================================================================

FTexture *OpenGLFrameBuffer::WipeEndScreen()
{
	GLRenderer->Flush();
	const auto &viewport = screen->mScreenViewport;
	auto tex = new FWrapperTexture(viewport.width, viewport.height, 1);
	tex->GetSystemTexture()->CreateTexture(NULL, viewport.width, viewport.height, 0, false, "WipeEndScreen");
	glFinish();
	static_cast<FHardwareTexture*>(tex->GetSystemTexture())->Bind(0, false);
	GLRenderer->mBuffers->BindCurrentFB();
	glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, viewport.left, viewport.top, viewport.width, viewport.height);
	return tex;
}

bool OpenGLFrameBuffer::HasNVidiaVRAMExt() { return gl.flags & RFL_NV_MEM; }
bool OpenGLFrameBuffer::HasATIVRAMExt() { return gl.flags & RFL_ATI_MEM; }

}
