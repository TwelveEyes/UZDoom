/*
** Renderer.h
**
** Port of Helion.Render.Renderer (Renderer.cs, Renderer.World.cs).
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** What is not ported (plan D7, 2D drawing stays with F2DDrawer): the window / virtual / screenshot framebuffers
** and their blits, HudRenderer, the automap renderer, the transition renderer, the image / shape / text / hud /
** transition / virtual frame buffer commands (see RenderCommands.h) and SetReverseZ (plan D11). The profiler is
** not ported.
**
** Mapping: IConfig is the CVars (HelionCVars.h, plan D16/D20/D28), IWorld is FLevelLocals*, Entity is AActor*.
** The world framebuffer is the backend scene target (OpenGL/Framebuffer/GLFramebuffer.h).
**
** UZDoom-only: SetWorldView() carries what Helion reads from ConfigRender / the player object / ShaderVars
** (field of view and projection of the viewpoint, fullbright / invulnerability / light amp state of the viewer,
** ShowViewer) from the Entrypoint, and the HWViewpointUniforms block the mod shader contract reads.
*/

#pragma once

#include <memory>
#include "Common/Commands/RenderCommands.h"
#include "Common/Shared/RenderInfo.h"
#include "OpenGL/Renderers/Legacy/World/Geometry/Static/LightBufferType.h"
#include "OpenGL/Textures/GLBufferTextureStorage.h"
#include "Util/Geometry.h"
#include "hwrenderer/data/hw_viewpointuniforms.h"
#include "intrect.h"
#include "World/SectorUpdates.h"

struct FLevelLocals;
struct FRenderViewpoint;
struct sector_t;
struct side_t;
class AActor;

namespace Helion::Render::OpenGL::Texture::Legacy
{
class LegacyGLTextureManager;
}

namespace Helion::Render::OpenGL::Framebuffer
{
class GLFramebuffer;
}

namespace Helion::Render::OpenGL::Renderers::Legacy::World
{
class LegacyWorldRenderer;

namespace Geometry::Portals
{
struct PortalClip;
}

namespace Data
{
class SectorLightBuffers;
}
}

namespace Helion::Render
{

// The state of the viewpoint that is rendered by the next world command (UZDoom-only, see the file comment).
struct WorldView
{
	const FRenderViewpoint* Viewpoint = nullptr;
	// VREyeInfo::GetProjection arguments of the view (the Entrypoint passes the same values hw_entrypoint does).
	float FieldOfView = 90;
	float AspectRatio = 4.0f / 3.0f;
	float FovRatio = 4.0f / 3.0f;
	// Stereo / isometric views: finished projection matrix instead (HasProjectionOverride).
	bool HasProjectionOverride = false;
	Geometry::mat4 ProjectionOverride;
	// Player.DrawFullBright(), DrawInvulnerableColorMap(), HasLightAmp(), GetExtraLightRender().
	bool DrawFullBright = false;
	bool DrawInvulnerableColorMap = false;
	bool HasLightAmp = false;
	int ExtraLight = 0;
	bool ShowViewer = false;
	// 0 = whole map.
	double CullDistance = 0;
	// Helion_RenderViewpoint mainview.
	bool MainView = false;
};

class Renderer
{
public:
	Renderer();
	~Renderer();

	Renderer(const Renderer&) = delete;
	Renderer& operator=(const Renderer&) = delete;

	// Executes the commands (clear, viewport, scissor, world) in order on the scene target.
	void Render(OpenGL::Commands::RenderCommands& renderCommands);

	// UZDoom: the player sprites of the last world command (LegacyWorldRenderer::RenderPlayerSprites); the Entrypoint calls it
	// with hudModelStep true after the scene and with false from the 2D callback of PostProcessScene.
	void RenderPlayerSprites(bool hudModelStep);

	void SetWorldView(const WorldView& view);

	void UpdateToNewWorld(FLevelLocals* world);
	void ResetInterpolation(FLevelLocals* world);
	// Drops the level state (call before the level is destroyed).
	void ReleaseWorld(FLevelLocals* world);

	// Renderer.World.cs
	static int GetLightBufferIndex(const side_t* side, int tier, const sector_t* sector, int& overrideLightIndex);
	static int GetLightBufferIndex(const sector_t* sector, int planePos, OpenGL::Renderers::Legacy::World::Geometry::Static::LightBufferType type, int& overrideLightIndex);
	static int GetLightBufferIndex(const sector_t* sector, OpenGL::Renderers::Legacy::World::Geometry::Static::LightBufferType type);

	// Renderer.cs
	static float GetTimeFrac(FLevelLocals* world);
	static float GetFuzzDiv(const IntRect& viewport);
	static OpenGL::Renderers::Legacy::World::ShaderUniforms GetShaderUniforms(OpenGL::Shared::RenderInfo& renderInfo);
	static Geometry::mat4 CalculateMvpMatrix(const OpenGL::Shared::RenderInfo& renderInfo, bool onlyXY = false);
	static Geometry::mat4 GetProjection(const OpenGL::Shared::RenderInfo& renderInfo);
	static float GetZNear(const OpenGL::Shared::RenderInfo& renderInfo);
	static float GetDistanceOffset(const OpenGL::Shared::RenderInfo& renderInfo);
	static float GetDownScaleAmount(const OpenGL::Shared::RenderInfo& renderInfo);
	static ::Helion::World::Geometry::Sectors::TransferHeightView GetTransferHeightView(sector_t* viewSector, double viewZ);

	// Uploads the colormap buffer (palette mode / invulnerability emulation).
	void UploadColorMap();

	OpenGL::Framebuffer::GLFramebuffer* WorldFramebuffer() { return m_worldFramebuffer.get(); }

private:
	using SectorLightBuffers = OpenGL::Renderers::Legacy::World::Data::SectorLightBuffers;

	// Bound to BindTextures::Colormap.
	std::unique_ptr<OpenGL::Textures::GLBufferTextureStorage<float>> m_colorMapBuffer;
	bool m_colorMapUploaded = false;

	std::unique_ptr<OpenGL::Texture::Legacy::LegacyGLTextureManager> m_textureManager;
	std::unique_ptr<OpenGL::Renderers::Legacy::World::LegacyWorldRenderer> m_worldRenderer;
	std::unique_ptr<OpenGL::Framebuffer::GLFramebuffer> m_worldFramebuffer;
	std::unique_ptr<SectorLightBuffers> m_sectorLightBuffers;
	OpenGL::Shared::RenderInfo m_renderInfo;
	HWViewpointUniforms m_viewpointUniforms;
	WorldView m_view;
	IntRect m_viewport = { 0, 0, 0, 0 };
	FLevelLocals* m_world = nullptr;
	FLevelLocals* m_lastRenderedWorld = nullptr;

	// Renderer.World.cs m_mapDataBuffer / m_lineHeightsBuffer (gl_vanillarender only): per line the end points, and the floor
	// heights (previous and current tick) with the BlockSide flags. The sprite clip emulation of the entity programs reads them
	// (mapDataTexture / lineHeightsTexture) for the line ids of the wall clip buffer.
	std::unique_ptr<OpenGL::Textures::GLBufferTextureStorage<float>> m_mapDataBuffer;
	std::unique_ptr<OpenGL::Textures::GLBufferTextureStorage<float>> m_lineHeightsBuffer;
	::Helion::World::SectorUpdates::SectorUpdateList m_updateLineHeights;
	int m_tokenMoveComplete = 0;

	void UpdateFramebufferIfNeeded(Geometry::Dimension dimension);
	void HandleClearCommand(const OpenGL::Commands::Types::ClearRenderCommand& cmd);
	void HandleRenderWorldCommand(const OpenGL::Commands::Types::DrawWorldCommand& cmd, const IntRect& viewport);
	void HandleViewportCommand(const OpenGL::Commands::Types::ViewportCommand& cmd);
	void HandleScissorCommand(const OpenGL::Commands::Types::ScissorCommand& cmd);
	void SetupViewpointUniforms(const OpenGL::Shared::RenderInfo& renderInfo, const FRenderViewpoint& vp, HWViewpointUniforms& u, bool mirror, bool planemirror);
	// UZDoom (plan M5.2): the RenderView hook of the portal driver.
	void RenderPortalView(const FRenderViewpoint& viewpoint, const OpenGL::Renderers::Legacy::World::Geometry::Portals::PortalClip& clip);
	void BindColorMapBuffer();

	// Renderer.World.cs
	void SetMapDataBuffer(FLevelLocals* world);
	void SetLineHeights(FLevelLocals* world);
	void UpdateLineHeights();
	void SetLineHeightBuffer(float* buffer, line_t* line);
	static bool IsBlocked(line_t* line);
	void BindMapDataBuffer();
	void BindLineHeightsBuffer();
};

}
