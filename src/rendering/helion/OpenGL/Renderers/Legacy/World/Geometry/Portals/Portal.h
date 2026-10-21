/*
** Portal.h
**
** UZDoom-only (plan M5.2). Base class of the portal kinds, the counterpart of HWPortal / FPortalSceneState of
** hw_portal.h: the surfaces the world pass collected for the portal (wall quads of the lines that show it), the stencil
** passes that mask the portal on screen, and the contract of the portal views (DrawContents).
**
**---------------------------------------------------------------------------
**
** Copyright 2004-2018 Christoph Oelckers
** Copyright 2017-2025 GZDoom Maintainers and Contributors
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** Mapping to the classic renderer:
**   HWPortal                 -> Portal            (Portal.h / Portal.cpp)
**   HWScenePortalBase        -> ScenePortal       (ScenePortal.h)
**   HWLinePortal             -> LinePortal
**   HWMirrorPortal           -> MirrorPortal
**   HWLineToLinePortal       -> LineToLinePortal
**   HWSkyboxPortal           -> SkyboxPortal
**   HWSectorStackPortal      -> SectorStackPortal
**   HWPlaneMirrorPortal      -> PlaneMirrorPortal
**   HWHorizonPortal          -> HorizonPortal
**   HWEEHorizonPortal        -> EEHorizonPortal
**   FPortalSceneState        -> PortalSceneState  (+ PortalDriver, which owns the portals of a frame)
**   classic draw info (as seen by a portal) -> PortalFrameView (the view the portal is seen from)
** HWSkyPortal (plain level sky) is not a portal here: LegacySkyRenderer draws the level sky.
**
** The stencil geometry is built in the map coordinates of the Helion pipeline (x, y, height) and drawn with
** PortalStencilProgram and the MVP of the view the portal is seen from.
**
*/

#pragma once

#include <memory>
#include <vector>
#include "fcolormap.h"
#include "r_sections.h"
#include "textureid.h"
#include "vectors.h"
#include "tarray.h"
#include "PortalClip.h"
#include "Util/Geometry.h"

struct FRenderViewpoint;
struct FLevelLocals;
struct FSectorPortal;
struct FSectorPortalGroup;
struct FSection;
struct FLinePortalSpan;
struct secplane_t;
struct sector_t;
struct seg_t;
struct subsector_t;
struct line_t;
struct linebase_t;
struct vertex_t;
class FRenderState;

namespace Helion::Render::OpenGL::Shared
{
class RenderInfo;
}

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Geometry::Portals
{

class PortalDriver;

// The wall quad of a portal line (HWWall of the classic renderer): corners x1,y1 / x2,y2 (the vertices of the drawn
// side) and ztop / zbottom at both ends. For the portals that are bounded by a flat (sector stack, plane mirror) Sub is
// the subsector of the wall.
struct PortalSurface
{
	float X1 = 0, Y1 = 0, X2 = 0, Y2 = 0;
	float ZTop[2] = { 0, 0 };
	float ZBottom[2] = { 0, 0 };
	seg_t* Seg = nullptr;
	subsector_t* Sub = nullptr;
	// HWWall::vertexes: the vertices of the drawn side of the line.
	vertex_t* Vertexes[2] = { nullptr, nullptr };
};

// HWSectorPlane as far as the horizon needs it.
struct HorizonPlane
{
	FTextureID Texture;
	float Texheight = 0;
	float Angle = 0;
	FVector2 Offs = { 0, 0 };
	FVector2 Scale = { 1, 1 };
	// UZDoom: the plane the values come from (the light buffer slot of the horizon draw).
	sector_t* Sector = nullptr;
	int PlaneIndex = 0;

	void GetFromSector(sector_t* sec, int plane);
};

// HWHorizonInfo. LightLevel is the plain sector light (GetFloorLight / GetCeilingLight): the hook that draws the horizon
// applies RescaleLightLevel / the lighting mode itself.
struct HorizonInfo
{
	HorizonPlane Plane;
	int LightLevel = 0;
	FColormap Colormap;
	PalEntry SpecialColor = 0;

	HorizonInfo() { Colormap.Clear(); }

	bool operator==(const HorizonInfo& other) const;
};

// FPortalSceneState. instack (screen->instack) and stencilValue stay on the frame buffer like in the classic renderer.
struct PortalSceneState
{
	int MirrorFlag = 0;
	int PlaneMirrorFlag = 0;
	int RenderDepth = 0;

	int PlaneMirrorMode = 0;
	bool InSkybox = false;
	bool VpIsAllowedOoB = false;

	int SkyboxRecursion = 0;

	bool IsMirrored() const { return !!((MirrorFlag ^ PlaneMirrorFlag) & 1); }
};

// What a portal needs to know about the view it is seen from (the outer draw info of the classic renderer).
struct PortalFrameView
{
	FLevelLocals* Level = nullptr;
	// The viewpoint of the view (copied by the driver at StartFrame, so it stays valid).
	const FRenderViewpoint* Viewpoint = nullptr;
	// Clip of the view; null for the main view.
	const PortalClip* Clip = nullptr;
	// The portal this view is drawn through (classic mCurrentPortal); null for the main view.
	const Portal* CurrentPortal = nullptr;
	// MVP of the view in the world coordinates of the pipeline (RenderInfo::Uniforms.Mvp), used for the stencil geometry.
	::Helion::Geometry::mat4 Mvp;
	// Level->info->pixelstretch.
	float PixelStretch = 1.2f;
	// Culling mode of the view (Cull_CCW, or Cull_CW for a view with reversed winding), restored after the stencil passes.
	int Culling = 1;
	Shared::RenderInfo* RenderInfo = nullptr;
};

class Portal
{
	friend class PortalDriver;

public:
	enum StencilPass
	{
		STP_Stencil,
		STP_DepthClear,
		STP_DepthRestore,
		STP_AllInOne
	};

	// The surfaces that show the portal.
	std::vector<PortalSurface> Lines;
	BoundingRect BoundingBox;
	int PlanesUsed = 0;
	// HWPortal::zshift: plane mirror stencil geometry z-fights with flats even at a distance of 32000 map units unless >= 1.
	float ZShift = 1.0f;

	Portal();
	virtual ~Portal();

	Portal(const Portal&) = delete;
	Portal& operator=(const Portal&) = delete;

	virtual PortalType Type() const = 0;
	virtual int GetMirrorSide() const { return 0; }
	// PClipInFront / PClipInside / PClipBehind: the BSP walk of the portal view calls these through PortalClip::ClipPortal.
	virtual int ClipSeg(seg_t* seg, const DVector3& viewpos) const { return PClipInside; }
	virtual int ClipSubsector(subsector_t* sub) const { return PClipInside; }
	virtual int ClipPoint(const DVector2& pos) const { return PClipInside; }
	// HWPortal::GetSource(): identity of the portal (what FindPortal compares).
	// HWPortal::ClipLine(): the line the portal view is clipped against (line portals), else null.
	virtual const linebase_t* ClipLine() const { return nullptr; }
	virtual const void* GetSource() const = 0;
	virtual const char* GetName() const = 0;
	virtual bool AllowSSAO() const { return true; }
	virtual bool IsSky() const { return false; }
	virtual bool NeedCap() const { return true; }
	virtual bool NeedDepthBuffer() const { return true; }

	// Renders the content of the portal into the stencil area: scene portals set the view up and call the world renderer
	// through the driver hooks, horizons draw their plane. vp is a copy of the viewpoint of the outer view, clip a fresh
	// clip (initialised from the clip of the outer view).
	virtual void DrawContents(PortalDriver& driver, const PortalFrameView& outer, FRenderViewpoint& vp, PortalClip& clip) = 0;
	// Called at the end of the BSP walk of the portal view (line to line portals add the actors of the destination).
	virtual void RenderAttached(PortalDriver& driver, const PortalFrameView& view) {}
	// Draws the stencil geometry for one pass (HWPortal::DrawPortalStencil).
	virtual void DrawPortalStencil(PortalDriver& driver, const PortalFrameView& outer, int pass);

	void SetupStencil(PortalDriver& driver, const PortalFrameView& outer, bool useStencil);
	void RemoveStencil(PortalDriver& driver, const PortalFrameView& outer, bool useStencil);

	void AddLine(const PortalSurface& surface);

protected:
	// The triangles of the flat of one FSection (plane = sector_t::floor / sector_t::ceiling) appended to the stencil buffer,
	// moved by zOffset in height.
	static void AddFlatStencil(PortalDriver& driver, FSection* section, sector_t* sector, int plane, float zOffset);

	// The wall quads of Lines as one triangle list in the driver's stencil buffer (built on first use).
	void BuildWallStencil(PortalDriver& driver);
	void BuildCapStencil(PortalDriver& driver);

	// [first vertex, count] in the stencil vertex buffer of the driver.
	struct StencilRange
	{
		int First = 0;
		int Count = 0;
	};

	StencilRange m_wallRange;
	StencilRange m_bottomCap;
	StencilRange m_topCap;
	bool m_wallBuilt = false;
	bool m_capsBuilt = false;

private:
	uint32_t m_savedVisibility = 0;
};

}
