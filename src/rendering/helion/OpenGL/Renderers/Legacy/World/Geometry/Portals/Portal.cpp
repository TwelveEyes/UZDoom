/*
** Portal.cpp
**
** UZDoom-only (plan M5.2), see Portal.h. Port of HWPortal (hw_portal.cpp: SetupStencil, RemoveStencil, DrawPortalStencil).
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
*/

#include "Portal.h"
#include "PortalDriver.h"
#include "actor.h"
#include "g_levellocals.h"
#include "hw_renderstate.h"
#include "Common/Shared/World/WorldTriangulator.h"
#include "r_utility.h"
#include "v_video.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Geometry::Portals
{

void HorizonPlane::GetFromSector(sector_t* sec, int plane)
{
	Offs.X = (float)sec->GetXOffset(plane);
	Offs.Y = (float)sec->GetYOffset(plane);
	Scale.X = (float)sec->GetXScale(plane);
	Scale.Y = (float)sec->GetYScale(plane);
	Angle = (float)sec->GetAngle(plane).Degrees();
	Texture = sec->GetTexture(plane);
	const secplane_t& secplane = sec->GetSecPlane(plane);
	Texheight = (float)((plane == sector_t::ceiling) ? secplane.fD() : -secplane.fD());
	Sector = sec;
	PlaneIndex = plane;
}

bool HorizonInfo::operator==(const HorizonInfo& other) const
{
	return Plane.Texture == other.Plane.Texture && Plane.Texheight == other.Plane.Texheight && Plane.Angle == other.Plane.Angle &&
		Plane.Offs.X == other.Plane.Offs.X && Plane.Offs.Y == other.Plane.Offs.Y &&
		Plane.Scale.X == other.Plane.Scale.X && Plane.Scale.Y == other.Plane.Scale.Y &&
		LightLevel == other.LightLevel && SpecialColor == other.SpecialColor &&
		Colormap.LightColor == other.Colormap.LightColor && Colormap.FadeColor == other.Colormap.FadeColor &&
		Colormap.Desaturation == other.Colormap.Desaturation && Colormap.FogDensity == other.Colormap.FogDensity;
}

Portal::Portal() : BoundingBox(false)
{
}

Portal::~Portal() = default;

void Portal::AddLine(const PortalSurface& surface)
{
	Lines.push_back(surface);
	BoundingBox.addVertex(surface.X1, surface.Y1);
	BoundingBox.addVertex(surface.X2, surface.Y2);
}

//-----------------------------------------------------------------------------
//
// Stencil geometry
//
//-----------------------------------------------------------------------------

void Portal::BuildWallStencil(PortalDriver& driver)
{
	if (m_wallBuilt)
		return;
	m_wallBuilt = true;

	m_wallRange.First = driver.StencilCount();
	for (const PortalSurface& line : Lines)
	{
		// The corners of the quad of HWWall::MakeVertices: top left, top right, bottom right, bottom left.
		const float a[3] = { line.X1, line.Y1, line.ZTop[0] };
		const float b[3] = { line.X2, line.Y2, line.ZTop[1] };
		const float c[3] = { line.X2, line.Y2, line.ZBottom[1] };
		const float d[3] = { line.X1, line.Y1, line.ZBottom[0] };
		driver.AddStencilQuad(a, b, c, d);
	}
	m_wallRange.Count = driver.StencilCount() - m_wallRange.First;
}

void Portal::AddFlatStencil(PortalDriver& driver, FSection* section, sector_t* sector, int plane, float zOffset)
{
	using namespace ::Helion::World::Geometry::Sectors;
	bool floor = plane == sector_t::floor;
	SectorPlane sectorPlane = SectorPlane::FromSector(sector, floor ? SectorPlaneFace::Floor : SectorPlaneFace::Ceiling);

	TArray<Shared::World::TriangulatedWorldVertex> vertices;
	TArray<uint32_t> indices;
	// Only the positions matter, the texture size is any non zero value.
	Shared::World::WorldTriangulator::HandleSection(section, sectorPlane, floor, ::Helion::Geometry::Vec2F(64, 64), vertices, indices);
	for (unsigned i = 0; i + 2 < indices.Size(); i += 3)
	{
		const auto& a = vertices[indices[i]];
		const auto& b = vertices[indices[i + 1]];
		const auto& c = vertices[indices[i + 2]];
		const float pa[3] = { a.X, a.Y, a.Z + zOffset };
		const float pb[3] = { b.X, b.Y, b.Z + zOffset };
		const float pc[3] = { c.X, c.Y, c.Z + zOffset };
		driver.AddStencilTriangle(pa, pb, pc);
	}
}

void Portal::BuildCapStencil(PortalDriver& driver)
{
	if (m_capsBuilt)
		return;
	m_capsBuilt = true;

	// The caps close the portal against the floor / ceiling over its whole bounding box.
	auto cap = [&](float z, StencilRange& range)
	{
		const float lt[3] = { (float)BoundingBox.left, (float)BoundingBox.top, z };
		const float rt[3] = { (float)BoundingBox.right, (float)BoundingBox.top, z };
		const float rb[3] = { (float)BoundingBox.right, (float)BoundingBox.bottom, z };
		const float lb[3] = { (float)BoundingBox.left, (float)BoundingBox.bottom, z };
		range.First = driver.StencilCount();
		driver.AddStencilQuad(lt, rt, rb, lb);
		range.Count = driver.StencilCount() - range.First;
	};

	if (PlanesUsed & (1 << sector_t::floor))
		cap(-32767.f, m_bottomCap);
	if (PlanesUsed & (1 << sector_t::ceiling))
		cap(32767.f, m_topCap);
}

void Portal::DrawPortalStencil(PortalDriver& driver, const PortalFrameView& outer, int pass)
{
	FRenderState& state = *screen->RenderState();

	BuildWallStencil(driver);
	bool caps = NeedCap() && Lines.size() > 1;
	if (caps && PlanesUsed != 0)
		BuildCapStencil(driver);

	driver.BeginStencilDraw(outer);
	if (m_wallRange.Count > 0)
		driver.DrawStencilRange(m_wallRange.First, m_wallRange.Count);

	if (caps)
	{
		// The cap's depth handling needs special treatment so that it won't block further portal caps.
		if (pass == STP_DepthRestore)
			state.SetDepthRange(1, 1);

		if (m_bottomCap.Count > 0)
			driver.DrawStencilRange(m_bottomCap.First, m_bottomCap.Count);
		if (m_topCap.Count > 0)
			driver.DrawStencilRange(m_topCap.First, m_topCap.Count);

		if (pass == STP_DepthRestore)
			state.SetDepthRange(0, 1);
	}
	driver.EndStencilDraw(outer);
}

//-----------------------------------------------------------------------------
//
// Start
//
//-----------------------------------------------------------------------------

void Portal::SetupStencil(PortalDriver& driver, const PortalFrameView& outer, bool useStencil)
{
	FRenderState& state = *screen->RenderState();

	if (useStencil)
	{
		// Create stencil, increment stencil of valid pixels.
		state.SetStencil(0, SOP_Increment);
		state.SetColorMask(false);
		state.SetDepthFunc(DF_Less);

		if (NeedDepthBuffer())
		{
			state.SetDepthMask(false);							// don't write to Z-buffer!

			DrawPortalStencil(driver, outer, STP_Stencil);

			// Clear Z-buffer
			state.SetStencil(1, SOP_Keep);						// draw sky into stencil. This stage doesn't modify the stencil.
			state.SetDepthMask(true);							// enable z-buffer again
			state.SetDepthRange(1, 1);
			state.SetDepthFunc(DF_Always);
			DrawPortalStencil(driver, outer, STP_DepthClear);

			// set normal drawing mode
			state.SetDepthRange(0, 1);
			state.SetDepthFunc(DF_Less);
			state.SetColorMask(true);
		}
		else
		{
			// No z-buffer is needed therefore we can skip all the complicated stuff that is involved
			// Note: We must draw the stencil with z-write enabled here because there is no second pass!

			state.SetDepthMask(true);
			DrawPortalStencil(driver, outer, STP_AllInOne);
			state.SetStencil(1, SOP_Keep);						// draw sky into stencil. This stage doesn't modify the stencil.
			state.SetColorMask(true);
			state.EnableDepthTest(false);
			state.SetDepthMask(false);							// don't write to Z-buffer!
		}
		screen->stencilValue++;
	}
	else
	{
		if (!NeedDepthBuffer())
		{
			state.SetDepthMask(false);
			state.EnableDepthTest(false);
		}
	}

	// save viewpoint
	const FRenderViewpoint& vp = *outer.Viewpoint;
	m_savedVisibility = vp.camera ? (uint32_t)(vp.camera->renderflags & RF_MAYBEINVISIBLE).GetValue() : 0;
}

//-----------------------------------------------------------------------------
//
// End
//
//-----------------------------------------------------------------------------

void Portal::RemoveStencil(PortalDriver& driver, const PortalFrameView& outer, bool useStencil)
{
	FRenderState& state = *screen->RenderState();
	bool needDepth = NeedDepthBuffer();

	// Restore the old view
	const FRenderViewpoint& vp = *outer.Viewpoint;
	if (vp.camera != nullptr)
		vp.camera->renderflags = (vp.camera->renderflags & ~RF_MAYBEINVISIBLE) | ActorRenderFlags::FromInt((int)m_savedVisibility);

	if (useStencil)
	{
		state.SetColorMask(false);						// no graphics

		if (needDepth)
		{
			// first step: reset the depth buffer to max. depth
			state.SetDepthRange(1, 1);							// always
			state.SetDepthFunc(DF_Always);						// write the farthest depth value
			DrawPortalStencil(driver, outer, STP_DepthClear);
		}
		else
		{
			state.EnableDepthTest(true);
		}

		// second step: restore the depth buffer to the previous values and reset the stencil
		state.SetDepthFunc(DF_LEqual);
		state.SetDepthRange(0, 1);
		state.SetStencil(0, SOP_Decrement);
		DrawPortalStencil(driver, outer, STP_DepthRestore);
		state.SetDepthFunc(DF_Less);

		state.SetColorMask(true);
		screen->stencilValue--;

		// restore old stencil op.
		state.SetStencil(0, SOP_Keep);
	}
	else
	{
		if (needDepth)
		{
			state.Clear(CT_Depth);
		}
		else
		{
			state.EnableDepthTest(true);
			state.SetDepthMask(true);
		}

		// This draws a valid z-buffer into the stencil's contents to ensure it
		// doesn't get overwritten by the level's geometry.

		state.SetDepthFunc(DF_LEqual);
		state.SetDepthRange(0, 1);
		state.SetColorMask(false, false, false, true);	// mark portal in alpha channel but don't touch color
		state.ClearCustomBlend();
		state.SetRenderStyle(STYLE_Source);
		DrawPortalStencil(driver, outer, STP_DepthRestore);
		state.SetColorMask(true);
		state.SetDepthFunc(DF_Less);
	}
}

}
