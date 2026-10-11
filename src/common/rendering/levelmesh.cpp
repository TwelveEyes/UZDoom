/*
** levelmesh.cpp
**
** Builder for the backend-neutral static level-mesh model. Replaces the
** classic DoomLevelMesh build site at level load. Walks FLevelLocals and
** bakes the whole level into the vertex pool, per-region IBOs, and fixed-size
** surface/region records. The u/v bake mirrors the classic per-surface
** calculation so the later GL33 draw path is bit-exact.
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

#include "levelmesh.h"
#include "r_defs.h"
#include "r_sky.h"
#include "gametexture.h"
#include "texturemanager.h"
#include "hw_lighting.h"
#include "p_3dfloors.h"
#include "g_levellocals.h"
#include "a_sharedglobal.h"
#include "m_round.h"
#include "p_lnspec.h"
#include "c_cvars.h"
#include "earcut.hpp"
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstring>

// Per-tier default skew cvars (hw_walls.cpp), read by the HWWall::Process port.
EXTERN_CVAR(Int, topskew)
EXTERN_CVAR(Int, midskew)
EXTERN_CVAR(Int, bottomskew)
// Seamless T-junction splits (hw_cvars); read at mesh build (map load).
EXTERN_CVAR(Bool, gl_seamless)

using levelmesh::FLevelMesh;
using levelmesh::LevelMeshVertex;
using levelmesh::LevelMeshSurface;
using levelmesh::LevelMeshRegion;
using levelmesh::LevelMeshSubRange;
using levelmesh::LevelMeshTexRange;
using levelmesh::LevelMeshBand;
using levelmesh::LMF_ISFLAT;
using levelmesh::LMF_3DFLOOR;
using levelmesh::LMF_LOADDYN;
using levelmesh::LMF_SIDE_ABSLIGHT;
using levelmesh::LMF_SIDE_ABSTIER;
using levelmesh::LMF_SIDE_NOFAKECONTRAST;
using levelmesh::LMF_SIDE_SMOOTHLIGHT;
using levelmesh::LMF_SIDE_LIGHTFOG;
using levelmesh::LMF_LIGHTMAP;
using levelmesh::LMF_POLYOBJ;
using levelmesh::LMF_3DFLOOR;
using levelmesh::LMF_OPENING_UPPER;
using levelmesh::LMF_OPENING_MIDDLE;
using levelmesh::LMF_OPENING_LOWER;
using levelmesh::LMF_UNPEGGEDLOWER;
using levelmesh::LM_PLANEREF_NONE;
using levelmesh::LM_LIGHT_SLOT_FLOOR;
using levelmesh::LM_LIGHT_SLOT_CEILING;
using levelmesh::LM_LIGHT_SLOT_WALL;
using levelmesh::LMDS_CLAMPX;
using levelmesh::LMDS_CLAMPY;
using levelmesh::LMDS_TEXCLAMPY;
using levelmesh::LMDS_MASKED;
using levelmesh::LMDS_NOCULL;

//============================================================================
//
// Local helpers
//
//============================================================================

// planeRef encoding: sectorIndex<<1 | planeBit (0=floor, 1=ceiling).
static uint32_t LM_PlaneRef(const sector_t *sec, int plane)
{
	return ((uint32_t)sec->Index() << 1) | (uint32_t)(plane == sector_t::ceiling ? 1 : 0);
}

// lightRef encoding: sectorIndex<<2 | slot. The effective light sector mirrors the
// classic renderer's fakesector resolution (hw_bsp.cpp DoSubsector passes hw_FakeFlat()
// of the render sector as the wall/flat light source): a sector with a heightsec
// (Transfer_Heights / transdoor hack) transfers the model sector's light unless it
// carries SECMF_NOFAKELIGHT.
static uint32_t LM_LightRef(const sector_t *sec, int slot)
{
	const sector_t *light = sec;
	if (sec->GetHeightSec() && !(sec->GetHeightSec()->MoreFlags & SECMF_NOFAKELIGHT))
		light = sec->GetHeightSec();
	return ((uint32_t)light->Index() << 2) | (uint32_t)slot;
}

namespace
{
//============================================================================
//
// The builder accumulates the model while walking FLevelLocals.
//
//============================================================================

struct Builder
{
	FLevelLocals &level;
	levelmesh::FLevelMesh &mesh;
	TArray<int32_t> sectorRegion;
	int m_curRegion = -1;

	// per-side "wall already emitted" marks (non-polyobject walls go per line)
	TArray<uint8_t> sideEmitted;

	// per-region vertex offset (the pool is laid out region by region)
	TArray<uint32_t> regionVertexOffset;

	// Record the vertex range for a surface just after its vertices are emitted,
	// and compute its world XY AABB from the pool positions. The sub-range entry
	// is parallel to the surface (same index), so we write the AABB in place.
	void RecordSurfaceVerts(uint32_t surfaceIndex, uint32_t vertBase, uint32_t vertCount)
	{
		LevelMeshSubRange &sr = mesh.subRanges[surfaceIndex];
		sr.aabbMin[0] = sr.aabbMin[1] = 1e30f;
		sr.aabbMax[0] = sr.aabbMax[1] = -1e30f;
		for (uint32_t i = 0; i < vertCount; i++)
		{
			float x = mesh.positions[vertBase + i].X;
			float y = mesh.positions[vertBase + i].Y;
			if (x < sr.aabbMin[0]) sr.aabbMin[0] = x;
			if (x > sr.aabbMax[0]) sr.aabbMax[0] = x;
			if (y < sr.aabbMin[1]) sr.aabbMin[1] = y;
			if (y > sr.aabbMax[1]) sr.aabbMax[1] = y;
		}
	}

	Builder(FLevelLocals &lvl, levelmesh::FLevelMesh &m) : level(lvl), mesh(m) {}

	//--- region partition ---------------------------------------------------

	void PartitionRegions()
	{
		int nsectors = level.sectors.Size();
		sectorRegion.Resize(nsectors);
		memset(sectorRegion.Data(), -1, sizeof(int32_t) * nsectors);
		int region = 0;
		TArray<int32_t> stack;
		for (int s = 0; s < nsectors; s++)
		{
			if (sectorRegion[s] != -1) continue;
			stack.Clear();
			stack.Push(s);
			sectorRegion[s] = region;
			while (stack.Size() > 0)
			{
				int cur = stack.Pop();
				sector_t &sec = level.sectors[cur];
				for (line_t *line : sec.Lines)
				{
					if (line->sidedef[1] == nullptr) continue;		// one-sided
					if (line->isLinePortal()) continue;			// portal boundary
					sector_t *back = line->backsector;
					if (back == nullptr) continue;
					int bi = back - &level.sectors[0];
					if (sectorRegion[bi] == -1)
					{
						sectorRegion[bi] = region;
						stack.Push(bi);
					}
				}
			}
			region++;
		}
		mesh.regions.Resize(region);
		regionVertexOffset.Resize(region);
		for (int r = 0; r < region; r++)
		{
			LevelMeshRegion &reg = mesh.regions[r];
			memset(&reg, 0, sizeof(reg));
			reg.transform[0] = reg.transform[5] = reg.transform[10] = reg.transform[15] = 1.0f;
			reg.frontFace = 0;
		}
	}

	int SectorRegion(const sector_t *sec) const
	{
		int idx = sec - &level.sectors[0];
		return sectorRegion[idx];
	}

	//--- emission -----------------------------------------------------------

	// z is the real (build-time) height for the raytrace adapter's positions;
	// slotA differs from it on load-dynamic surfaces (offset from the plane).
	uint32_t EmitVertex(float x, float y, float z, float slotA, float slotB, float u,
		float lmu, float lmv, uint32_t surfaceIndex)
	{
		LevelMeshVertex v;
		v.x = x; v.y = y;
		v.slotA = slotA; v.slotB = slotB;
		v.u = u;
		v.lmu = lmu; v.lmv = lmv;
		v.surfaceIndex = surfaceIndex;
		uint32_t idx = (uint32_t)mesh.vertices.Size();
		mesh.vertices.Push(v);
		mesh.positions.Push(FVector3(x, y, z));
		return idx;
	}

	void BeginRegion(int r)
	{
		m_curRegion = r;
		mesh.regionIBOs.Push(TArray<uint32_t>());
		regionVertexOffset[r] = (uint32_t)mesh.vertices.Size();
	}

	void EndRegion(int r)
	{
		if (m_curRegion < 0) return;
		LevelMeshRegion &reg = mesh.regions[r];
		reg.vertexOffset = regionVertexOffset[r];
		reg.vertexCount = (uint32_t)mesh.vertices.Size() - regionVertexOffset[r];
		reg.iboOffset = 0;
		reg.iboCount = (uint32_t)mesh.regionIBOs[r].Size();
	}

	void SwitchRegion(int r)
	{
		if (r == m_curRegion) return;
		if (m_curRegion >= 0) EndRegion(m_curRegion);
		BeginRegion(r);
	}

	//--- u/v bake -----------------------------------------------------------

	//--- wall coordinates ---------------------------------------------------
	//
	// Verbatim ports of HWWall::SetWallCoordinates, CheckTexturePosition,
	// DoTexture and DoMidTexture (hw_walls.cpp). They fill a WallQuad - the
	// subset of the classic HWWall state those functions produce (glseg,
	// ztop/zbottom, tcs, lightuv, the clamp flags) - which EmitWall turns into
	// one quad. Corner order is the classic tcs[] order.

	enum { LOLFT = 0, UPLFT = 1, UPRGT = 2, LORGT = 3 };

	struct WallQuad
	{
		FTexCoordInfo tci;
		float x1, y1, x2, y2;		// glseg
		float fracleft, fracright;	// glseg fractions along the line
		float ztop[2], zbottom[2];
		float u[4], v[4];			// tcs
		float lu[4], lv[4];			// lightuv
		float lindex;			// lightmap page, -1 = none
		uint32_t clamp;			// HWF_CLAMPX / HWF_CLAMPY (= LMDS_CLAMPX / LMDS_CLAMPY)
		// Texture anchor for the load-dynamic vertex warp: the classic
		// texturetop is always one plane's TexZ plus a constant (the pegged
		// DoTexture forms collapse: flh and v_offset cancel); anchorSec/Plane
		// name that plane, texturetop is the value SetWallCoordinates used.
		const sector_t *anchorSec;
		int anchorPlane;
		float texturetop;
		bool noSplit;			// HWF_NOSPLIT (same-sector mid texture)
		side_t *side;			// for the seamless upper/lower splits (segs/sidefrac)
		vertex_t *vert[2];		// classic vertexes[]: the line's vertices (left/right splits)
	};

	// GetTexCoordInfo (hw_walls.cpp, static).
	void GetTexCoordInfo(FGameTexture *tex, FTexCoordInfo *tci, side_t *side, int texpos) const
	{
		tci->GetFromTexture(tex, (float)side->GetTextureXScale(texpos), (float)side->GetTextureYScale(texpos),
			!!(level.flags3 & LEVEL3_FORCEWORLDPANNING));
	}

	// glseg setup of HWWall::Process: non-polyobject walls span the whole line
	// (BuildWall passes a proxy seg with the line's vertices, ordered for this
	// side); polyobject sides go per seg with the major-axis line fraction.
	void InitGlSeg(WallQuad &w, seg_t *seg)
	{
		w.x1 = (float)seg->v1->fX(); w.y1 = (float)seg->v1->fY();
		w.x2 = (float)seg->v2->fX(); w.y2 = (float)seg->v2->fY();
		w.fracleft = 0.f;
		w.fracright = 1.f;
		w.clamp = 0;
		w.anchorSec = seg->frontsector;
		w.anchorPlane = sector_t::ceiling;
		w.texturetop = 0.f;
		w.noSplit = false;
		w.side = seg->sidedef;
		w.vert[0] = seg->v1;
		w.vert[1] = seg->v2;
		// HWWall::Process: refresh the vertex height lists before splitting.
		if (gl_seamless)
		{
			if (seg->v1->dirty) seg->v1->RecalcVertexHeights();
			if (seg->v2->dirty) seg->v2->RecalcVertexHeights();
		}
		line_t *ln = seg->linedef;
		if (ln != nullptr && seg->sidedef != nullptr && (seg->sidedef->Flags & WALLF_POLYOBJ))
		{
			vertex_t *lv1 = (seg->sidedef == ln->sidedef[0]) ? ln->v1 : ln->v2;
			vertex_t *lv2 = (seg->sidedef == ln->sidedef[0]) ? ln->v2 : ln->v1;
			if (fabs(lv1->fX() - lv2->fX()) > fabs(lv1->fY() - lv2->fY()))
			{
				w.fracleft = (float)((seg->v1->fX() - lv1->fX()) / (lv2->fX() - lv1->fX()));
				w.fracright = (float)((seg->v2->fX() - lv1->fX()) / float(lv2->fX() - lv1->fX()));
			}
			else
			{
				w.fracleft = (float)((seg->v1->fY() - lv1->fY()) / (lv2->fY() - lv1->fY()));
				w.fracright = (float)((seg->v2->fY() - lv1->fY()) / (lv2->fY() - lv1->fY()));
			}
		}
	}

	// HWWall::SetWallCoordinates. `tex` may be null (untextured quad).
	void SetWallCoordinates(WallQuad &w, side_t *side, FGameTexture *tex, LightmapSurface *lightmap,
		float texturetop, float topleft, float topright, float bottomleft, float bottomright, float t_ofs, float skew)
	{
		static const float zeroLightUV[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
		const FTexCoordInfo *tci = tex ? &w.tci : nullptr;
		w.texturetop = texturetop;
		float l_ul = 0, texlength = 0;
		if (tci)
		{
			float length = (float)side->TexelLength;
			l_ul = tci->FloatToTexU(tci->TextureOffset(t_ofs));
			texlength = tci->FloatToTexU(length);
		}

		const float *src;
		if (lightmap && lightmap->Type != ST_NULL)
		{
			src = (const float *)lightmap->TexCoords;
			w.lindex = (float)lightmap->LightmapNum;
		}
		else
		{
			src = zeroLightUV;
			w.lindex = -1.0f;
		}
		auto su = [&](int c) { return src[c * 2]; };
		auto sv = [&](int c) { return src[c * 2 + 1]; };

		// left side
		if (topleft >= bottomleft)
		{
			w.ztop[0] = topleft;
			w.zbottom[0] = bottomleft;
			if (tci)
			{
				w.v[UPLFT] = tci->FloatToTexV(-w.ztop[0] + texturetop);
				w.v[LOLFT] = tci->FloatToTexV(-w.zbottom[0] + texturetop);
			}
			w.lv[UPLFT] = sv(UPLFT);
			w.lv[LOLFT] = sv(LOLFT);
		}
		else
		{
			// ceiling below floor - clip to the visible part of the wall
			float dch = topright - topleft;
			float dfh = bottomright - bottomleft;
			float inter_x = (bottomleft - topleft) / (dch - dfh);
			float inter_y = topleft + inter_x * dch;
			w.x1 = w.x1 + inter_x * (w.x2 - w.x1);
			w.y1 = w.y1 + inter_x * (w.y2 - w.y1);
			w.fracleft = inter_x;
			w.zbottom[0] = w.ztop[0] = inter_y;
			if (tci) w.v[LOLFT] = w.v[UPLFT] = tci->FloatToTexV(-w.ztop[0] + texturetop);
			w.lv[UPLFT] = sv(UPLFT) + inter_x * (sv(UPRGT) - sv(UPLFT));
			w.lv[LOLFT] = sv(LOLFT) + inter_x * (sv(LORGT) - sv(LOLFT));
		}

		// right side
		if (topright >= bottomright)
		{
			w.ztop[1] = topright;
			w.zbottom[1] = bottomright;
			if (tci)
			{
				w.v[UPRGT] = tci->FloatToTexV(-w.ztop[1] + texturetop + skew);
				w.v[LORGT] = tci->FloatToTexV(-w.zbottom[1] + texturetop + skew);
			}
			w.lv[UPRGT] = sv(UPRGT);
			w.lv[LORGT] = sv(LORGT);
		}
		else
		{
			float dch = topright - topleft;
			float dfh = bottomright - bottomleft;
			float inter_x = (bottomleft - topleft) / (dch - dfh);
			float inter_y = topleft + inter_x * dch;
			w.x2 = w.x1 + inter_x * (w.x2 - w.x1);
			w.y2 = w.y1 + inter_x * (w.y2 - w.y1);
			w.fracright = inter_x;
			w.zbottom[1] = w.ztop[1] = inter_y;
			if (tci) w.v[LORGT] = w.v[UPRGT] = tci->FloatToTexV(-w.ztop[1] + texturetop + skew);
			w.lv[UPRGT] = sv(UPRGT) + inter_x * (sv(UPRGT) - sv(UPLFT));
			w.lv[LORGT] = sv(LORGT) + inter_x * (sv(LORGT) - sv(LOLFT));
		}

		w.u[UPLFT] = w.u[LOLFT] = l_ul + texlength * w.fracleft;
		w.u[UPRGT] = w.u[LORGT] = l_ul + texlength * w.fracright;
		w.lu[UPLFT] = su(UPLFT) + (su(UPRGT) - su(UPLFT)) * w.fracleft;
		w.lu[LOLFT] = su(LOLFT) + (su(LORGT) - su(LOLFT)) * w.fracleft;
		w.lu[UPRGT] = su(UPLFT) + (su(UPRGT) - su(UPLFT)) * w.fracright;
		w.lu[LORGT] = su(LOLFT) + (su(LORGT) - su(LOLFT)) * w.fracright;

		if (tex)
		{
			bool normalize = false;
			if (tex->isHardwareCanvas()) normalize = true;
			else if (w.clamp & LMDS_CLAMPY)
			{
				// for negative scales we can get negative coordinates here.
				normalize = (w.v[UPLFT] > w.v[LOLFT] || w.v[UPRGT] > w.v[LORGT]);
			}
			if (normalize)
			{
				for (int c = 0; c < 4; c++) w.v[c] += 1.f;
			}
		}
	}

	// HWWall::CheckTexturePosition.
	void CheckTexturePosition(WallQuad &w, seg_t *seg, FGameTexture *tex)
	{
		if (tex->isHardwareCanvas()) return;
		float sub;
		if (w.tci.mScale.Y > 0)
		{
			sub = (w.v[UPLFT] < w.v[UPRGT]) ? float(RoundDown(w.v[UPLFT])) : float(RoundDown(w.v[UPRGT]));
			for (int c = 0; c < 4; c++) w.v[c] -= sub;
			if ((w.v[UPLFT] == 0.f && w.v[UPRGT] == 0.f && w.v[LOLFT] <= 1.f && w.v[LORGT] <= 1.f) ||
				(w.v[UPLFT] >= 0.f && w.v[UPRGT] >= 0.f && w.v[LOLFT] == 1.f && w.v[LORGT] == 1.f))
			{
				w.clamp |= LMDS_CLAMPY;
			}
		}
		else
		{
			sub = (w.v[LOLFT] < w.v[LORGT]) ? float(RoundDown(w.v[LOLFT])) : float(RoundDown(w.v[LORGT]));
			for (int c = 0; c < 4; c++) w.v[c] -= sub;
			if ((w.v[LOLFT] == 0.f && w.v[LORGT] == 0.f && w.v[UPLFT] <= 1.f && w.v[UPRGT] <= 1.f) ||
				(w.v[LOLFT] >= 0.f && w.v[LORGT] >= 0.f && w.v[UPLFT] == 1.f && w.v[UPRGT] == 1.f))
			{
				w.clamp |= LMDS_CLAMPY;
			}
		}
		// Skybox-marked front sectors: the same for x (frontsector only, as classic).
		if (seg->frontsector->special == GLSector_Skybox)
		{
			sub = (float)RoundDown(w.u[UPLFT]);
			for (int c = 0; c < 4; c++) w.u[c] -= sub;
			if ((w.u[UPLFT] == 0.f && w.u[LOLFT] == 0.f && w.u[UPRGT] <= 1.f && w.u[LORGT] <= 1.f) ||
				(w.u[UPLFT] >= 0.f && w.u[LOLFT] >= 0.f && w.u[UPRGT] == 1.f && w.u[LORGT] == 1.f))
			{
				w.clamp |= LMDS_CLAMPX;
			}
		}
	}

	// HWWall::DoTexture (geometry part). renderwall: 0 TOP / 1 M1S / 3 BOTTOM
	// (the classic RENDERWALL_* order, also the side lightmap index).
	// Returns false when nothing is visible.
	// anchorSec/anchorPlane: the plane whose TexZ the effective texturetop
	// rides (see WallQuad), chosen by the caller from the peg state.
	bool DoTexture(WallQuad &w, seg_t *seg, FGameTexture *tex, int renderwall, int peg,
		float ceilingrefheight, float floorrefheight,
		float topleft, float topright, float bottomleft, float bottomright,
		float v_offset, float skew, const sector_t *anchorSec, int anchorPlane)
	{
		if (topleft <= bottomleft && topright <= bottomright) return false;
		w.anchorSec = anchorSec;
		w.anchorPlane = anchorPlane;
		side_t *side = seg->sidedef;
		const float flh = ceilingrefheight - floorrefheight;
		const int texpos = renderwall == 0 ? side_t::top : renderwall == 3 ? side_t::bottom : side_t::mid;
		GetTexCoordInfo(tex, &w.tci, side, texpos);
		LightmapSurface *lightmap = side->lightmap ? &side->lightmap[renderwall] : nullptr;
		float floatceilingref = ceilingrefheight + w.tci.RowOffset((float)side->GetTextureYOffset(texpos));
		if (peg) floatceilingref += w.tci.mRenderHeight - flh - v_offset;
		SetWallCoordinates(w, side, tex, lightmap, floatceilingref, topleft, topright, bottomleft, bottomright,
			(float)side->GetTextureXOffset(texpos), skew);
		CheckTexturePosition(w, seg, tex);
		return true;
	}

	// HWWall::DoMidTexture for a two-sided line, without the fog boundary sheet,
	// translucency and the GetAreas vertical splitting (see the ticket gap list).
	// front == realfront and back == realback (no fake flats yet), zalign = 0.
	// Returns false when nothing is visible; texClampY reports the M2S
	// TM_CLAMPY texture clamp.
	bool DoMidTexture(WallQuad &w, seg_t *seg, FGameTexture *tex, sector_t *front, sector_t *back,
		float fch1, float fch2, float ffh1, float ffh2,
		float bch1, float bch2, float bfh1, float bfh2, float skew, bool &texClampY)
	{
		side_t *side = seg->sidedef;
		const bool wrap = (seg->linedef->flags & ML_WRAP_MIDTEX) || (side->Flags & WALLF_WRAP_MIDTEX);
		bool mirrory = false;
		float texturetop, texturebottom;
		float topleft, topright, bottomleft, bottomright;
		texClampY = false;

		GetTexCoordInfo(tex, &w.tci, side, side_t::mid);
		if (w.tci.mRenderHeight < 0)
		{
			mirrory = true;
			w.tci.mRenderHeight = -w.tci.mRenderHeight;
			w.tci.mScale.Y = -w.tci.mScale.Y;
		}
		const float rowoffset = w.tci.RowOffset((float)side->GetTextureYOffset(side_t::mid));
		if ((seg->linedef->flags & ML_DONTPEGBOTTOM) > 0)
		{
			texturebottom = (float)max(front->GetPlaneTexZ(sector_t::floor), back->GetPlaneTexZ(sector_t::floor)) + rowoffset;
			texturetop = texturebottom + w.tci.mRenderHeight;
			w.anchorSec = front->GetPlaneTexZ(sector_t::floor) >= back->GetPlaneTexZ(sector_t::floor) ? front : back;
			w.anchorPlane = sector_t::floor;
		}
		else
		{
			texturetop = (float)min(front->GetPlaneTexZ(sector_t::ceiling), back->GetPlaneTexZ(sector_t::ceiling)) + rowoffset;
			texturebottom = texturetop - w.tci.mRenderHeight;
			w.anchorSec = front->GetPlaneTexZ(sector_t::ceiling) <= back->GetPlaneTexZ(sector_t::ceiling) ? front : back;
			w.anchorPlane = sector_t::ceiling;
		}

		if (front != back || wrap || front->GetHeightSec())
		{
			// top
			FGameTexture *ttex = TexMan.GetGameTexture(side->GetTexture(side_t::top), true);
			if (!ttex || !ttex->isValid())
			{
				if (front->GetTexture(sector_t::ceiling) == skyflatnum &&
					back->GetTexture(sector_t::ceiling) == skyflatnum && !wrap && skew == 0)
				{
					// intra-sky lines do not clip the texture at all if there's no upper texture.
					topleft = topright = texturetop;
				}
				else
				{
					topleft = max(bch1, fch1);
					topright = max(bch2, fch2);
				}
			}
			else if ((bch1 > fch1 || bch2 > fch2) &&
				(seg->frontsector->GetTexture(sector_t::ceiling) != skyflatnum || seg->backsector->GetTexture(sector_t::ceiling) == skyflatnum))
			{
				topleft = bch1;
				topright = bch2;
			}
			else
			{
				topleft = min(bch1, fch1);
				topright = min(bch2, fch2);
			}

			// bottom
			FGameTexture *btex = TexMan.GetGameTexture(side->GetTexture(side_t::bottom), true);
			if (!btex || !btex->isValid())
			{
				bottomleft = min(bfh1, ffh1);
				bottomright = min(bfh2, ffh2);
			}
			else if (bfh1 < ffh1 || bfh2 < ffh2)
			{
				bottomleft = bfh1;
				bottomright = bfh2;
			}
			else
			{
				bottomleft = max(bfh1, ffh1);
				bottomright = max(bfh2, ffh2);
			}

			// no fog sheet: clip away the parts outside the texture
			if (!wrap && skew == 0)
			{
				if (texturetop < topleft && texturetop < topright) topleft = topright = texturetop;
				if (texturebottom > bottomleft && texturebottom > bottomright) bottomleft = bottomright = texturebottom;
			}
		}
		else
		{
			// same sector on both sides and no Transfer_Heights: no plane clipping
			topleft = texturetop;
			topright = texturetop + skew;
			bottomleft = texturebottom;
			bottomright = texturebottom + skew;
		}

		if (topleft <= bottomleft && topright <= bottomright) return false;

		float t_ofs = (float)side->GetTextureXOffset(side_t::mid);
		// adjust the offset so the line's left edge is inside [0..1]
		float texwidth = w.tci.TextureAdjustWidth();
		if (t_ofs >= 0)
		{
			float div = t_ofs / texwidth;
			t_ofs = (div - RoundDown(div)) * texwidth;
		}
		else
		{
			float div = (-t_ofs) / texwidth;
			t_ofs = texwidth - (div - RoundDown(div)) * texwidth;
		}
		// horizontal clamping when the line stays within one texture repeat
		float textureoffset = w.tci.TextureOffset(t_ofs);
		int righttex = int(textureoffset) + side->TexelLength;
		if ((textureoffset == 0 && righttex <= w.tci.mRenderWidth) ||
			(textureoffset >= 0 && righttex == w.tci.mRenderWidth))
		{
			w.clamp |= LMDS_CLAMPX;
		}
		if (!wrap) w.clamp |= LMDS_CLAMPY;
		// we don't need to do vertex splits if a line has both sides in the same sector
		if (seg->frontsector == seg->backsector) w.noSplit = true;
		if (mirrory)
		{
			w.tci.mRenderHeight = -w.tci.mRenderHeight;
			w.tci.mScale.Y = -w.tci.mScale.Y;
		}
		LightmapSurface *lightmap = side->lightmap ? &side->lightmap[2] : nullptr;	// RENDERWALL_M2S
		SetWallCoordinates(w, side, tex, lightmap, texturetop, topleft, topright, bottomleft, bottomright, t_ofs, skew);
		texClampY = (w.clamp & LMDS_CLAMPY) && !tex->isWarped();
		return true;
	}

	// The classic per-tier skew selection (HWWall::Process): the side's
	// per-tier skew flag, else the topskew/midskew/bottomskew cvar default.
	static float WallSkew(side_t *side, int texpos, int cvarDefault,
		float fch1, float fch2, float ffh1, float ffh2, float bch1, float bch2, float bfh1, float bfh2)
	{
		int skewflag = side->textures[texpos].skew;
		if (skewflag == 0) skewflag = cvarDefault;
		switch (skewflag)
		{
		default: return 0;
		case side_t::skew_front_ceiling: return fch2 - fch1;
		case side_t::skew_back_ceiling: return bch2 - bch1;
		case side_t::skew_front_floor: return ffh2 - ffh1;
		case side_t::skew_back_floor: return bfh2 - bfh1;
		}
	}

	//--- light bake ---------------------------------------------------------

	// Stores the STATIC ingredients of side_t::GetLightLevel (p_sectors.cpp);
	// the VS evaluates it against the live sector light every frame (spec
	// section 6 port list items 2-4), so light thinkers (pulse/strobe/flicker)
	// reach static walls. The fake-contrast rel depends only on the line delta
	// and the level's WallHorizLight/WallVertLight, so both variants are baked;
	// the VS picks one with the runtime smooth-lighting / r_fakecontrast state.
	void BakeLight(LevelMeshSurface &s, side_t *side, int which, int /*orglightlevel*/)
	{
		s.light = (int16_t)side->Light;
		s.tierLight = (int16_t)side->TierLights[which];
		if (side->Flags & WALLF_ABSLIGHTING) s.flags |= LMF_SIDE_ABSLIGHT;
		if (side->Flags & (WALLF_ABSLIGHTING_TIER << which)) s.flags |= LMF_SIDE_ABSTIER;
		if (side->Flags & WALLF_NOFAKECONTRAST) s.flags |= LMF_SIDE_NOFAKECONTRAST;
		if (side->Flags & WALLF_SMOOTHLIGHTING) s.flags |= LMF_SIDE_SMOOTHLIGHT;
		if (side->Flags & WALLF_LIGHT_FOG) s.flags |= LMF_SIDE_LIGHTFOG;

		const DVector2 delta = side->linedef->Delta();
		const int horiz = level.WallHorizLight, vert = level.WallVertLight;
		const int nonSmooth = delta.X == 0 ? vert : delta.Y == 0 ? horiz : 0;
		const int smooth = delta.X != 0
			? RoundHalfUp(horiz + fabs(atan(delta.Y / delta.X) / 1.57079) * (vert - horiz))
			: nonSmooth;
		s.relSmooth = (int16_t)smooth;
		s.relNonSmooth = (int16_t)nonSmooth;
	}

	//--- surface record -----------------------------------------------------

	// planeRefs: 4 sector/plane refs. lightRef: resolved effective-light sector/slot.
	// texId is the UNTRANSLATED texture id (side/sector GetTexture): the draw
	// pass resolves texture animation per group at draw time (classic
	// GetGameTexture(id, true) per draw), so it must not be baked here. tex is
	// the resolved (current-frame) texture, used only for validity/dimensions.
	uint32_t AddSurface(FGameTexture *tex, FTextureID texId, int r, uint32_t flags,
		const uint32_t planeRefs[4], uint32_t lightRef, uint32_t drawState = 0)
	{
		LevelMeshSurface s;
		memset(&s, 0, sizeof(s));
		s.textureIndex = tex ? (uint32_t)texId.GetIndex() : 0xFFFFFFFFu;
		s.regionIndex = (uint32_t)r;
		for (int i = 0; i < 4; i++) s.planeRef[i] = planeRefs[i];
		s.lightRef = lightRef;
		s.flags = flags;
		s.lightlistRef = -1;
		uint32_t idx = (uint32_t)mesh.surfaces.Size();
		mesh.surfaces.Push(s);
		// parallel sub-range record (AABB + ibo range) in build order; the
		// per-region texRangeOffset/Count is finalized in BuildSubRanges.
		LevelMeshSubRange sr;
		memset(&sr, 0, sizeof(sr));
		sr.textureIndex = s.textureIndex;
		sr.iboOffset = (uint32_t)mesh.regionIBOs[r].Size();
		sr.bandIndex = 0;
		sr.drawState = drawState;
		sr.aabbMin[0] = sr.aabbMin[1] = 1e30f;
		sr.aabbMax[0] = sr.aabbMax[1] = -1e30f;
		mesh.subRanges.Push(sr);
		return idx;
	}

	//--- wall emission ------------------------------------------------------

	// Live height of a planeRef slot's plane at (x, y).
	float PlaneRefZ(uint32_t planeRef, float x, float y) const
	{
		sector_t &sec = level.sectors[planeRef >> 1];
		return (float)sec.GetSecPlane((planeRef & 1) ? sector_t::ceiling : sector_t::floor).ZatPoint(x, y);
	}

	// Emit one wall quad from a WallQuad. Emission slots k0=LOLFT k1=UPLFT
	// k2=LORGT k3=UPRGT (HWWall::CreateVertices positions: LOLFT/UPLFT at
	// glseg (x1,y1), UPRGT/LORGT at (x2,y2)); the triangles follow the classic
	// fan LOLFT, UPLFT, UPRGT, LORGT so the diagonal runs LOLFT-UPRGT.
	// Lightmap uv and page go with each corner (vLightmap).
	// Load-dynamic (dyn != nullptr): slotA = z - live plane z of the corner's
	// tracked slot, slotB = v - (texturetop - z) * k (levelmesh.vp warp).
	struct WallDyn { int zslot[4]; float k; };	// zslot by classic corner
	uint32_t EmitWall(const WallQuad &w, uint32_t surfaceIndex, const WallDyn *dyn, const uint32_t planeRefs[4])
	{
		static const int corner[4] = { LOLFT, UPLFT, LORGT, UPRGT };
		uint32_t base = (uint32_t)mesh.vertices.Size();
		uint32_t iboOff = (uint32_t)mesh.regionIBOs[m_curRegion].Size();
		const bool split = gl_seamless && !w.noSplit && w.side != nullptr && !(w.side->Flags & WALLF_POLYOBJ);
		if (dyn || !split)
		{
			for (int k = 0; k < 4; k++)
			{
				const int c = corner[k];
				const bool left = (c == LOLFT || c == UPLFT);
				const bool top = (c == UPLFT || c == UPRGT);
				const float x = left ? w.x1 : w.x2;
				const float y = left ? w.y1 : w.y2;
				const float z = top ? w.ztop[left ? 0 : 1] : w.zbottom[left ? 0 : 1];
				float slotA = z, slotB = w.v[c];
				if (dyn)
				{
					slotA = z - PlaneRefZ(planeRefs[dyn->zslot[c]], x, y);
					slotB = w.v[c] - (w.texturetop - z) * dyn->k;
				}
				EmitVertex(x, y, z, slotA, slotB, w.u[c], w.lu[c], w.lv[c], surfaceIndex);
			}
			mesh.regionIBOs[m_curRegion].Push(base + 0);
			mesh.regionIBOs[m_curRegion].Push(base + 1);
			mesh.regionIBOs[m_curRegion].Push(base + 3);
			mesh.regionIBOs[m_curRegion].Push(base + 0);
			mesh.regionIBOs[m_curRegion].Push(base + 3);
			mesh.regionIBOs[m_curRegion].Push(base + 2);
			mesh.subRanges[surfaceIndex].iboCount = 6;
			RecordSurfaceVerts(surfaceIndex, base, 4);
			return iboOff;
		}

		// gl_seamless (static walls): HWWall::CreateVertices with its edge
		// splits (hw_walls_vertex.cpp) - T-junction vertices at the heights of
		// all sector planes meeting at the line's vertices (left/right edges)
		// and at the side's seg split points (upper/lower edges), drawn as one
		// triangle fan from LOLFT. Load-dynamic walls keep the 4-corner warp.
		auto put = [&](float x, float y, float z, float u, float v, float lu, float lv)
		{
			EmitVertex(x, y, z, z, v, u, lu, lv, surfaceIndex);
		};
		side_t *sidedef = w.side;
		const float polyw = w.fracright - w.fracleft;

		put(w.x1, w.y1, w.zbottom[0], w.u[LOLFT], w.v[LOLFT], w.lu[LOLFT], w.lv[LOLFT]);
		// SplitLeftEdge
		if (w.fracleft == 0 && w.vert[0] != nullptr && w.vert[0]->numheights)
		{
			vertex_t *vi = w.vert[0];
			float polyh1 = w.ztop[0] - w.zbottom[0];
			float factv1 = polyh1 ? (w.v[UPLFT] - w.v[LOLFT]) / polyh1 : 0;
			float factu1 = polyh1 ? (w.u[UPLFT] - w.u[LOLFT]) / polyh1 : 0;
			float factlv1 = polyh1 ? (w.lv[UPLFT] - w.lv[LOLFT]) / polyh1 : 0;
			float factlu1 = polyh1 ? (w.lu[UPLFT] - w.lu[LOLFT]) / polyh1 : 0;
			int i = 0;
			while (i < vi->numheights && vi->heightlist[i] <= w.zbottom[0]) i++;
			while (i < vi->numheights && vi->heightlist[i] < w.ztop[0])
			{
				float h = vi->heightlist[i];
				put(w.x1, w.y1, h, factu1 * (h - w.ztop[0]) + w.u[UPLFT], factv1 * (h - w.ztop[0]) + w.v[UPLFT],
					factlu1 * (h - w.ztop[0]) + w.lu[UPLFT], factlv1 * (h - w.ztop[0]) + w.lv[UPLFT]);
				i++;
			}
		}
		put(w.x1, w.y1, w.ztop[0], w.u[UPLFT], w.v[UPLFT], w.lu[UPLFT], w.lv[UPLFT]);
		// SplitUpperEdge
		{
			float facu = (w.u[UPRGT] - w.u[UPLFT]) / polyw;
			float facv = (w.v[UPRGT] - w.v[UPLFT]) / polyw;
			float fact = (w.ztop[1] - w.ztop[0]) / polyw;
			float faclu = (w.lu[UPRGT] - w.lu[UPLFT]) / polyw;
			float faclv = (w.lv[UPRGT] - w.lv[UPLFT]) / polyw;
			for (int i = 0; i < sidedef->numsegs - 1; i++)
			{
				seg_t *cseg = sidedef->segs[i];
				float sidefrac = cseg->sidefrac;
				if (sidefrac <= w.fracleft) continue;
				if (sidefrac >= w.fracright) break;
				float fracfac = sidefrac - w.fracleft;
				put((float)cseg->v2->fX(), (float)cseg->v2->fY(), w.ztop[0] + fact * fracfac,
					w.u[UPLFT] + facu * fracfac, w.v[UPLFT] + facv * fracfac,
					w.lu[UPLFT] + faclu * fracfac, w.lv[UPLFT] + faclv * fracfac);
			}
		}
		put(w.x2, w.y2, w.ztop[1], w.u[UPRGT], w.v[UPRGT], w.lu[UPRGT], w.lv[UPRGT]);
		// SplitRightEdge
		if (w.fracright == 1 && w.vert[1] != nullptr && w.vert[1]->numheights)
		{
			vertex_t *vi = w.vert[1];
			int i = vi->numheights - 1;
			float polyh2 = w.ztop[1] - w.zbottom[1];
			float factv2 = polyh2 ? (w.v[UPRGT] - w.v[LORGT]) / polyh2 : 0;
			float factu2 = polyh2 ? (w.u[UPRGT] - w.u[LORGT]) / polyh2 : 0;
			float factlv2 = polyh2 ? (w.lv[UPRGT] - w.lv[LORGT]) / polyh2 : 0;
			float factlu2 = polyh2 ? (w.lu[UPRGT] - w.lu[LORGT]) / polyh2 : 0;
			while (i > 0 && vi->heightlist[i] >= w.ztop[1]) i--;
			while (i > 0 && vi->heightlist[i] > w.zbottom[1])
			{
				float h = vi->heightlist[i];
				put(w.x2, w.y2, h, factu2 * (h - w.ztop[1]) + w.u[UPRGT], factv2 * (h - w.ztop[1]) + w.v[UPRGT],
					factlu2 * (h - w.ztop[1]) + w.lu[UPRGT], factlv2 * (h - w.ztop[1]) + w.lv[UPRGT]);
				i--;
			}
		}
		put(w.x2, w.y2, w.zbottom[1], w.u[LORGT], w.v[LORGT], w.lu[LORGT], w.lv[LORGT]);
		// SplitLowerEdge
		if (sidedef->numsegs > 1)
		{
			float facu = (w.u[LORGT] - w.u[LOLFT]) / polyw;
			float facv = (w.v[LORGT] - w.v[LOLFT]) / polyw;
			float facb = (w.zbottom[1] - w.zbottom[0]) / polyw;
			float faclu = (w.lu[LORGT] - w.lu[LOLFT]) / polyw;
			float faclv = (w.lv[LORGT] - w.lv[LOLFT]) / polyw;
			for (int i = sidedef->numsegs - 2; i >= 0; i--)
			{
				seg_t *cseg = sidedef->segs[i];
				float sidefrac = cseg->sidefrac;
				if (sidefrac >= w.fracright) continue;
				if (sidefrac <= w.fracleft) break;
				float fracfac = sidefrac - w.fracleft;
				put((float)cseg->v2->fX(), (float)cseg->v2->fY(), w.zbottom[0] + facb * fracfac,
					w.u[LOLFT] + facu * fracfac, w.v[LOLFT] + facv * fracfac,
					w.lu[LOLFT] + faclu * fracfac, w.lv[LOLFT] + faclv * fracfac);
			}
		}
		const uint32_t n = (uint32_t)mesh.vertices.Size() - base;
		for (uint32_t k = 2; k < n; k++)	// DT_TriangleFan
		{
			mesh.regionIBOs[m_curRegion].Push(base);
			mesh.regionIBOs[m_curRegion].Push(base + k - 1);
			mesh.regionIBOs[m_curRegion].Push(base + k);
		}
		mesh.subRanges[surfaceIndex].iboCount = 3 * (n - 2);
		RecordSurfaceVerts(surfaceIndex, base, n);
		return iboOff;
	}

	// A sector plane is load-dynamic if it is translucent (alpha < 1), or the
	// sector is a transdoor, or the plane is scrolling (non-zero scroll offset at
	// load). These surfaces get their vertex slotA written as a vparam at load
	// (LMF_LOADDYN) so the vertex shader evaluates z from the sector state ring.
	// NOTE: plane alpha defaults to 1.0 at map load (maploader.cpp, udmf.cpp);
	// hw_flats.cpp's `alpha != 0` is its "plane drawn at all" gate, not a
	// translucency test. Testing `alpha > 0` here marked every surface dynamic
	// and collapsed every wall to a zero-height quad (ticket 03, P1).
	static bool IsLoadDynamic(sector_t *sec, int plane)
	{
		if (sec == nullptr) return false;
		if (sec->GetAlpha(plane) < 1.) return true;
		if (sec->transdoor) return true;
		const FTransform &xf = sec->planes[plane].xform;
		if (xf.xOffs != 0 || xf.yOffs != 0) return true;
		return false;
	}

	//--- wall build ---------------------------------------------------------

	// Create the surface record for one wall quad and emit it.
	// renderwall: classic RENDERWALL index (0 TOP / 1 M1S / 2 M2S / 3 BOTTOM),
	// which is also the side lightmap index; texpos: the side tier for light.
	// topPref / bottomPref: planeRefs slots a top / bottom edge may track, in
	// priority order (-1 = unused); for load-dynamic surfaces each corner
	// tracks the first listed slot whose plane passes through it, else the
	// texture anchor (a texture-clipped edge rides the anchor plane).
	void AddWallSurface(const WallQuad &w, FGameTexture *tex, FTextureID texId, int r, side_t *side,
		int renderwall, int texpos, uint32_t extraFlags, bool dyn, const uint32_t planeRefs[4],
		uint32_t lightRef, int orglightlevel, const int topPref[2], const int bottomPref[2], uint32_t drawState)
	{
		uint32_t flags = extraFlags | ((uint32_t)texpos << 8);
		if (side->lightmap && side->lightmap[renderwall].Type != ST_NULL) flags |= LMF_LIGHTMAP;
		if (side->Flags & WALLF_POLYOBJ) flags |= LMF_POLYOBJ;
		if (dyn) flags |= LMF_LOADDYN;
		uint32_t surf = AddSurface(tex, texId, r, flags, planeRefs, lightRef, drawState);
		BakeLight(mesh.surfaces[surf], side, texpos, orglightlevel);
		if (!dyn)
		{
			EmitWall(w, surf, nullptr, planeRefs);
			return;
		}

		const uint32_t anchorRef = LM_PlaneRef(w.anchorSec, w.anchorPlane);
		int anchorSlot = 0;
		for (int i = 0; i < 4; i++) if (planeRefs[i] == anchorRef) { anchorSlot = i; break; }
		WallDyn d;
		d.k = 1.f / w.tci.mRenderHeight;	// FloatToTexV
		int packed = 0;
		for (int c = 0; c < 4; c++)
		{
			const bool left = (c == LOLFT || c == UPLFT);
			const bool top = (c == UPLFT || c == UPRGT);
			const float x = left ? w.x1 : w.x2, y = left ? w.y1 : w.y2;
			const float z = top ? w.ztop[left ? 0 : 1] : w.zbottom[left ? 0 : 1];
			const int *pref = top ? topPref : bottomPref;
			int slot = anchorSlot;
			for (int i = 0; i < 2; i++)
			{
				if (pref[i] >= 0 && fabsf(PlaneRefZ(planeRefs[pref[i]], x, y) - z) < 1.f / 256.f) { slot = pref[i]; break; }
			}
			d.zslot[c] = slot;
		}
		// vparam[0] packs the slot per EMISSION slot k (the VS corner index).
		static const int corner[4] = { LOLFT, UPLFT, LORGT, UPRGT };
		for (int k = 0; k < 4; k++) packed |= d.zslot[corner[k]] << (2 * k);
		LevelMeshSurface &s = mesh.surfaces[surf];
		s.vparam[0] = (float)packed;
		s.vparam[1] = (float)anchorSlot;
		s.vparam[2] = w.texturetop - (float)w.anchorSec->GetPlaneTexZ(w.anchorPlane);
		s.vparam[3] = d.k;
		EmitWall(w, surf, &d, planeRefs);
	}

	// Port of HWWall::Process (hw_walls.cpp) for the static mesh: per line
	// (non-polyobject), the one-sided / two-sided split, the upper / mid /
	// lower tiers through DoTexture / DoMidTexture. Not yet handled (ticket 03
	// gap list): sky walls (SkyNormal/SkyTop/SkyBottom/SkyLine), horizon and
	// line portals (06), the missing upper/lower texture fill, fog boundaries,
	// translucent mid textures, 3D-floor blocks (05, approximated below).
	void BuildWall(seg_t *seg)
	{
		sector_t *frontsector = seg->frontsector;
		sector_t *backsector = seg->backsector;
		if (frontsector == nullptr) return;
		side_t *side = seg->sidedef;
		// Some segs carry a null sidedef or degenerate vertices (e.g. 3D-floor or
		// non-drawable segs); the classic renderer skips them. Guard here.
		if (side == nullptr || seg->v1 == nullptr || seg->v2 == nullptr || seg->linedef == nullptr) return;
		line_t *line = seg->linedef;
		const bool ispoly = (side->Flags & WALLF_POLYOBJ) != 0;
		// Polyobject walls move at runtime and are drawn per frame from the
		// classic polyobject BSP (HWDrawList::DrawPolyobjWalls on the levelmesh
		// path); the static mesh must not hold their load-time segs.
		if (ispoly) return;

		// Classic AddLine: a line with the same sector on both sides and no valid
		// mid texture draws nothing at all.
		if (!ispoly && backsector == frontsector && !line->isVisualPortal())
		{
			FGameTexture *mt = TexMan.GetGameTexture(side->GetTexture(side_t::mid), true);
			if (!mt || !mt->isValid()) return;
		}

		// Classic draws a non-polyobject wall once per LINE from the line's own
		// vertices (hw_bsp.cpp linedef validcount; HWWall::Process sets v1/v2 to
		// the linedef vertices, frac 0..1); only polyobject sides go per seg.
		// The mesh is static and holds both sides, so dedupe per SIDE: the first
		// seg of a side emits through a proxy seg spanning the whole line, later
		// segs of that side emit nothing.
		seg_t lineSeg;
		if (!ispoly)
		{
			const int sideIndex = side->Index();
			if (sideIndex >= 0 && sideIndex < (int)sideEmitted.Size())
			{
				if (sideEmitted[sideIndex]) return;
				sideEmitted[sideIndex] = 1;
			}
			const bool frontSide = (side == line->sidedef[0]);
			lineSeg = *seg;
			lineSeg.v1 = frontSide ? line->v1 : line->v2;
			lineSeg.v2 = frontSide ? line->v2 : line->v1;
			seg = &lineSeg;
		}
		vertex_t *v1 = seg->v1, *v2 = seg->v2;

		int r = SectorRegion(frontsector);
		SwitchRegion(r);

		const int orglightlevel = frontsector->lightlevel;
		const float frefz = (float)frontsector->GetPlaneTexZ(sector_t::floor);
		const float crefz = (float)frontsector->GetPlaneTexZ(sector_t::ceiling);
		const float ffh1 = (float)frontsector->floorplane.ZatPoint(v1);
		const float ffh2 = (float)frontsector->floorplane.ZatPoint(v2);
		const float fch1 = (float)frontsector->ceilingplane.ZatPoint(v1);
		const float fch2 = (float)frontsector->ceilingplane.ZatPoint(v2);

		if (line->special == Line_Horizon) return;	// horizon: sky/portal territory

		const bool isportal = line->isVisualPortal() && side == line->sidedef[0];
		const uint32_t frontLightRef = LM_LightRef(frontsector, LM_LIGHT_SLOT_WALL);

		//--- one-sided (incl. two-sided lines without ML_TWOSIDED / ML_3DMIDTEX) ---
		if (!backsector || (!(line->flags & (ML_TWOSIDED | ML_3DMIDTEX)) && !isportal))
		{
			if (isportal || line->GetTransferredPortal()) return;	// portal / sky line
			FTextureID texId = side->GetTexture(side_t::mid);
			FGameTexture *tex = TexMan.GetGameTexture(texId, true);
			if (tex && tex->isValid())
			{
				// one-sided: only the front skews apply (back heights 0 -> back skews 0)
				float skew = WallSkew(side, side_t::mid, midskew, fch1, fch2, ffh1, ffh2, 0, 0, 0, 0);
				WallQuad w;
				InitGlSeg(w, seg);
				const bool peg = (line->flags & ML_DONTPEGBOTTOM) > 0;
				if (DoTexture(w, seg, tex, 1, peg, crefz, frefz, fch1, fch2, ffh1, ffh2, 0, skew,
					frontsector, peg ? sector_t::floor : sector_t::ceiling))
				{
					bool dyn = IsLoadDynamic(frontsector, sector_t::floor) || IsLoadDynamic(frontsector, sector_t::ceiling);
					uint32_t planeRefs[4] = { LM_PlaneRef(frontsector, sector_t::floor),
						LM_PlaneRef(frontsector, sector_t::ceiling),
						LM_PlaneRef(frontsector, sector_t::floor),
						LM_PlaneRef(frontsector, sector_t::ceiling) };
					static const int topPref[2] = { 1, -1 }, bottomPref[2] = { 0, -1 };
					AddWallSurface(w, tex, texId, r, side, 1, side_t::mid, LMF_OPENING_MIDDLE, dyn, planeRefs,
						frontLightRef, orglightlevel, topPref, bottomPref, w.clamp);
				}
			}
			return;
		}

		//--- two-sided ---
		if (isportal) return;	// line portals: ticket 06

		float bfh1 = (float)backsector->floorplane.ZatPoint(v1);
		float bfh2 = (float)backsector->floorplane.ZatPoint(v2);
		const float bch1 = (float)backsector->ceilingplane.ZatPoint(v1);
		const float bch2 = (float)backsector->ceilingplane.ZatPoint(v2);
		const float bfh1a = bfh1, bfh2a = bfh2;	// unclipped back floor (lower skew_back_floor)

		const bool bothCeilSky = frontsector->GetTexture(sector_t::ceiling) == skyflatnum &&
			backsector->GetTexture(sector_t::ceiling) == skyflatnum;
		const bool bothFloorSky = frontsector->GetTexture(sector_t::floor) == skyflatnum &&
			backsector->GetTexture(sector_t::floor) == skyflatnum;
		const uint32_t twoSidedRefs[4] = { LM_PlaneRef(frontsector, sector_t::floor),
			LM_PlaneRef(frontsector, sector_t::ceiling),
			LM_PlaneRef(backsector, sector_t::floor),
			LM_PlaneRef(backsector, sector_t::ceiling) };
		const float backFloorTexZ = (float)backsector->GetPlaneTexZ(sector_t::floor);
		const float backCeilTexZ = (float)backsector->GetPlaneTexZ(sector_t::ceiling);

		//--- upper texture ---
		if (!bothCeilSky)
		{
			float bch1a = bch1, bch2a = bch2;
			if (!bothFloorSky)
			{
				// the back sector's floor obstructs part of this wall
				if (ffh1 > bch1 && ffh2 > bch2 && (line->flags & ML_DRAWFULLHEIGHT) == 0)
				{
					bch2a = ffh2;
					bch1a = ffh1;
				}
			}
			if (bch1a < fch1 || bch2a < fch2)
			{
				FTextureID texId = side->GetTexture(side_t::top);
				FGameTexture *tex = TexMan.GetGameTexture(texId, true);
				float skew = 0;
				bool draw = false;
				if (tex && tex->isValid())
				{
					skew = WallSkew(side, side_t::top, topskew, fch1, fch2, ffh1, ffh2, bch1, bch2, bfh1, bfh2);
					draw = true;
				}
				else if (!ispoly && (frontsector->ceilingplane.isSlope() || backsector->ceilingplane.isSlope()) &&
					frontsector->GetTexture(sector_t::ceiling) != skyflatnum &&
					backsector->GetTexture(sector_t::ceiling) != skyflatnum)
				{
					// sloped: render the gap with the front ceiling texture
					texId = frontsector->GetTexture(sector_t::ceiling);
					tex = TexMan.GetGameTexture(texId, true);
					draw = tex && tex->isValid();
				}
				// else: AddUpperMissingTexture fill (ticket 03 gap list)
				const bool peg = (line->flags & ML_DONTPEGTOP) == 0;
				WallQuad w;
				InitGlSeg(w, seg);
				if (draw && DoTexture(w, seg, tex, 0, peg, crefz, backCeilTexZ, fch1, fch2, bch1a, bch2a, 0, skew,
					peg ? backsector : frontsector, sector_t::ceiling))
				{
					uint32_t extra = LMF_OPENING_UPPER;
					bool dyn = IsLoadDynamic(frontsector, sector_t::ceiling) || IsLoadDynamic(backsector, sector_t::ceiling);
					static const int topPref[2] = { 1, -1 }, bottomPref[2] = { 3, 0 };
					AddWallSurface(w, tex, texId, r, side, 0, side_t::top, extra, dyn, twoSidedRefs,
						frontLightRef, orglightlevel, topPref, bottomPref, w.clamp);
				}
			}
		}

		//--- mid texture ---
		{
			FTextureID texId = side->GetTexture(side_t::mid);
			FGameTexture *tex = TexMan.GetGameTexture(texId, true);
			if (tex && tex->isValid())
			{
				if (level.i_compatflags & COMPATF_MASKEDMIDTEX)
				{
					FTextureID rawId = TexMan.GetRawTexture(tex->GetID());
					FGameTexture *rawtex = TexMan.GetGameTexture(rawId);
					if (rawtex) { tex = rawtex; texId = rawId; }
				}
				float skew = WallSkew(side, side_t::mid, midskew, fch1, fch2, ffh1, ffh2, bch1, bch2, bfh1, bfh2);
				WallQuad w;
				InitGlSeg(w, seg);
				bool texClampY = false;
				// Translucent mids go to the classic translucent pass, which
				// still runs on the levelmesh path (DoMidTexture alpha block +
				// PutWall's passflag-2 texture translucency): never meshed.
				// DoMidTexture only emits when the side or line carries an alpha.
				const bool hasAlpha = side->HasAlpha() || line->alpha != 0;
				bool translucent = tex->GetTranslucency();
				if (hasAlpha)
				{
					if ((line->flags & ML_ADDTRANS) != 0) translucent = true;
					else if ((side->HasAlpha() ? side->alpha : line->alpha) < 1.) translucent = true;
				}
				if (hasAlpha && !translucent && DoMidTexture(w, seg, tex, frontsector, backsector, fch1, fch2, ffh1, ffh2,
					bch1, bch2, bfh1, bfh2, skew, texClampY))
				{
					bool dyn = IsLoadDynamic(frontsector, sector_t::floor) || IsLoadDynamic(frontsector, sector_t::ceiling);
					static const int topPref[2] = { 3, 1 }, bottomPref[2] = { 2, 0 };
					AddWallSurface(w, tex, texId, r, side, 2, side_t::mid, LMF_OPENING_MIDDLE, dyn, twoSidedRefs,
						frontLightRef, orglightlevel, topPref, bottomPref, w.clamp | (texClampY ? LMDS_TEXCLAMPY : 0) | (tex->isMasked() ? LMDS_MASKED : 0));
				}
			}
		}

		//--- 3D-floor boundary quads (approximation; the BuildFFBlock port is ticket 05) ---
		// Each F3DFloor shared by the front sector but not the back sector gets a
		// middle-wall quad spanning the model sector's floor/ceiling planes.
		TArray<F3DFloor *> &frontff = frontsector->e->XFloor.ffloors;
		TArray<F3DFloor *> &backff = backsector->e->XFloor.ffloors;
		for (unsigned j = 0; j < frontff.Size(); j++)
		{
			F3DFloor *xfloor = frontff[j];
			if (!(xfloor->flags & FF_EXISTS) || !(xfloor->flags & FF_RENDERSIDES)) continue;
			if ((xfloor->flags & FF_INVERTSIDES) == 0) continue;
			if (backff.Contains(xfloor)) continue;
			sector_t *model = xfloor->model;
			if (model == nullptr) continue;
			FTextureID texId = side->GetTexture(side_t::mid);
			FGameTexture *tex = TexMan.GetGameTexture(texId, true);
			if (tex == nullptr || !tex->isValid()) continue;
			WallQuad w;
			InitGlSeg(w, seg);
			if (!DoTexture(w, seg, tex, 1, false,
				(float)model->GetPlaneTexZ(sector_t::ceiling), (float)model->GetPlaneTexZ(sector_t::floor),
				(float)model->ceilingplane.ZatPoint(v1), (float)model->ceilingplane.ZatPoint(v2),
				(float)model->floorplane.ZatPoint(v1), (float)model->floorplane.ZatPoint(v2), 0, 0,
				model, sector_t::ceiling)) continue;
			uint32_t planeRefs[4] = { LM_PlaneRef(model, sector_t::floor),
				LM_PlaneRef(model, sector_t::ceiling),
				LM_PlaneRef(model, sector_t::floor),
				LM_PlaneRef(model, sector_t::ceiling) };
			static const int topPref3d[2] = { 1, -1 }, bottomPref3d[2] = { 0, -1 };
			AddWallSurface(w, tex, texId, r, side, 1, side_t::mid, LMF_3DFLOOR | LMF_OPENING_MIDDLE, false,
				planeRefs, LM_LightRef(model, LM_LIGHT_SLOT_WALL), orglightlevel, topPref3d, bottomPref3d,
				w.clamp | (tex->isMasked() ? LMDS_MASKED : 0));
		}

		//--- bottom texture ---
		// the back sector's ceiling obstructs part of this wall (specially important for sky sectors)
		if (fch1 < bfh1 && fch2 < bfh2 && (line->flags & ML_DRAWFULLHEIGHT) == 0)
		{
			bfh1 = fch1;
			bfh2 = fch2;
		}
		if (bfh1 > ffh1 || bfh2 > ffh2)
		{
			FTextureID texId = side->GetTexture(side_t::bottom);
			FGameTexture *tex = TexMan.GetGameTexture(texId, true);
			float skew = 0;
			float vOffset = bothCeilSky ? frefz - backCeilTexZ : frefz - crefz;
			bool vOffsetBackCeil = bothCeilSky;
			bool draw = false;
			if (tex && tex->isValid())
			{
				skew = WallSkew(side, side_t::bottom, bottomskew, fch1, fch2, ffh1, ffh2, bch1, bch2, bfh1a, bfh2a);
				draw = true;
			}
			else if (!ispoly && (frontsector->floorplane.isSlope() || backsector->floorplane.isSlope()) &&
				frontsector->GetTexture(sector_t::floor) != skyflatnum &&
				backsector->GetTexture(sector_t::floor) != skyflatnum)
			{
				// sloped: render the gap with the front floor texture
				texId = frontsector->GetTexture(sector_t::floor);
				tex = TexMan.GetGameTexture(texId, true);
				draw = tex && tex->isValid();
				vOffset = frefz - crefz;
				vOffsetBackCeil = false;
			}
			// else: AddLowerMissingTexture fill (ticket 03 gap list)
			const bool unpeg = (line->flags & ML_DONTPEGBOTTOM) > 0;
			WallQuad w;
			InitGlSeg(w, seg);
			// anchor: unpegged texturetop = backFloor + row + h - (backFloor - frefz) - vOffset
			// = the ceiling vOffset was taken from (front, or back when both are sky).
			const sector_t *anchor = !unpeg ? backsector : (vOffsetBackCeil ? backsector : frontsector);
			const int anchorPlane = !unpeg ? sector_t::floor : sector_t::ceiling;
			if (draw && DoTexture(w, seg, tex, 3, unpeg, backFloorTexZ, frefz, bfh1, bfh2, ffh1, ffh2, vOffset, skew,
				anchor, anchorPlane))
			{
				uint32_t extra = LMF_OPENING_LOWER;
				if (unpeg) extra |= LMF_UNPEGGEDLOWER;
				bool dyn = IsLoadDynamic(frontsector, sector_t::floor) || IsLoadDynamic(backsector, sector_t::floor);
				static const int topPref[2] = { 2, 1 }, bottomPref[2] = { 0, -1 };
				AddWallSurface(w, tex, texId, r, side, 3, side_t::bottom, extra, dyn, twoSidedRefs,
					frontLightRef, orglightlevel, topPref, bottomPref, w.clamp);
			}
		}
	}

	//--- flat emission ------------------------------------------------------

	// Emit a flat as a fan over the subsector's polygon vertices (firstline[j].v1),
	// fanned from vertex 0. The flat UV is world-space: u = x/64, v = -y/64 (classic).
	// z is evaluated per vertex from the plane so slanted planes (3D-floor model
	// sectors) bake correctly; on horizontal planes this is the constant z.
	// planeSlot: the planeRefs slot of this flat's own plane (0 floor, 1
	// ceiling). Load-dynamic flats store slotA = 0 (offset from that plane,
	// warped by the VS) and keep the baked world-space v.
	void EmitFlatFan(subsector_t *sub, const secplane_t &plane, float lmuv[2], uint32_t surfaceIndex, int planeSlot)
	{
		// Port of CreateVerticesForSubsector + SetFlatVertex (hw_vertexbuilder.cpp):
		// < 3 lines draw nothing; hole-filling subsectors are not necessarily
		// convex and are earcut; everything else is the fan (0, k-1, k).
		// Transdoor floors sit 1 unit low (CreateIndexedSectorVertices diff).
		// Lightmapped subsectors carry per-vertex lightmap uvs (the LM variant).
		const int n = (int)sub->numlines;
		if (n < 3) return;
		const bool dyn = (mesh.surfaces[surfaceIndex].flags & LMF_LOADDYN) != 0;
		if (dyn) mesh.surfaces[surfaceIndex].vparam[0] = (float)planeSlot;
		const float diff = (planeSlot == 0 && sub->sector->transdoor) ? -1.f : 0.f;
		const LightmapSurface *lm = nullptr;
		if (sub->lightmap[planeSlot] != nullptr)
		{
			const LightmapSurface *cand = &sub->lightmap[planeSlot][0];
			if (cand->Type != ST_NULL) lm = cand;
		}
		uint32_t base = (uint32_t)mesh.vertices.Size();
		for (int i = 0; i < n; i++)
		{
			vertex_t *vt = sub->firstline[i].v1;
			float x = (float)vt->fX();
			float y = (float)vt->fY();
			float u = x / 64.f;
			float v = -y / 64.f;
			const float z = (float)plane.ZatPoint(vt) + diff;
			float lu = lmuv[0], lv = lmuv[1];
			if (lm) { lu = lm->TexCoords[i * 2]; lv = lm->TexCoords[i * 2 + 1]; }
			// dynamic: slotA is the offset from the live plane (the transdoor -1)
			EmitVertex(x, y, z, dyn ? diff : z, v, u, lu, lv, surfaceIndex);
		}
		TArray<uint32_t> &ibo = mesh.regionIBOs[m_curRegion];
		const uint32_t start = ibo.Size();
		if ((sub->flags & SSECF_HOLE) && n > 3)
		{
			using Point = std::pair<double, double>;
			std::vector<std::vector<Point>> polygon(1);
			polygon[0].resize(n);
			for (int i = 0; i < n; i++)
				polygon[0][i] = { sub->firstline[i].v1->fX(), sub->firstline[i].v1->fY() };
			auto indices = mapbox::earcut(polygon);
			for (auto vti : indices) ibo.Push(base + (uint32_t)vti);
		}
		else
		{
			for (int k = 2; k < n; k++)
			{
				ibo.Push(base);
				ibo.Push(base + k - 1);
				ibo.Push(base + k);
			}
		}
		// Front-face orientation for back-face culling (LMDS_NOCULL docs):
		// clockwise on screen. Seen from above with map +y up, the screen
		// equals map space, so floors are clockwise in map space (negative
		// signed area); ceilings are seen from below (mirrored), so they are
		// counter-clockwise. Earcut / fan output is normalized per triangle.
		const bool wantNegative = (planeSlot == 0);
		for (uint32_t t = start; t + 2 < ibo.Size(); t += 3)
		{
			const LevelMeshVertex &a = mesh.vertices[ibo[t]], &b = mesh.vertices[ibo[t + 1]], &c = mesh.vertices[ibo[t + 2]];
			const double area = ((double)b.x - a.x) * ((double)c.y - a.y) - ((double)c.x - a.x) * ((double)b.y - a.y);
			if (area != 0 && (area < 0) != wantNegative) std::swap(ibo[t + 1], ibo[t + 2]);
		}
		mesh.subRanges[surfaceIndex].iboCount = ibo.Size() - start;
		RecordSurfaceVerts(surfaceIndex, base, (uint32_t)n);
	}

	//--- flat build ---------------------------------------------------------

	// The classic flat alpha (hw_flats.cpp ProcessSector): portal-stacked
	// planes use the plane alpha, all others 1 - reflect. Anything below 1 is
	// routed to the classic translucent-border pass (AddFlat), which still runs
	// on the levelmesh path, so such flats are never meshed.
	static bool IsTranslucentFlat(sector_t *sec, int plane)
	{
		const double alpha = sec->ValidatePortal(plane) != nullptr ? sec->GetAlpha(plane) : 1. - sec->GetReflect(plane);
		return alpha < 1. - FLT_EPSILON;
	}

	void BuildFlat(subsector_t *sub)
	{
		sector_t *sec = sub->sector;
		if (sec == nullptr) return;
		int r = SectorRegion(sec);
		SwitchRegion(r);

		float lmuv[2] = { 0, 0 };

		//--- floor ---
		if (sec->GetTexture(sector_t::floor) != skyflatnum && !IsTranslucentFlat(sec, sector_t::floor))
		{
			FGameTexture *tex = TexMan.GetGameTexture(sec->GetTexture(sector_t::floor), true);
			if (tex && tex->isValid())
			{
				uint32_t flags = LMF_ISFLAT;
				if (IsLoadDynamic(sec, sector_t::floor)) flags |= LMF_LOADDYN;
				uint32_t planeRefs[4] = { LM_PlaneRef(sec, sector_t::floor),
					LM_PlaneRef(sec, sector_t::ceiling), LM_PLANEREF_NONE, LM_PLANEREF_NONE };
				uint32_t surf = AddSurface(tex, sec->GetTexture(sector_t::floor), r, flags, planeRefs,
					LM_LightRef(sec, LM_LIGHT_SLOT_FLOOR));
				// sector light for flats
				mesh.surfaces[surf].light = (int16_t)RescaleLightLevel(sec->lightlevel);
				mesh.surfaces[surf].tierLight = sec->planes[sector_t::floor].Light;
				EmitFlatFan(sub, sec->floorplane, lmuv, surf, 0);
			}
		}

		//--- ceiling ---
		if (sec->GetTexture(sector_t::ceiling) != skyflatnum && !IsTranslucentFlat(sec, sector_t::ceiling))
		{
			FGameTexture *tex = TexMan.GetGameTexture(sec->GetTexture(sector_t::ceiling), true);
			if (tex && tex->isValid())
			{
				uint32_t flags = LMF_ISFLAT;
				if (IsLoadDynamic(sec, sector_t::ceiling)) flags |= LMF_LOADDYN;
				uint32_t planeRefs[4] = { LM_PlaneRef(sec, sector_t::floor),
					LM_PlaneRef(sec, sector_t::ceiling), LM_PLANEREF_NONE, LM_PLANEREF_NONE };
				uint32_t surf = AddSurface(tex, sec->GetTexture(sector_t::ceiling), r, flags, planeRefs,
					LM_LightRef(sec, LM_LIGHT_SLOT_CEILING));
				mesh.surfaces[surf].light = (int16_t)RescaleLightLevel(sec->lightlevel);
				mesh.surfaces[surf].tierLight = sec->planes[sector_t::ceiling].Light;
				EmitFlatFan(sub, sec->ceilingplane, lmuv, surf, 1);
			}
		}

		//--- 3D-floor fake flats (model sector's floor + ceiling fans) ---
		// Each F3DFloor in the sector gets its own floor + ceiling fan in the same
		// pool, plane refs pointing at the model sector (heightsec). No second pass,
		// no runtime copies; the structure is static data.
		TArray<F3DFloor *> &ffloors = sec->e->XFloor.ffloors;
		int nff = (int)ffloors.Size();
		for (int j = 0; j < nff; j++)
		{
			F3DFloor *xfloor = ffloors[j];
			if (!(xfloor->flags & FF_EXISTS)) continue;
			sector_t *model = xfloor->model;
			if (model == nullptr) continue;
			// model floor fan
			FGameTexture *ftex = TexMan.GetGameTexture(model->GetTexture(sector_t::floor), true);
			if (ftex && ftex->isValid())
			{
				uint32_t flags = LMF_ISFLAT | LMF_3DFLOOR;
				if (IsLoadDynamic(model, sector_t::floor)) flags |= LMF_LOADDYN;
				uint32_t planeRefs[4] = { LM_PlaneRef(model, sector_t::floor),
					LM_PlaneRef(model, sector_t::ceiling), LM_PLANEREF_NONE, LM_PLANEREF_NONE };
				uint32_t surf = AddSurface(ftex, model->GetTexture(sector_t::floor), r, flags, planeRefs,
					LM_LightRef(model, LM_LIGHT_SLOT_FLOOR), LMDS_NOCULL | (ftex->isMasked() ? LMDS_MASKED : 0u));
				mesh.surfaces[surf].light = (int16_t)RescaleLightLevel(model->lightlevel);
				mesh.surfaces[surf].tierLight = model->planes[sector_t::floor].Light;
				EmitFlatFan(sub, model->floorplane, lmuv, surf, 0);
			}
			// model ceiling fan
			FGameTexture *ctex = TexMan.GetGameTexture(model->GetTexture(sector_t::ceiling), true);
			if (ctex && ctex->isValid())
			{
				uint32_t flags = LMF_ISFLAT | LMF_3DFLOOR;
				if (IsLoadDynamic(model, sector_t::ceiling)) flags |= LMF_LOADDYN;
				uint32_t planeRefs[4] = { LM_PlaneRef(model, sector_t::floor),
					LM_PlaneRef(model, sector_t::ceiling), LM_PLANEREF_NONE, LM_PLANEREF_NONE };
				uint32_t surf = AddSurface(ctex, model->GetTexture(sector_t::ceiling), r, flags, planeRefs,
					LM_LightRef(model, LM_LIGHT_SLOT_CEILING), LMDS_NOCULL | (ctex->isMasked() ? LMDS_MASKED : 0u));
				mesh.surfaces[surf].light = (int16_t)RescaleLightLevel(model->lightlevel);
				mesh.surfaces[surf].tierLight = model->planes[sector_t::ceiling].Light;
				EmitFlatFan(sub, model->ceilingplane, lmuv, surf, 1);
			}
		}
	}

	//--- top-level build ----------------------------------------------------

	// Walk per region so each region's vertices form a contiguous block in the
	// pool (the spec invariant: one region = one self-contained vertex+index
	// block). Within a region, walls are emitted first, then flats.
	void BuildGeometry()
	{
		// One wall emission per non-polyobject side (see BuildWall).
		sideEmitted.Clear();
		sideEmitted.Resize(level.sides.Size());
		for (auto &e : sideEmitted) e = 0;
		int nregions = mesh.regions.Size();
		for (int r = 0; r < nregions; r++)
		{
			BeginRegion(r);
			int nsides = level.segs.Size();
			for (int i = 0; i < nsides; i++)
			{
				seg_t &seg = level.segs[i];
				if (seg.frontsector == nullptr) continue;
				if (SectorRegion(seg.frontsector) != r) continue;
				BuildWall(&seg);
			}
			int nsub = level.subsectors.Size();
			for (int i = 0; i < nsub; i++)
			{
				subsector_t &sub = level.subsectors[i];
				if (sub.sector == nullptr) continue;
				if (SectorRegion(sub.sector) != r) continue;
				BuildFlat(&sub);
			}
			EndRegion(r);
		}
		m_curRegion = -1;
	}

	void BuildSubRanges()
	{
		// The per-surface sub-range records are already in mesh.subRanges in build
		// order (one per surface, with the AABB computed at emission). Build the
		// per-(region, texture) table the region record's texRangeOffset/Count
		// points into: group the region's surfaces by texture index (sorted, one
		// entry per distinct texture); each entry carries the IBO span covering
		// the group's surfaces and the union of their world AABBs (spec section 3
		// region record). Within a texture group the surfaces keep build order.
		mesh.texRanges.Clear();
		int nregions = mesh.regions.Size();
		TArray<uint32_t> regionSurfaces;
		for (int r = 0; r < nregions; r++)
		{
			LevelMeshRegion &reg = mesh.regions[r];
			regionSurfaces.Clear();
			for (uint32_t s = 0; s < mesh.surfaces.Size(); s++)
			{
				if ((int)mesh.surfaces[s].regionIndex != r) continue;
				regionSurfaces.Push(s);
			}
			// stable sort by (texture index, draw state): one group per material
			// bind + sampler/texture clamp state; build order within a group.
			auto groupKey = [&](uint32_t s) {
				return ((uint64_t)mesh.surfaces[s].textureIndex << 32) | mesh.subRanges[s].drawState;
			};
			std::stable_sort(regionSurfaces.begin(), regionSurfaces.end(),
				[&](uint32_t a, uint32_t b) { return groupKey(a) < groupKey(b); });
			// S1.2 fix (ticket 03): the texRange span computed below is the
			// bounding interval of the group's member sub-ranges, which is only
			// exact when the group's surfaces are contiguous in the region IBO.
			// Emission appends in BSP order, so same-texture surfaces interleave
			// and the bounding spans cover other textures' triangles (later groups
			// rasterize equal-depth indices and show nothing, and some spans run
			// past the end of small region IBOs entirely). Permute the region IBO
			// into the texture-sorted order above (a pure permutation of the same
			// indices; the vertex pool is untouched) and rewrite each surface's
			// sub-range offset so every span is exactly [first, first + sum).
			TArray<uint32_t> &ibo = mesh.regionIBOs[r];
			uint32_t attributed = 0;
			for (uint32_t k = 0; k < regionSurfaces.Size(); k++)
				attributed += mesh.subRanges[regionSurfaces[k]].iboCount;
			if (attributed == ibo.Size() && ibo.Size() > 0)
			{
				// NOTE: TArray::Reserve(n) APPENDS n elements in GZDoom (grow-by-n,
				// returns the old count); it does not preallocate capacity. Do not
				// "optimize" this with Reserve - Push alone grows geometrically.
				TArray<uint32_t> reordered;
				for (uint32_t k = 0; k < regionSurfaces.Size(); k++)
				{
					LevelMeshSubRange &sr = mesh.subRanges[regionSurfaces[k]];
					uint32_t newOffset = reordered.Size();
					for (uint32_t n = 0; n < sr.iboCount; n++)
						reordered.Push(ibo[sr.iboOffset + n]);
					sr.iboOffset = newOffset;
				}
				ibo = reordered;
			}
			reg.texRangeOffset = (uint32_t)mesh.texRanges.Size();
			reg.texRangeCount = 0;
			uint32_t i = 0;
			while (i < regionSurfaces.Size())
			{
				uint32_t tex = mesh.surfaces[regionSurfaces[i]].textureIndex;
				const uint64_t key = groupKey(regionSurfaces[i]);
				LevelMeshTexRange tr;
				memset(&tr, 0, sizeof(tr));
				tr.textureIndex = tex;
				tr.drawState = mesh.subRanges[regionSurfaces[i]].drawState;
				tr.iboOffset = 0xFFFFFFFFu;
				tr.aabbMin[0] = tr.aabbMin[1] = 1e30f;
				tr.aabbMax[0] = tr.aabbMax[1] = -1e30f;
				tr.firstSurface = regionSurfaces[i];
				tr.surfaceCount = 0;
				uint32_t j = i;
				while (j < regionSurfaces.Size() && groupKey(regionSurfaces[j]) == key)
				{
					const LevelMeshSubRange &sr = mesh.subRanges[regionSurfaces[j]];
					if (sr.iboOffset < tr.iboOffset) tr.iboOffset = sr.iboOffset;
					uint32_t end = sr.iboOffset + sr.iboCount;
					if (end > tr.iboOffset + tr.iboCount) tr.iboCount = end - tr.iboOffset;
					if (sr.aabbMin[0] < tr.aabbMin[0]) tr.aabbMin[0] = sr.aabbMin[0];
					if (sr.aabbMin[1] < tr.aabbMin[1]) tr.aabbMin[1] = sr.aabbMin[1];
					if (sr.aabbMax[0] > tr.aabbMax[0]) tr.aabbMax[0] = sr.aabbMax[0];
					if (sr.aabbMax[1] > tr.aabbMax[1]) tr.aabbMax[1] = sr.aabbMax[1];
					tr.surfaceCount++;
					j++;
				}
				if (tr.iboOffset == 0xFFFFFFFFu) tr.iboOffset = 0;
				mesh.texRanges.Push(tr);
				reg.texRangeCount++;
				i = j;
			}
		}
	}

	void BuildBands()
	{
		// The band table is built from 3D floors: each existing 3D floor in a
		// sector creates one band. The 3D-floor wall build (a later pass) attaches
		// the band index to the affected wall surfaces. For now we just record
		// the bands so the table is populated.
		int nsec = level.sectors.Size();
		for (int s = 0; s < nsec; s++)
		{
			sector_t &sec = level.sectors[s];
			int nff = sec.e->XFloor.ffloors.Size();
			for (int i = 0; i < nff; i++)
			{
				F3DFloor *ff = sec.e->XFloor.ffloors[i];
				if (!(ff->flags & FF_EXISTS)) continue;
				LevelMeshBand band;
				band.modelSector = 0;
				band.plane = 0;
				band.lightRef = -1;
				mesh.bands.Push(band);
			}
		}
	}

	void BuildActors()
	{
		// per-region actor list: the subsector indices (into level.subsectors) of
		// all subsectors whose sector maps to the region, sorted by subsector
		// index. The draw pass walks each subsector's live sprite list at draw
		// time, so the list is static subsector references, not actor ids. The
		// list is a flat array; each region records an [actorOffset, actorCount]
		// range.
		int nregions = mesh.regions.Size();
		TArray<uint32_t> regionActorCount;
		regionActorCount.Resize(nregions);
		int nsub = level.subsectors.Size();
		for (int s = 0; s < nsub; s++)
		{
			subsector_t &sub = level.subsectors[s];
			if (sub.sector == nullptr) continue;
			int r = SectorRegion(sub.sector);
			mesh.actors.Push((uint32_t)s);
			regionActorCount[r]++;
		}
		uint32_t off = 0;
		for (int r = 0; r < nregions; r++)
		{
			mesh.regions[r].actorOffset = off;
			mesh.regions[r].actorCount = regionActorCount[r];
			off += regionActorCount[r];
		}
	}
};

}

//============================================================================
//
// FLevelMesh::Build
//
//============================================================================

FLevelMesh FLevelMesh::Build(FLevelLocals &lvl)
{
	FLevelMesh mesh;
	Builder b(lvl, mesh);
	b.PartitionRegions();
	b.BuildGeometry();
	b.BuildSubRanges();
	b.BuildBands();
	b.BuildActors();
	// Spec section 3 build order step 5: initialize the per-sector state
	// ring with a full snapshot of the load-time sector state (and build
	// the 3D light state buffer).
	mesh.state.Init(lvl);
	return mesh;
}
