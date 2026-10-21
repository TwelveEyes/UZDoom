/*
** PortalDriver.Collect.cpp
**
** UZDoom-only (plan M5.2), see PortalDriver.h. The collection half of the portal driver: HWWall::PutPortal and the portal branches of
** HWWall::Process (hw_walls.cpp), SkyPlane / SkyLine / SkyNormal / SkyTop / SkyBottom (hw_sky.cpp), DoHorizon, the plane alpha rules of
** HWFlat::ProcessSector and classic AddSubsectorToPortal.
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
** Plain level sky (PORTALTYPE_SKY of the classic renderer) is not collected here: LegacySkyRenderer owns it. Every place where the
** classic SkyPlane / SkyLine decided "plain sky" (sky flat without a sector portal, a skyviewpoint portal with gl_noskyboxes) is left
** to it. The classic walls of those places are the sky wall quads the geometry renderer already creates for LegacySkyRenderer.
**
*/

#include "PortalDriver.h"
#include "EEHorizonPortal.h"
#include "HorizonPortal.h"
#include "LineToLinePortal.h"
#include "MirrorPortal.h"
#include "PlaneMirrorPortal.h"
#include "SectorStackPortal.h"
#include "SkyboxPortal.h"
#include "Common/Shared/RenderInfo.h"
#include "actor.h"
#include "doomdata.h"
#include "doomdef.h"
#include "g_levellocals.h"
#include "gametexture.h"
#include "hw_cvars.h"
#include "p_3dfloors.h"
#include "p_lnspec.h"
#include "portal.h"
#include "r_utility.h"
#include "texturemanager.h"

EXTERN_CVAR(Bool, gl_noskyboxes)

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Geometry::Portals
{

//-----------------------------------------------------------------------------
//
// PutPortal
//
//-----------------------------------------------------------------------------

void PortalDriver::PutHorizon(const PortalSurface& surface, const HorizonInfo& info)
{
	if (m_dryRun)
		return;

	// portalState.UniqueHorizons.Get(horizon)
	const HorizonInfo* unique = nullptr;
	for (const HorizonInfo& existing : m_uniqueHorizons)
	{
		if (existing == info)
		{
			unique = &existing;
			break;
		}
	}
	if (unique == nullptr)
	{
		m_uniqueHorizons.push_back(info);
		unique = &m_uniqueHorizons.back();
	}

	Portal* portal = FindPortal(unique);
	if (portal == nullptr)
	{
		auto created = std::make_unique<HorizonPortal>(unique, *CurrentView().Viewpoint);
		portal = created.get();
		Add(std::move(created));
	}
	portal->AddLine(surface);
}

void PortalDriver::PutSkybox(const PortalSurface& surface, FSectorPortal* secportal, int plane)
{
	if (m_dryRun)
		return;

	Portal* portal = FindPortal(secportal);
	if (portal == nullptr)
	{
		// either a regular skybox or an Eternity-style horizon.
		// (UZDoom's hw_walls.cpp only pushes the skybox portal into the list, so the walls of Eternity horizons are added to a
		// portal that is never drawn. GZDoom pushes both; this is the evident intent.)
		std::unique_ptr<Portal> created;
		if (secportal->mType != PORTS_SKYVIEWPOINT)
			created = std::make_unique<EEHorizonPortal>(secportal);
		else
			created = std::make_unique<SkyboxPortal>(secportal);
		portal = created.get();
		Add(std::move(created));
	}
	portal->AddLine(surface);
	if (plane != -1)
		portal->PlanesUsed |= (1 << plane);
}

void PortalDriver::PutSectorStack(const PortalSurface& surface, FSectorPortalGroup* group, int plane)
{
	if (m_dryRun)
		return;

	Portal* portal = FindPortal(group);
	if (portal == nullptr)
	{
		auto created = std::make_unique<SectorStackPortal>(group);
		portal = created.get();
		Add(std::move(created));
	}
	portal->AddLine(surface);
	if (plane != -1)
		portal->PlanesUsed |= (1 << plane);
}

void PortalDriver::PutPlaneMirror(const PortalSurface& surface, secplane_t* planeMirror, int plane)
{
	if (m_dryRun)
		return;

	const FRenderViewpoint& vp = *CurrentView().Viewpoint;
	if (vp.bDoOrtho ? (vp.ViewVector3D.dot(planeMirror->Normal()) < 0)
		: (m_state.PlaneMirrorMode * planeMirror->fC() <= 0))
	{
		// portalState.UniquePlaneMirrors.Get(planemirror)
		secplane_t* unique = nullptr;
		for (secplane_t& existing : m_uniquePlaneMirrors)
		{
			if (existing == *planeMirror)
			{
				unique = &existing;
				break;
			}
		}
		if (unique == nullptr)
		{
			m_uniquePlaneMirrors.push_back(*planeMirror);
			unique = &m_uniquePlaneMirrors.back();
		}

		Portal* portal = FindPortal(unique);
		if (portal == nullptr)
		{
			auto created = std::make_unique<PlaneMirrorPortal>(unique);
			portal = created.get();
			Add(std::move(created));
		}
		portal->AddLine(surface);
		if (plane != -1)
			portal->PlanesUsed |= (1 << plane);
	}
}

void PortalDriver::PutMirror(const PortalSurface& surface)
{
	if (!m_dryRun)
	{
		Portal* portal = FindPortal(surface.Seg->linedef);
		if (portal == nullptr)
		{
			auto created = std::make_unique<MirrorPortal>(surface.Seg->linedef);
			portal = created.get();
			Add(std::move(created));
		}
		portal->AddLine(surface);
	}

	// The sorted bucket belongs to the gather of the view, so the surface is registered again by the second gather of a view whose
	// portals have been drawn (SetDryRun).
	if (gl_mirror_envmap && m_sorted != nullptr && Hooks.DrawMirrorSurface)
	{
		// draw a reflective layer over the mirror: translucent, so it goes into the sorted bucket (plan D10).
		const FRenderViewpoint& vp = *CurrentView().Viewpoint;
		float key = Data::SortedTranslucentRenderer::DistanceSquared((surface.X1 + surface.X2) * 0.5f, (surface.Y1 + surface.Y2) * 0.5f,
			(surface.ZTop[0] + surface.ZBottom[0]) * 0.5f, (float)vp.Pos.X, (float)vp.Pos.Y, (float)vp.Pos.Z);
		PortalSurface copy = surface;
		m_sorted->Add(key, [this, copy](Shared::RenderInfo& renderInfo)
		{
			if (Hooks.DrawMirrorSurface)
				Hooks.DrawMirrorSurface(copy, renderInfo);
		});
	}
}

void PortalDriver::PutLineToLine(const PortalSurface& surface, FLinePortalSpan* span)
{
	if (m_dryRun)
		return;

	if (span == nullptr)
		return;

	Portal* portal = FindPortal(span);
	if (portal == nullptr)
	{
		line_t* otherside = span->lines[0]->mDestination;
		FLevelLocals* level = CurrentView().Level;
		if (otherside != nullptr && otherside->portalindex < level->linePortals.Size() && Hooks.ProcessActorsInPortal)
		{
			Hooks.ProcessActorsInPortal(otherside->getPortal()->mGroup);
		}
		auto created = std::make_unique<LineToLinePortal>(span);
		portal = created.get();
		Add(std::move(created));
	}
	portal->AddLine(surface);
}

//-----------------------------------------------------------------------------
//
// Plane helpers (HWFlat::ProcessSector, HWWall::SkyPlane)
//
//-----------------------------------------------------------------------------

float PortalDriver::PlaneAlpha(sector_t* sector, int plane, bool* isPortal)
{
	FSectorPortal* port = sector->ValidatePortal(plane);
	if (isPortal != nullptr)
		*isPortal = port != nullptr;
	if (port != nullptr)
		return (float)sector->GetAlpha(plane);
	return 1.0f - sector->GetReflect(plane);
}

bool PortalDriver::PlaneIsPortal(sector_t* sector, int plane)
{
	FSectorPortal* sportal = sector->ValidatePortal(plane);
	if (sportal != nullptr && (sportal->mFlags & PORTSF_INSKYBOX))
		sportal = nullptr;	// no recursions

	if (sportal != nullptr)
	{
		// A skybox with skyboxes disabled is the plain level sky.
		return !(gl_noskyboxes && sportal->mType == PORTS_SKYVIEWPOINT);
	}
	if (sector->GetTexture(plane) == skyflatnum)
		return false;
	return sector->GetReflect(plane) > 0 && !(sector->Level->ib_compatflags & BCOMPATF_NOMIRRORS);
}

void PortalDriver::AddTranslucentPlane(sector_t* sector, int plane, float alpha, const DVector3& center)
{
	if (m_sorted == nullptr || !HasView() || !Hooks.DrawTranslucentPlane)
		return;

	const FRenderViewpoint& vp = *CurrentView().Viewpoint;
	float key = Data::SortedTranslucentRenderer::DistanceSquared((float)center.X, (float)center.Y, (float)center.Z,
		(float)vp.Pos.X, (float)vp.Pos.Y, (float)vp.Pos.Z);
	m_sorted->Add(key, [this, sector, plane, alpha](Shared::RenderInfo& renderInfo)
	{
		if (Hooks.DrawTranslucentPlane)
			Hooks.DrawTranslucentPlane(sector, plane, alpha, renderInfo);
	});
}

void PortalDriver::ProcessSubsector(subsector_t* sub, sector_t* fakesector)
{
	if (m_dryRun || m_frames.empty())
		return;

	// This is for portal coverage.
	for (int plane : { sector_t::ceiling, sector_t::floor })
	{
		FSectorPortalGroup* group = fakesector->GetPortalGroup(plane);
		if (group != nullptr)
		{
			Portal* portal = FindPortal(group);
			if (portal == nullptr)
			{
				auto created = std::make_unique<SectorStackPortal>(group);
				portal = created.get();
				Add(std::move(created));
			}
			static_cast<SectorStackPortal*>(portal)->AddSubsector(sub);
		}
	}
}

//-----------------------------------------------------------------------------
//
// SkyPlane: the portal part
//
//-----------------------------------------------------------------------------

bool PortalDriver::SkyPlane(PortalSurface& surface, sector_t* sector, int plane, bool allowReflect)
{
	const PortalFrameView& view = CurrentView();
	const FRenderViewpoint& vp = *view.Viewpoint;
	FLevelLocals* level = view.Level;

	FSectorPortal* sportal = sector->ValidatePortal(plane);
	if (sportal != nullptr && sportal->mFlags & PORTSF_INSKYBOX) sportal = nullptr;	// no recursions, delete it here to simplify the following code

	// Either a regular sky or a skybox with skyboxes disabled: LegacySkyRenderer draws it.
	if ((sportal == nullptr && sector->GetTexture(plane) == skyflatnum) || (gl_noskyboxes && sportal != nullptr && sportal->mType == PORTS_SKYVIEWPOINT))
	{
		return false;
	}

	if (sportal != nullptr)
	{
		switch (sportal->mType)
		{
		case PORTS_STACKEDSECTORTHING:
		case PORTS_PORTAL:
		case PORTS_LINKEDPORTAL:
		{
			if (vp.bDoOob)
			{
				secplane_t myplane = plane ? sector->ceilingplane : sector->floorplane;
				if (vp.bDoOrtho && vp.ViewVector3D.dot(myplane.Normal()) > 0.0) return false;
				else if (plane == 1 && vp.Pos.Z >= myplane.ZatPoint(vp.Pos)) return false;
				else if (plane == 0 && vp.Pos.Z <= myplane.ZatPoint(vp.Pos)) return false;
			}
			FSectorPortalGroup* glport = sector->GetPortalGroup(plane);
			if (glport != nullptr)
			{
				if (sector->PortalBlocksView(plane)) return false;
				if (screen->instack[1 - plane]) return false;
				PutSectorStack(surface, glport, plane);
				return true;
			}
			return false;
		}

		case PORTS_SKYVIEWPOINT:
		case PORTS_HORIZON:
		case PORTS_PLANE:
			PutSkybox(surface, sportal, plane);
			return true;
		}
		return false;
	}
	else if (allowReflect && sector->GetReflect(plane) > 0 && !(level->ib_compatflags & BCOMPATF_NOMIRRORS))
	{
		auto vpz = vp.Pos.Z;
		if ((plane == sector_t::ceiling && vpz > sector->ceilingplane.fD()) ||
			(plane == sector_t::floor && vpz < -sector->floorplane.fD())) return false;
		secplane_t* planemirror = plane == sector_t::ceiling ? &sector->ceilingplane : &sector->floorplane;
		PutPlaneMirror(surface, planemirror, plane);
		return true;
	}
	return false;
}

//-----------------------------------------------------------------------------
//
// Skies on lines
//
//-----------------------------------------------------------------------------

void PortalDriver::SkyLine(PortalSurface& surface, sector_t* fs, line_t* line)
{
	FSectorPortal* secport = line->GetTransferredPortal();

	// JUSTHIT is used as an indicator that a skybox is in use.
	// This is to avoid recursion

	if (!gl_noskyboxes && secport && (secport->mSkybox == nullptr || !(secport->mFlags & PORTSF_INSKYBOX)))
	{
		PutSkybox(surface, secport, -1);
	}
	// else: the plain level sky (LegacySkyRenderer).
}

void PortalDriver::SkyNormal(PortalSurface& surface, sector_t* fs, const float zfloor[2], const float zceil[2])
{
	surface.ZTop[0] = surface.ZTop[1] = 32768.0f;
	surface.ZBottom[0] = zceil[0];
	surface.ZBottom[1] = zceil[1];
	SkyPlane(surface, fs, sector_t::ceiling, true);

	surface.ZTop[0] = zfloor[0];
	surface.ZTop[1] = zfloor[1];
	surface.ZBottom[0] = surface.ZBottom[1] = -32768.0f;
	SkyPlane(surface, fs, sector_t::floor, true);
}

void PortalDriver::SkyTop(PortalSurface& surface, seg_t* seg, sector_t* fs, sector_t* bs, vertex_t* v1, vertex_t* v2, const float zceil[2])
{
	if (fs->GetTexture(sector_t::ceiling) == skyflatnum)
	{
		if (bs->special == GLSector_NoSkyDraw || (bs->MoreFlags & SECMF_NOSKYWALLS) != 0 || (seg->linedef->flags & ML_NOSKYWALLS) != 0) return;
		if (bs->GetTexture(sector_t::ceiling) == skyflatnum)
		{
			// if the back sector is closed the sky must be drawn!
			if (bs->ceilingplane.ZatPoint(v1) > bs->floorplane.ZatPoint(v1) ||
				bs->ceilingplane.ZatPoint(v2) > bs->floorplane.ZatPoint(v2) || bs->transdoor)
				return;

			// one more check for some ugly transparent door hacks
			if (!bs->floorplane.isSlope() && !fs->floorplane.isSlope())
			{
				if (bs->GetPlaneTexZ(sector_t::floor) == fs->GetPlaneTexZ(sector_t::floor) + 1.)
				{
					auto tex = TexMan.GetGameTexture(seg->sidedef->GetTexture(side_t::bottom), true);
					if (!tex || !tex->isValid()) return;

					// very, very, very ugly special case (See Icarus MAP14)
					// It is VERY important that this is only done for a floor height difference of 1
					// or it will cause glitches elsewhere.
					tex = TexMan.GetGameTexture(seg->sidedef->GetTexture(side_t::mid), true);
					if (tex != NULL && !(seg->linedef->flags & ML_DONTPEGTOP) &&
						seg->sidedef->GetTextureYOffset(side_t::mid) > 0)
					{
						surface.ZTop[0] = surface.ZTop[1] = 32768.0f;
						surface.ZBottom[0] = surface.ZBottom[1] =
							(float)(bs->ceilingplane.ZatPoint(v2) + seg->sidedef->GetTextureYOffset(side_t::mid));
						SkyPlane(surface, fs, sector_t::ceiling, false);
						return;
					}
				}
			}
		}

		surface.ZTop[0] = surface.ZTop[1] = 32768.0f;

		if (bs->GetTexture(sector_t::ceiling) != skyflatnum)
		{
			surface.ZBottom[0] = zceil[0];
			surface.ZBottom[1] = zceil[1];
		}
		else
		{
			surface.ZBottom[0] = (float)bs->ceilingplane.ZatPoint(v1);
			surface.ZBottom[1] = (float)bs->ceilingplane.ZatPoint(v2);
		}
	}
	else
	{
		float frontreflect = fs->GetReflect(sector_t::ceiling);
		if (frontreflect > 0)
		{
			// [DVR] Changed the stencil for planemirrors, so now I need intra-portal lines
		}
		else
		{
			int type = fs->GetPortalType(sector_t::ceiling);
			if (type == PORTS_STACKEDSECTORTHING || type == PORTS_PORTAL || type == PORTS_LINKEDPORTAL)
			{
				auto pfront = fs->GetPortalGroup(sector_t::ceiling);
				auto pback = bs->GetPortalGroup(sector_t::ceiling);
				if (pfront == NULL || fs->PortalBlocksView(sector_t::ceiling)) return;
				if (pfront == pback && !bs->PortalBlocksView(sector_t::ceiling)) return;
			}
		}

		// stacked sectors
		surface.ZTop[0] = surface.ZTop[1] = 32768.0f;
		surface.ZBottom[0] = (float)fs->ceilingplane.ZatPoint(v1);
		surface.ZBottom[1] = (float)fs->ceilingplane.ZatPoint(v2);
	}

	SkyPlane(surface, fs, sector_t::ceiling, true);
}

void PortalDriver::SkyBottom(PortalSurface& surface, seg_t* seg, sector_t* fs, sector_t* bs, vertex_t* v1, vertex_t* v2, const float zfloor[2])
{
	if (fs->GetTexture(sector_t::floor) == skyflatnum)
	{
		if (bs->special == GLSector_NoSkyDraw || (bs->MoreFlags & SECMF_NOSKYWALLS) != 0 || (seg->linedef->flags & ML_NOSKYWALLS) != 0) return;
		auto tex = TexMan.GetGameTexture(seg->sidedef->GetTexture(side_t::bottom), true);

		// For lower skies the normal logic only applies to walls with no lower texture.
		if (!tex->isValid())
		{
			if (bs->GetTexture(sector_t::floor) == skyflatnum)
			{
				// if the back sector is closed the sky must be drawn!
				if (bs->ceilingplane.ZatPoint(v1) > bs->floorplane.ZatPoint(v1) ||
					bs->ceilingplane.ZatPoint(v2) > bs->floorplane.ZatPoint(v2))
					return;
			}
			else
			{
				// Special hack for Vrack2b.
				const FRenderViewpoint& vp = *CurrentView().Viewpoint;
				if (bs->floorplane.ZatPoint(vp.Pos) > vp.Pos.Z) return;
			}
		}
		surface.ZBottom[0] = surface.ZBottom[1] = -32768.0f;

		if ((tex && tex->isValid()) || bs->GetTexture(sector_t::floor) != skyflatnum)
		{
			surface.ZTop[0] = zfloor[0];
			surface.ZTop[1] = zfloor[1];
		}
		else
		{
			surface.ZTop[0] = (float)bs->floorplane.ZatPoint(v1);
			surface.ZTop[1] = (float)bs->floorplane.ZatPoint(v2);
		}
	}
	else
	{
		float frontreflect = fs->GetReflect(sector_t::floor);
		if (frontreflect > 0)
		{
			// [DVR] Changed the stencil for planemirrors, so now I need intra-portal lines
		}
		else
		{
			int type = fs->GetPortalType(sector_t::floor);
			if (type == PORTS_STACKEDSECTORTHING || type == PORTS_PORTAL || type == PORTS_LINKEDPORTAL)
			{
				auto pfront = fs->GetPortalGroup(sector_t::floor);
				auto pback = bs->GetPortalGroup(sector_t::floor);
				if (pfront == NULL || fs->PortalBlocksView(sector_t::floor)) return;
				if (pfront == pback && !bs->PortalBlocksView(sector_t::floor)) return;
			}
		}

		// stacked sectors
		surface.ZBottom[0] = surface.ZBottom[1] = -32768.0f;
		surface.ZTop[0] = (float)fs->floorplane.ZatPoint(v1);
		surface.ZTop[1] = (float)fs->floorplane.ZatPoint(v2);
	}

	SkyPlane(surface, fs, sector_t::floor, true);
}

//-----------------------------------------------------------------------------
//
// Horizon lines
//
//-----------------------------------------------------------------------------

bool PortalDriver::DoHorizon(seg_t* seg, sector_t* fs, vertex_t* v1, vertex_t* v2, PortalSurface& surface)
{
	const PortalFrameView& view = CurrentView();
	const FRenderViewpoint& vp = *view.Viewpoint;
	bool fullbright = view.RenderInfo != nullptr && view.RenderInfo->DrawFullBright;

	// ZDoom doesn't support slopes in a horizon sector so I won't either!
	surface.ZTop[1] = surface.ZTop[0] = (float)fs->GetPlaneTexZ(sector_t::ceiling);
	surface.ZBottom[1] = surface.ZBottom[0] = (float)fs->GetPlaneTexZ(sector_t::floor);

	auto vpz = vp.Pos.Z;
	if (vpz < fs->GetPlaneTexZ(sector_t::ceiling))
	{
		if (vpz > fs->GetPlaneTexZ(sector_t::floor))
			surface.ZBottom[1] = surface.ZBottom[0] = (float)vpz;

		if (fs->GetTexture(sector_t::ceiling) == skyflatnum)
		{
			SkyPlane(surface, fs, sector_t::ceiling, false);
		}
		else
		{
			HorizonInfo hi;
			hi.Plane.GetFromSector(fs, sector_t::ceiling);
			hi.LightLevel = fs->GetCeilingLight();
			hi.Colormap = fs->Colormap;
			hi.SpecialColor = fs->SpecialColors[sector_t::ceiling];

			if (fs->e->XFloor.ffloors.Size())
			{
				lightlist_t* light = P_GetPlaneLight(fs, &fs->ceilingplane, true);

				if (!(fs->GetFlags(sector_t::ceiling) & PLANEF_ABSLIGHTING)) hi.LightLevel = *light->p_lightlevel;
				hi.Colormap.CopyLight(light->extra_colormap);
			}

			if (fullbright) hi.Colormap.Clear();
			PutHorizon(surface, hi);
		}
		surface.ZTop[1] = surface.ZTop[0] = surface.ZBottom[0];
	}

	if (vpz > fs->GetPlaneTexZ(sector_t::floor))
	{
		surface.ZBottom[1] = surface.ZBottom[0] = (float)fs->GetPlaneTexZ(sector_t::floor);
		if (fs->GetTexture(sector_t::floor) == skyflatnum)
		{
			SkyPlane(surface, fs, sector_t::floor, false);
		}
		else
		{
			HorizonInfo hi;
			hi.Plane.GetFromSector(fs, sector_t::floor);
			hi.LightLevel = fs->GetFloorLight();
			hi.Colormap = fs->Colormap;
			hi.SpecialColor = fs->SpecialColors[sector_t::floor];

			if (fs->e->XFloor.ffloors.Size())
			{
				lightlist_t* light = P_GetPlaneLight(fs, &fs->floorplane, false);

				if (!(fs->GetFlags(sector_t::floor) & PLANEF_ABSLIGHTING)) hi.LightLevel = *light->p_lightlevel;
				hi.Colormap.CopyLight(light->extra_colormap);
			}

			if (fullbright) hi.Colormap.Clear();
			PutHorizon(surface, hi);
		}
	}
	return true;
}

//-----------------------------------------------------------------------------
//
// HWWall::Process, the portal branches
//
//-----------------------------------------------------------------------------

bool PortalDriver::LineHasPortal(line_t* line)
{
	return line->special == Line_Horizon || line->special == Line_Mirror || line->isVisualPortal() || line->GetTransferredPortal() != nullptr;
}

bool PortalDriver::SectorHasPortalPlanes(sector_t* sector)
{
	for (int plane : { sector_t::floor, sector_t::ceiling })
	{
		if (sector->ValidatePortal(plane) != nullptr || sector->GetReflect(plane) > 0)
			return true;
	}
	return false;
}

bool PortalDriver::SideReplacedByPortal(const side_t* side)
{
	line_t* linedef = side->linedef;
	if (linedef == nullptr || !LineHasPortal(linedef))
		return false;
	if (linedef->special == Line_Horizon)
		return true;

	bool isportal = linedef->isVisualPortal() && side == linedef->sidedef[0];
	sector_t* backsector = side == linedef->sidedef[0] ? linedef->backsector : linedef->frontsector;
	if (linedef->sidedef[1] == nullptr)
		backsector = nullptr;
	if (!(!backsector || (!(linedef->flags & (ML_TWOSIDED | ML_3DMIDTEX)) && !isportal)))
		return false;

	if (isportal)
		return true;
	if (FSectorPortal* secport = linedef->GetTransferredPortal())
		return !gl_noskyboxes && (secport->mSkybox == nullptr || !(secport->mFlags & PORTSF_INSKYBOX));
	if (linedef->special == Line_Mirror && gl_mirrors && !(const_cast<side_t*>(side)->GetLevel()->ib_compatflags & BCOMPATF_NOMIRRORS))
	{
		auto tex = TexMan.GetGameTexture(side->GetTexture(side_t::mid), true);
		return tex && tex->isValid();
	}
	return false;
}

bool PortalDriver::ProcessSide(side_t* side, sector_t* frontsector, sector_t* backsector)
{
	bool taken = false;
	for (int i = 0; i < side->numsegs; i++)
	{
		seg_t* seg = side->segs[i];
		taken |= ProcessSeg(seg, frontsector, backsector, seg->Subsector);
	}
	return taken;
}

bool PortalDriver::ProcessSeg(seg_t* seg, sector_t* frontsector, sector_t* backsector, subsector_t* sub)
{
	if (!HasView() || seg->sidedef == nullptr || seg->linedef == nullptr)
		return false;

	// SkyPlane (the only place that makes sector portals and plane mirrors) works on the front sector.
	if (!LineHasPortal(seg->linedef) && !SectorHasPortalPlanes(frontsector))
		return false;

	const PortalFrameView& view = CurrentView();
	FLevelLocals* level = view.Level;
	line_t* linedef = seg->linedef;

	vertex_t* v1;
	vertex_t* v2;
	if (seg->sidedef == linedef->sidedef[0])
	{
		v1 = linedef->v1;
		v2 = linedef->v2;
	}
	else
	{
		v1 = linedef->v2;
		v2 = linedef->v1;
	}
	if (seg->sidedef->Flags & WALLF_POLYOBJ)
	{
		// polyobjects must be rendered per seg.
		v1 = seg->v1;
		v2 = seg->v2;
	}

	sector_t* segfront = frontsector;
	if ((seg->sidedef->Flags & WALLF_POLYOBJ) && seg->backsector)
	{
		// Textures on 2-sided polyobjects are aligned to the actual seg's sectors
		segfront = seg->frontsector;
	}

	float zfloor[2], zceil[2];
	zfloor[0] = (float)segfront->floorplane.ZatPoint(v1);
	zfloor[1] = (float)segfront->floorplane.ZatPoint(v2);
	zceil[0] = (float)segfront->ceilingplane.ZatPoint(v1);
	zceil[1] = (float)segfront->ceilingplane.ZatPoint(v2);

	PortalSurface surface;
	surface.X1 = (float)v1->fX();
	surface.Y1 = (float)v1->fY();
	surface.X2 = (float)v2->fX();
	surface.Y2 = (float)v2->fY();
	surface.Seg = seg;
	surface.Sub = sub;
	surface.Vertexes[0] = v1;
	surface.Vertexes[1] = v2;

	if (linedef->special == Line_Horizon)
	{
		SkyNormal(surface, frontsector, zfloor, zceil);
		DoHorizon(seg, frontsector, v1, v2, surface);
		return true;
	}

	bool isportal = linedef->isVisualPortal() && seg->sidedef == linedef->sidedef[0];

	// [GZ] 3D middle textures are necessarily two-sided, even if they lack the explicit two-sided flag
	if (!backsector || (!(linedef->flags & (ML_TWOSIDED | ML_3DMIDTEX)) && !isportal)) // one sided
	{
		// sector's sky
		SkyNormal(surface, frontsector, zfloor, zceil);

		if (isportal)
		{
			surface.ZTop[0] = zceil[0];
			surface.ZTop[1] = zceil[1];
			surface.ZBottom[0] = zfloor[0];
			surface.ZBottom[1] = zfloor[1];
			PutLineToLine(surface, linedef->getPortal()->mGroup);
			return true;
		}
		else if (linedef->GetTransferredPortal())
		{
			SkyLine(surface, frontsector, linedef);
			// The skybox / sky replaces the wall.
			return true;
		}
		else if (linedef->special == Line_Mirror && gl_mirrors && !(level->ib_compatflags & BCOMPATF_NOMIRRORS))
		{
			// HWWall::DoTexture: only a one sided line with a valid middle texture is a mirror.
			auto tex = TexMan.GetGameTexture(seg->sidedef->GetTexture(side_t::mid), true);
			if (tex && tex->isValid())
			{
				surface.ZTop[0] = zceil[0];
				surface.ZTop[1] = zceil[1];
				surface.ZBottom[0] = zfloor[0];
				surface.ZBottom[1] = zfloor[1];
				PutMirror(surface);
				return true;
			}
		}
		return false;
	}
	else // two sided
	{
		float bfh1 = (float)(backsector->floorplane.ZatPoint(v1));
		float bfh2 = (float)(backsector->floorplane.ZatPoint(v2));
		float bch1 = (float)(backsector->ceilingplane.ZatPoint(v1));
		float bch2 = (float)(backsector->ceilingplane.ZatPoint(v2));

		if ((seg->sidedef->Flags & WALLF_POLYOBJ) && seg->backsector)
		{
			bfh1 = (float)(seg->backsector->floorplane.ZatPoint(v1));
			bfh2 = (float)(seg->backsector->floorplane.ZatPoint(v2));
			bch1 = (float)(seg->backsector->ceilingplane.ZatPoint(v1));
			bch2 = (float)(seg->backsector->ceilingplane.ZatPoint(v2));
		}

		if (isportal && seg->backsector == nullptr)
		{
			SkyNormal(surface, frontsector, zfloor, zceil); // For sky rendering purposes this needs to be treated as a one-sided wall.

			// With floor or ceiling alignment the classic renderer returns here: the rest of the line is not processed.
			switch (linedef->getPortalAlignment())
			{
			case PORG_FLOOR:
			case PORG_CEILING:
				return false;

			default:
				break;
			}
		}
		else
		{
			SkyTop(surface, seg, frontsector, backsector, v1, v2, zceil);
			SkyBottom(surface, seg, frontsector, backsector, v1, v2, zfloor);
		}

		if (isportal)
		{
			surface.ZTop[0] = bch1;
			surface.ZTop[1] = bch2;
			surface.ZBottom[0] = bfh1;
			surface.ZBottom[1] = bfh2;
			PutLineToLine(surface, linedef->getPortal()->mGroup);
		}
		// The middle texture of a two sided portal line is still drawn by the geometry renderer.
		return false;
	}
}

}
