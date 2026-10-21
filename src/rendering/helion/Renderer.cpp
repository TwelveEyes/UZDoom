/*
** Renderer.cpp
**
** Port of Helion.Render.Renderer (Renderer.cs).
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** See Renderer.h for what is not ported.
*/

#include "Renderer.h"
#include "BindTextures.h"
#include "ColorMapBuffer.h"
#include "HelionCVars.h"
#include "OpenGL/Renderers/Legacy/World/LegacyWorldRenderer.h"
#include "OpenGL/Renderers/Legacy/World/Shader/ShaderVars.h"
#include "OpenGL/Texture/Legacy/LegacyGLTextureManager.h"
#include "World/Geometry/Sectors/TransferHeightsView.h"
#include "Util/Constants.h"
#include "OpenGL/Framebuffer/GLFramebuffer.h"
#include "OpenGL/Renderers/Legacy/World/Data/SectorLightBuffers.h"
#include "OpenGL/Renderers/Legacy/World/Geometry/Portals/FloodFill/FloodFillRenderer.h"
#include "OpenGL/Renderers/Legacy/World/Geometry/DecalRenderer.h"
#include "OpenGL/Renderers/Legacy/World/Geometry/Portals/PortalDriver.h"
#include "OpenGL/Renderers/Legacy/World/Geometry/Portals/PortalClip.h"
#include "OpenGL/Shader/RenderProgram.h"
#include "Common/Shared/OldCamera.h"

#include "g_levellocals.h"
#include "actor.h"
#include "d_player.h"
#include "r_utility.h"
#include "v_video.h"
#include "hwrenderer/data/hw_renderstate.h"
#include "hwrenderer/data/hw_vrmodes.h"
#include "hw_cvars.h"

EXTERN_CVAR(Float, r_visibility)
CVAR(Bool, gl_bandedswlight, false, CVAR_ARCHIVE)

namespace Helion::Render
{

using namespace Helion::Geometry;
using Helion::Render::OpenGL::Renderers::Legacy::World::ShaderUniforms;
using Helion::World::Geometry::Sectors::TransferHeightView;

// Helion Constants.DefaultMaxDistance: the whole map (map coordinates are 16 bit).
static constexpr int DefaultMaxDistance = 65536;

Renderer::Renderer()
{
	m_textureManager = std::make_unique<OpenGL::Texture::Legacy::LegacyGLTextureManager>();
	m_worldRenderer = std::make_unique<OpenGL::Renderers::Legacy::World::LegacyWorldRenderer>(*m_textureManager);
	m_sectorLightBuffers = std::make_unique<SectorLightBuffers>();
	UploadColorMap();

	// The part of the static program setup the flood programs need for the truecolor light stage: the viewpoint block
	// (gl_lightmode, fog mode, uCameraPos) and the glow / level constants buffer.
	auto contractUniforms = [](OpenGL::Renderers::Legacy::World::RenderProgramBase& program, OpenGL::Shared::RenderInfo& renderInfo) {
		if (renderInfo.Viewpoint != nullptr)
			program.ViewpointUniforms(*renderInfo.Viewpoint);
		program.SectorGlowTexture(BindTextures::SectorGlow);
	};
	OpenGL::Renderers::Legacy::World::Geometry::Portals::FloodFill::FloodFillRenderer::ContractUniforms = contractUniforms;
	OpenGL::Renderers::Legacy::World::Geometry::DecalRenderer::ContractUniforms = contractUniforms;

	// UZDoom (plan M5.2): the world pass of a portal view.
	m_worldRenderer->Portals().Hooks.RenderView = [this](const FRenderViewpoint& viewpoint, const OpenGL::Renderers::Legacy::World::Geometry::Portals::PortalClip& clip) {
		RenderPortalView(viewpoint, clip);
	};

	// Renderer.World.cs World_SectorMoveComplete: the final floor heights of a finished move.
	m_tokenMoveComplete = ::Helion::World::SectorUpdates::SectorMoveCompleteEvent.Subscribe([this](sector_t* sector, int) {
		if (m_lineHeightsBuffer != nullptr)
			m_updateLineHeights.Add(sector);
	});
}

Renderer::~Renderer()
{
	if (m_tokenMoveComplete != 0)
		::Helion::World::SectorUpdates::SectorMoveCompleteEvent.Unsubscribe(m_tokenMoveComplete);
	m_worldRenderer.reset();
	m_sectorLightBuffers.reset();
	m_colorMapBuffer.reset();
	m_mapDataBuffer.reset();
	m_lineHeightsBuffer.reset();
	m_worldFramebuffer.reset();
	if (m_textureManager)
		m_textureManager->Dispose();
	m_textureManager.reset();
}

void Renderer::UploadColorMap()
{
	if (!(OpenGL::Renderers::Legacy::World::Shader::ShaderVars::PaletteColorMode || gl_emulateinvulncolormap))
		return;

	std::vector<float> colorMapData = ColorMapBuffer::CreateRgba();
	TArray<float> data;
	data.Resize((unsigned)colorMapData.size());
	memcpy(data.Data(), colorMapData.data(), colorMapData.size() * sizeof(float));
	m_colorMapBuffer = std::make_unique<OpenGL::Textures::GLBufferTextureStorage<float>>("Colormap buffer", std::move(data), OpenGL::Textures::SizedInternalFormat::Rgba32f, false);
	m_colorMapUploaded = true;
}

void Renderer::BindColorMapBuffer()
{
	if (m_colorMapBuffer)
		m_colorMapBuffer->BindTexture(BindTextures::Colormap);
}

void Renderer::SetWorldView(const WorldView& view)
{
	m_view = view;
}

void Renderer::UpdateToNewWorld(FLevelLocals* world)
{
	m_worldRenderer->UpdateToNewWorld(world);
	m_sectorLightBuffers->Init(world, true);
	SetMapDataBuffer(world);
	SetLineHeights(world);
	m_world = world;
	m_lastRenderedWorld = world;
}

void Renderer::ResetInterpolation(FLevelLocals* world)
{
	m_worldRenderer->ResetInterpolation(world);
}

void Renderer::ReleaseWorld(FLevelLocals* world)
{
	if (m_world != world)
		return;
	m_world = nullptr;
	m_lastRenderedWorld = nullptr;
	m_updateLineHeights.ClearAndReset();
	m_mapDataBuffer.reset();
	m_lineHeightsBuffer.reset();
}

void Renderer::UpdateFramebufferIfNeeded(Dimension dimension)
{
	if (!m_worldFramebuffer || m_worldFramebuffer->Dimension != dimension)
	{
		m_worldFramebuffer.reset();
		m_worldFramebuffer = std::make_unique<OpenGL::Framebuffer::GLFramebuffer>("Scene", dimension, 0,
			OpenGL::Framebuffer::GLFrameBufferOptions::DepthStencilAttachment, true);
	}
}

void Renderer::Render(OpenGL::Commands::RenderCommands& renderCommands)
{
	UpdateFramebufferIfNeeded(renderCommands.RenderDimension());
	m_worldFramebuffer->Bind();
	BindColorMapBuffer();
	BindMapDataBuffer();
	BindLineHeightsBuffer();

	using OpenGL::Commands::RenderCommandType;
	for (size_t i = 0; i < renderCommands.Commands.size(); i++)
	{
		const OpenGL::Commands::RenderCommand& cmd = renderCommands.Commands[i];
		switch (cmd.Type)
		{
		case RenderCommandType::Clear:
			HandleClearCommand(renderCommands.ClearCommands[cmd.Index]);
			break;
		case RenderCommandType::World:
			HandleRenderWorldCommand(renderCommands.WorldCommands[cmd.Index], m_viewport);
			break;
		case RenderCommandType::Viewport:
			HandleViewportCommand(renderCommands.ViewportCommands[cmd.Index]);
			break;
		case RenderCommandType::Scissor:
			HandleScissorCommand(renderCommands.ScissorCommands[cmd.Index]);
			break;
		}
	}
}

void Renderer::HandleClearCommand(const OpenGL::Commands::Types::ClearRenderCommand& cmd)
{
	FRenderState& state = *screen->RenderState();
	int targets = 0;
	if (cmd.Color) targets |= CT_Color;
	if (cmd.Depth) targets |= CT_Depth;
	if (cmd.Stencil) targets |= CT_Stencil;

	// Always clear all buffers with scissor test disabled (hw_drawinfo Set3DViewport).
	state.SetScissor(0, 0, -1, -1);
	if (cmd.Color)
	{
		// FRenderState::Clear(CT_Color) clears to black: the colour of the command is drawn with ClearRenderTarget,
		// which clears the colour attachments of the current target to an explicit colour.
		state.ClearRenderTarget(cmd.ClearColor.r / 255.0f, cmd.ClearColor.g / 255.0f, cmd.ClearColor.b / 255.0f, cmd.ClearColor.a / 255.0f);
		targets &= ~CT_Color;
	}
	if (targets != 0)
		state.Clear(targets);
	state.SetScissor(m_viewport.left, m_viewport.top, m_viewport.width, m_viewport.height);
}

void Renderer::HandleViewportCommand(const OpenGL::Commands::Types::ViewportCommand& cmd)
{
	FRenderState& state = *screen->RenderState();
	m_viewport = { cmd.Offset.X, cmd.Offset.Y, cmd.Dimension.Width, cmd.Dimension.Height };
	state.SetViewport(m_viewport.left, m_viewport.top, m_viewport.width, m_viewport.height);
	state.SetScissor(m_viewport.left, m_viewport.top, m_viewport.width, m_viewport.height);
}

void Renderer::HandleScissorCommand(const OpenGL::Commands::Types::ScissorCommand& cmd)
{
	FRenderState& state = *screen->RenderState();
	if (cmd.Enable == OpenGL::Commands::Types::ScissorEnable::Disable)
		state.SetScissor(0, 0, -1, -1);
	else
		state.SetScissor(cmd.Box.Min.X, cmd.Box.Min.Y, cmd.Box.Width(), cmd.Box.Height());
}

TransferHeightView Renderer::GetTransferHeightView(sector_t* viewSector, double viewZ)
{
	// Transfer heights works off the TransferHeights of the sector that the player is viewing from
	// This means that it does the calculations for what sector to rendering based on the TransferHeights of the sector you are viewing from...
	sector_t* control = viewSector->GetHeightSec();
	if (control == nullptr)
		return TransferHeightView::Middle;

	DVector2 pos = r_viewpoint.Pos.XY();
	if (viewZ > control->ceilingplane.ZatPoint(pos) && !(control->MoreFlags & SECMF_FAKEFLOORONLY))
		return TransferHeightView::Top;
	if (viewZ > control->floorplane.ZatPoint(pos))
		return TransferHeightView::Middle;
	return TransferHeightView::Bottom;
}

void Renderer::HandleRenderWorldCommand(const OpenGL::Commands::Types::DrawWorldCommand& cmd, const IntRect& viewport)
{
	if (viewport.width == 0 || viewport.height == 0 || cmd.World == nullptr)
		return;

	if (m_lastRenderedWorld != cmd.World)
		UpdateToNewWorld(cmd.World);

	Vec3F camPos = cmd.Camera->PositionInterpolated;
	sector_t* viewSector = cmd.World->PointInRenderSubsector(DVector2(camPos.X, camPos.Y))->render_sector;
	TransferHeightView transferHeightsView = GetTransferHeightView(viewSector, camPos.Z);

	bool sectorColor = false;
	bool sectorFog = false;
	sectorColor = m_sectorLightBuffers->SectorColor();
	sectorFog = m_sectorLightBuffers->SectorFog();

	m_renderInfo.Set(cmd.Camera, cmd.GametickFraction, viewport, cmd.ViewerEntity, cmd.DrawAutomap,
		cmd.AutomapOffset, cmd.AutomapScale, viewSector, transferHeightsView, true, sectorColor, sectorFog);

	m_renderInfo.FieldOfView = m_view.FieldOfView;
	m_renderInfo.AspectRatio = m_view.AspectRatio;
	m_renderInfo.FovRatio = m_view.FovRatio;
	m_renderInfo.HasProjectionOverride = m_view.HasProjectionOverride;
	m_renderInfo.ProjectionOverride = m_view.ProjectionOverride;
	m_renderInfo.DrawFullBright = m_view.DrawFullBright;
	m_renderInfo.DrawInvulnerableColorMap = m_view.DrawInvulnerableColorMap;
	m_renderInfo.HasLightAmp = m_view.HasLightAmp;
	m_renderInfo.ExtraLight = m_view.ExtraLight;
	m_renderInfo.PixelStretch = cmd.World->pixelstretch;
	m_renderInfo.ShowViewer = m_view.ShowViewer;
	// D27: the radar clipper (fog of war) of an out of bounds view acts on the BSP walk; r_dithertransparency marks the surfaces in the walk;
	// r_distance_cull_type culls lines in the walk (the static cache has no per line culling). Camera textures and save pictures
	// (not the main view) walk the BSP too: the BSP walk of the static mode (BspHeuristics) is the main view's, so a static
	// view of another viewpoint would draw the portals and camera textures of the whole map.
	m_renderInfo.ForceBsp = (m_view.Viewpoint->bDoOob && ((r_radarclipper && !(cmd.World->flags3 & LEVEL3_NOFOGOFWAR)) || r_dithertransparency)) ||
		m_view.CullDistance > 0 || !m_view.MainView;
	m_renderInfo.CullDistance = m_view.CullDistance;
	m_renderInfo.MainView = m_view.MainView;
	m_renderInfo.Viewpoint = &m_viewpointUniforms;
	m_renderInfo.MirrorX = false;
	m_renderInfo.MirrorY = false;

	m_renderInfo.Uniforms = GetShaderUniforms(m_renderInfo);
	SetupViewpointUniforms(m_renderInfo, *m_view.Viewpoint, m_viewpointUniforms, false, false);

	if (m_renderInfo.Uniforms.SectorFog && m_renderInfo.Uniforms.ViewBlendFogSectorIndex != ShaderUniforms::NoViewBlendSectorIndex)
		m_sectorLightBuffers->SetGlobalViewFog(m_renderInfo.Uniforms.ViewBlendFogSectorIndex);

	// UpdateBuffers
	m_sectorLightBuffers->Update();
	UpdateLineHeights();
	m_sectorLightBuffers->BindTextures();
	BindColorMapBuffer();
	BindMapDataBuffer();
	BindLineHeightsBuffer();

	OpenGL::Shader::RenderProgram::SetClipPlane(FVector4(0.f, 0.f, 0.f, 1.f));
	m_worldRenderer->BeginScene(cmd.World);
	m_worldRenderer->Render(cmd.World, m_renderInfo, m_worldFramebuffer.get());
}

// UZDoom (plan M5.2, classic RenderPortal + the SetupView of the portal classes): the world pass from the viewpoint of a
// portal. The view state of the portal view lives on this stack frame; r_viewpoint and the clip plane are the portal's while
// the world renderer draws it and the outer view's again afterwards.
void Renderer::RenderPortalView(const FRenderViewpoint& viewpoint, const OpenGL::Renderers::Legacy::World::Geometry::Portals::PortalClip& clip)
{
	const OpenGL::Shared::RenderInfo& outer = m_worldRenderer->CurrentRenderInfo() != nullptr ? *m_worldRenderer->CurrentRenderInfo() : m_renderInfo;
	FLevelLocals* world = m_world;
	if (world == nullptr)
		return;

	FRenderViewpoint savedViewpoint = r_viewpoint;
	FVector4 savedClipPlane = OpenGL::Shader::RenderProgram::ClipPlane();
	r_viewpoint = viewpoint;

	// The classic plane mirror keeps the pitch and flips the world height axis of the view matrix (SetViewMatrix planemult):
	// with the flip done in clip space, the camera looks the other way vertically.
	OpenGL::Shared::OldCamera camera = OpenGL::Shared::OldCamera::FromViewpoint(viewpoint);
	if (clip.PlaneMirrored)
		camera.Set(camera.PositionInterpolated, camera.Position, camera.YawRadians, -camera.PitchRadians);

	OpenGL::Shared::RenderInfo renderInfo = outer;
	renderInfo.Camera = &camera;
	renderInfo.ViewSector = world->PointInRenderSubsector(viewpoint.Pos.XY())->render_sector;
	renderInfo.TransferHeightView = GetTransferHeightView(renderInfo.ViewSector, viewpoint.Pos.Z);
	renderInfo.ShowViewer = viewpoint.showviewer;
	renderInfo.ForceBsp = false;
	renderInfo.MainView = false;
	renderInfo.MirrorX = false;
	renderInfo.MirrorY = false;
	// classic ProjectionMatrix2: orthographic views draw skybox portals with the perspective projection.
	if (clip.PerspectiveProjection)
		renderInfo.HasProjectionOverride = false;

	HWViewpointUniforms uniforms;
	SetupViewpointUniforms(renderInfo, viewpoint, uniforms, clip.Mirrored, clip.PlaneMirrored);
	clip.ApplyToUniforms(uniforms);
	renderInfo.Viewpoint = &uniforms;
	renderInfo.MirrorX = clip.Mirrored;
	renderInfo.MirrorY = clip.PlaneMirrored;
	renderInfo.Uniforms = GetShaderUniforms(renderInfo);

	OpenGL::Shader::RenderProgram::SetClipPlane(clip.ClipPlane());
	m_worldRenderer->RenderPortalView(world, renderInfo, m_worldFramebuffer.get(), clip);

	OpenGL::Shader::RenderProgram::SetClipPlane(savedClipPlane);
	r_viewpoint = savedViewpoint;
}

void Renderer::RenderPlayerSprites(bool hudModelStep)
{
	if (m_world == nullptr || m_renderInfo.Camera == nullptr)
		return;

	// The post processing between the scene and the 2D step rebinds the texture units.
	m_sectorLightBuffers->BindTextures();
	BindColorMapBuffer();
	BindMapDataBuffer();
	BindLineHeightsBuffer();
	m_worldRenderer->RenderPlayerSprites(m_renderInfo, hudModelStep);
}

void Renderer::SetupViewpointUniforms(const OpenGL::Shared::RenderInfo& renderInfo, const FRenderViewpoint& vp, HWViewpointUniforms& u, bool mirror, bool planemirror)
{
	// The block hw_drawinfo's StartScene fills for the table shaders, in Helion's world coordinates (x, y, z up).
	FLevelLocals* level = m_world;

	// The mirror flip is in the view matrix of this block (classic SetViewMatrix), not in the projection.
	u.ProjectionMatrix = renderInfo.UnmirroredProjectionMatrix();
	// The viewpoint block is in the frame of pixelpos / lights[] (x, height, y), exactly classic SetViewMatrix.
	float stretch = renderInfo.PixelStretch;
	float mult = mirror ? -1.f : 1.f;
	float planemult = planemirror ? -stretch : stretch;
	u.ViewMatrix.loadIdentity();
	u.ViewMatrix.rotate(vp.HWAngles.Roll.Degrees(), 0.0f, 0.0f, 1.0f);
	u.ViewMatrix.rotate(vp.HWAngles.Pitch.Degrees(), 1.0f, 0.0f, 0.0f);
	u.ViewMatrix.rotate(vp.HWAngles.Yaw.Degrees(), 0.0f, mult, 0.0f);
	u.ViewMatrix.translate((float)vp.Pos.X * mult, -(float)vp.Pos.Z * planemult, -(float)vp.Pos.Y);
	u.ViewMatrix.scale(-mult, planemult, 1.0f);
	u.CalcDependencies();
	u.mCameraPos = FVector4((float)vp.Pos.X, (float)vp.Pos.Z, (float)vp.Pos.Y, 0.0f);
	u.mClipLine.X = -10000000.0f;
	u.mClipHeight = 0;
	u.mClipHeightDirection = 0;
	u.mViewHeight = renderInfo.Viewport.height;

	ELightMode lightmode = getRealLightmode(level, true);
	int fogmode = vp.bDoOrtho && (lightmode == ELightMode::ZDoomSoftware) ? 2 : gl_fogmode;
	if (lightmode == ELightMode::Build)
	{
		u.mGlobVis = 1 / 64.f;
		u.mPalLightLevels = 32 | (fogmode << 8) | ((int)lightmode << 16);
	}
	else
	{
		u.mGlobVis = (float)R_GetGlobVis(r_viewwindow, r_visibility) / 32.f;
		u.mPalLightLevels = (int)gl_bandedswlight | (fogmode << 8) | ((int)lightmode << 16);
	}
	u.mShadowmapFilter = gl_shadowmap_filter;
	u.mLightBlendMode = level->info ? (int)level->info->lightblendmode : 0;
	u.mThickFogDistance = level->thickfogdistance;
	u.mThickFogMultiplier = level->thickfogmultiplier;
}

float Renderer::GetTimeFrac(FLevelLocals* world)
{
	if (world == nullptr)
		return 0;

	constexpr int TicksPerFrame = 4;
	constexpr int DifferentFrames = 8;

	return (float)(((world->maptime / TicksPerFrame) % DifferentFrames) + 1);
}

float Renderer::GetFuzzDiv(const IntRect& viewport)
{
	return viewport.height / 480.0f / 2 * (float)gl_fuzzamount;
}

float Renderer::GetDownScaleAmount(const OpenGL::Shared::RenderInfo& renderInfo)
{
	if (renderInfo.Viewport.height <= 480)
		return 1;

	return (float)gl_vanillarender_downscale;
}

ShaderUniforms Renderer::GetShaderUniforms(OpenGL::Shared::RenderInfo& renderInfo)
{
	bool drawInvulnerability = false;
	int extraLightOrColorMapIndex = 0;
	float mix = 0.0f;
	OpenGL::Renderers::Legacy::World::ColorMixUniforms colorMix;
	Graphics::Palettes::PaletteIndex paletteIndex = Graphics::Palettes::PaletteIndex::Normal;
	OpenGL::Renderers::Legacy::World::ColorMapUniforms colorMapUniforms;

	if (renderInfo.ViewerEntity != nullptr && renderInfo.ViewerEntity->player != nullptr)
	{
		extraLightOrColorMapIndex = renderInfo.ExtraLight;

		if (!renderInfo.DrawInvulnerableColorMap && renderInfo.DrawFullBright)
		{
			extraLightOrColorMapIndex = ShaderUniforms::GetFixedColorMapIndex(Util::Constants::ColorMapIndex::FullBright);
			mix = 1.0f;
		}

		if (renderInfo.DrawInvulnerableColorMap)
			drawInvulnerability = true;

		// UZDoom: in truecolor the special colormaps (invulnerability, light amp, fixedcolormap) are the post-processing pass of
		// Helion_RenderViewpoint (PostProcessScene), the world is plain fullbright like classic SetFullbrightFlags.
		// gl_emulateinvulncolormap keeps Helion's emulated invulnerability colormap of the world programs instead.
		if (!OpenGL::Renderers::Legacy::World::Shader::ShaderVars::PaletteColorMode && renderInfo.DrawFullBright &&
			!(drawInvulnerability && OpenGL::Renderers::Legacy::World::Shader::ShaderVars::EmulateInvulnerabilityColorMap))
		{
			drawInvulnerability = false;
			mix = 1.0f;
		}

		if (OpenGL::Renderers::Legacy::World::Shader::ShaderVars::PaletteColorMode)
		{
			// Force to use 1st colormap to match original fixedcolormap behavior
			if (renderInfo.HasLightAmp)
			{
				mix = 0;
				extraLightOrColorMapIndex = ShaderUniforms::GetFixedColorMapIndex(Util::Constants::ColorMapIndex::LightAmp);
			}
		}
	}

	int viewBlendSectorFogIndex = ShaderUniforms::NoViewBlendSectorIndex;

	int maxDistance = (int)renderInfo.CullDistance;
	if (maxDistance <= 0)
		maxDistance = DefaultMaxDistance;

	return ShaderUniforms(
		CalculateMvpMatrix(renderInfo),
		CalculateMvpMatrix(renderInfo, true),
		GetTimeFrac(renderInfo.ViewerEntity != nullptr ? renderInfo.ViewerEntity->Level : nullptr),
		drawInvulnerability,
		mix,
		extraLightOrColorMapIndex,
		GetDistanceOffset(renderInfo),
		colorMix,
		GetFuzzDiv(renderInfo.Viewport),
		colorMapUniforms,
		paletteIndex,
		// UZDoom: truecolor runs the light level through R_DoomLightingEquation (gl_lightmode), palette mode keeps Helion's colormap math.
		OpenGL::Renderers::Legacy::World::Shader::ShaderVars::PaletteColorMode ? Util::Configs::Components::RenderLightMode::Banded : Util::Configs::Components::RenderLightMode::Uzdoom,
		1.0f,
		maxDistance,
		renderInfo.BrightMaps,
		renderInfo.SectorColor,
		renderInfo.SectorFog,
		viewBlendSectorFogIndex,
		GetDownScaleAmount(renderInfo));
}

mat4 Renderer::CalculateMvpMatrix(const OpenGL::Shared::RenderInfo& renderInfo, bool onlyXY)
{
	mat4 view = renderInfo.Camera->CalculateViewMatrix(onlyXY, renderInfo.PixelStretch);
	mat4 mvp = GetProjection(renderInfo);
	mvp.multMatrix(view);
	return mvp;
}

mat4 Renderer::GetProjection(const OpenGL::Shared::RenderInfo& renderInfo)
{
	// UZDoom: the projection of the viewpoint (VREyeInfo::GetProjection), not Helion's FOV conversion.
	return renderInfo.ProjectionMatrix();
}

float Renderer::GetZNear(const OpenGL::Shared::RenderInfo&)
{
	return screen->GetZNear();
}

// Helion shifts the light distance by the zNear it lowers when squeezed; UZDoom's zNear is constant.
float Renderer::GetDistanceOffset(const OpenGL::Shared::RenderInfo&)
{
	return 0;
}

}
