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
#include <cmath>
#include <cstring>

using levelmesh::FLevelMesh;
using levelmesh::LevelMeshVertex;
using levelmesh::LevelMeshSurface;
using levelmesh::LevelMeshRegion;
using levelmesh::LevelMeshSubRange;
using levelmesh::LevelMeshBand;
using levelmesh::LMF_ISFLAT;
using levelmesh::LMF_SKY;
using levelmesh::LMF_3DFLOOR;
using levelmesh::LMF_LOADDYN;
using levelmesh::LMF_FAKECONTRAST;
using levelmesh::LMF_LIGHTMAP;
using levelmesh::LMF_POLYOBJ;
using levelmesh::LM_LIGHTREF_OWN;

//============================================================================
//
// Local helpers
//
//============================================================================

// Ported verbatim from hw_walls.cpp (it is a static inline there).
static int LM_CalcRelLight(int lightlevel, int orglightlevel, int rel)
{
	if (orglightlevel >= 253)
	{
		return 0;
	}
	else if (lightlevel - rel > 256)
	{
		return 256 - lightlevel + rel;
	}
	else
	{
		return rel;
	}
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

	uint32_t EmitVertex(float x, float y, float slotA, float slotB, float u,
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
		mesh.positions.Push(FVector3(x, y, slotA));
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

	struct TexBake
	{
		int renderWidth, renderHeight;
		float scaleX, scaleY;
	};

	TexBake BakeTexInfo(FGameTexture *tex, float x, float y)
	{
		TexBake tb;
		FTexCoordInfo tci;
		tci.GetFromTexture(tex, x, y, false);
		tb.renderWidth = tci.mRenderWidth;
		tb.renderHeight = tci.mRenderHeight;
		tb.scaleX = tci.mScale.X;
		tb.scaleY = tci.mScale.Y;
		return tb;
	}

	// Classic RowOffset (unused now that BakeWallUVs uses tci; kept for reference).
	static float RowOffset(float ofs, float scaleY)
	{
		float scale = fabsf(scaleY);
		if (scale == 1.f) return ofs;
		return ofs / scale;
	}

	// Build the FTexCoordInfo + u offset/length for a wall texture, mirroring the
	// classic tci setup in the wall tcs generation.
	struct WallUV { FTexCoordInfo tci; float l_ul; float texlength; };
	WallUV MakeWallUV(FGameTexture *tex, side_t *side, int which, float walllen) const
	{
		WallUV w;
		w.tci.GetFromTexture(tex, (float)side->GetTextureXScale(which), (float)side->GetTextureYScale(which), false);
		float t_ofs = (float)side->GetTextureXOffset(which);
		w.l_ul = w.tci.FloatToTexU(w.tci.TextureOffset(t_ofs));
		float length = side->TexelLength ? (float)side->TexelLength : walllen;
		w.texlength = w.tci.FloatToTexU(length);
		return w;
	}

	// Bake the four wall corner UVs in classic corner order [LOLFT, UPLFT, UPRGT, LORGT],
	// mirroring the classic tcs generation: u = l_ul + texlength*frac, v = FloatToTexV(-z+texturetop+skew).
	void BakeWallUVs(seg_t *seg, const FTexCoordInfo &tci, float l_ul, float texlength,
		const float z[4], float texturetop, float skew, float uv[4][2])
	{
		// z[0]=LOLFT(bottom-left), z[1]=UPLFT(top-left), z[2]=UPRGT(top-right), z[3]=LORGT(bottom-right)
		uv[0][0] = l_ul;                                        // LOLFT u (fracleft=0)
		uv[0][1] = tci.FloatToTexV(-z[0] + texturetop);         // LOLFT v
		uv[1][0] = l_ul;                                        // UPLFT u (fracleft=0)
		uv[1][1] = tci.FloatToTexV(-z[1] + texturetop);         // UPLFT v
		uv[2][0] = l_ul + texlength;                            // UPRGT u (fracright=1)
		uv[2][1] = tci.FloatToTexV(-z[2] + texturetop + skew);  // UPRGT v
		uv[3][0] = l_ul + texlength;                            // LORGT u (fracright=1)
		uv[3][1] = tci.FloatToTexV(-z[3] + texturetop + skew);  // LORGT v
	}

	//--- light bake ---------------------------------------------------------

	void BakeLight(LevelMeshSurface &s, side_t *side, int which, int orglightlevel)
	{
		int rel = 0;
		int ll = side->GetLightLevel(false, orglightlevel, which, false, &rel);
		s.light = (int16_t)RescaleLightLevel(ll);
		int rl = LM_CalcRelLight(ll, orglightlevel, rel);
		s.relSmooth = (int16_t)rl;
		s.relNonSmooth = (int16_t)rl;
		if (rl != 0) s.flags |= LMF_FAKECONTRAST;
		s.tierLight = side->TierLights[which];
	}

	//--- surface record -----------------------------------------------------

	uint32_t AddSurface(FGameTexture *tex, int r, uint32_t flags)
	{
		LevelMeshSurface s;
		memset(&s, 0, sizeof(s));
		s.textureIndex = tex ? (uint32_t)tex->GetID().GetIndex() : 0xFFFFFFFFu;
		s.regionIndex = (uint32_t)r;
		s.lightRef = LM_LIGHTREF_OWN;
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
		sr.aabbMin[0] = sr.aabbMin[1] = 1e30f;
		sr.aabbMax[0] = sr.aabbMax[1] = -1e30f;
		mesh.subRanges.Push(sr);
		return idx;
	}

	//--- wall emission ------------------------------------------------------

	// Get the lightmap surface for a wall type index (0=TOP, 1=M1S, 2=M2S, 3=BOTTOM).
	static LightmapSurface *GetSideLightmap(side_t *side, int index)
	{
		return side->lightmap ? &side->lightmap[index] : nullptr;
	}

	void FillLightmapUV(seg_t *seg, int index, float lmuv[4][2])
	{
		LightmapSurface *lm = GetSideLightmap(seg->sidedef, index);
		if (lm == nullptr || lm->Type == ST_NULL) return;
		// pool order [v1, v2, v1', v2'] = [LOLFT, LORGT, UPLFT, UPRGT]
		// lightmap TexCoords order [LOLFT, UPLFT, UPRGT, LORGT]
		const texcoord *tc = (const texcoord *)lm->TexCoords;
		lmuv[0][0] = tc[0].u; lmuv[0][1] = tc[0].v; // LOLFT
		lmuv[1][0] = tc[3].u; lmuv[1][1] = tc[3].v; // LORGT
		lmuv[2][0] = tc[1].u; lmuv[2][1] = tc[1].v; // UPLFT
		lmuv[3][0] = tc[2].u; lmuv[3][1] = tc[2].v; // UPRGT
	}

	uint32_t EmitWallQuad(seg_t *seg, const float z[4], const float uv[4][2],
		const int cornerMap[4], float lmuv[4][2], uint32_t surfaceIndex,
		bool loadDyn, const float vparam[4])
	{
		uint32_t base = (uint32_t)mesh.vertices.Size();
		for (int k = 0; k < 4; k++)
		{
			int c = cornerMap[k];
			vertex_t *vtx = (k < 2) ? seg->v1 : seg->v2;
			float slotA, slotB;
			if (loadDyn) { slotA = vparam ? vparam[k] : 0; slotB = 0; }
			else { slotA = z[c]; slotB = uv[k][1]; }
			EmitVertex(vtx->fX(), vtx->fY(), slotA, slotB, uv[k][0],
				lmuv[c][0], lmuv[c][1], surfaceIndex);
		}
		uint32_t iboOff = (uint32_t)mesh.regionIBOs[m_curRegion].Size();
		mesh.regionIBOs[m_curRegion].Push(base + 0);
		mesh.regionIBOs[m_curRegion].Push(base + 1);
		mesh.regionIBOs[m_curRegion].Push(base + 2);
		mesh.regionIBOs[m_curRegion].Push(base + 3);
		mesh.regionIBOs[m_curRegion].Push(base + 2);
		mesh.regionIBOs[m_curRegion].Push(base + 1);
		RecordSurfaceVerts(surfaceIndex, base, 4);
		return iboOff;
	}

	// A sector plane is load-dynamic if it is alpha, or the sector is a transdoor,
	// or the plane is scrolling (non-zero scroll offset at load). These surfaces
	// get their vertex slotA written as a vparam at load (LMF_LOADDYN) so the
	// vertex shader evaluates z from the sector state ring.
	static bool IsLoadDynamic(sector_t *sec, int plane)
	{
		if (sec == nullptr) return false;
		if (sec->planes[plane].alpha > 0) return true;
		if (sec->transdoor) return true;
		const FTransform &xf = sec->planes[plane].xform;
		if (xf.xOffs != 0 || xf.yOffs != 0) return true;
		return false;
	}

	//--- wall build ---------------------------------------------------------

	void BuildWall(seg_t *seg)
	{
		sector_t *frontsector = seg->frontsector;
		sector_t *backsector = seg->backsector;
		if (frontsector == nullptr || backsector == nullptr) return;
		side_t *side = seg->sidedef;
		vertex_t *v1 = seg->v1, *v2 = seg->v2;
		// Some segs carry a null sidedef or degenerate vertices (e.g. 3D-floor or
		// non-drawable segs); the classic renderer skips them. Guard here.
		if (side == nullptr || v1 == nullptr || v2 == nullptr) return;
		int r = SectorRegion(frontsector);
		SwitchRegion(r);

		float v1x = v1->fX(), v1y = v1->fY(), v2x = v2->fX(), v2y = v2->fY();
		float walllen = sqrtf((v2x - v1x) * (v2x - v1x) + (v2y - v1y) * (v2y - v1y));

		float fch1 = (float)frontsector->ceilingplane.ZatPoint(v1x, v1y);
		float fch2 = (float)frontsector->ceilingplane.ZatPoint(v2x, v2y);
		float ffh1 = (float)frontsector->floorplane.ZatPoint(v1x, v1y);
		float ffh2 = (float)frontsector->floorplane.ZatPoint(v2x, v2y);
		float bch1 = (float)backsector->ceilingplane.ZatPoint(v1x, v1y);
		float bch2 = (float)backsector->ceilingplane.ZatPoint(v2x, v2y);
		float bfh1 = (float)backsector->floorplane.ZatPoint(v1x, v1y);
		float bfh2 = (float)backsector->floorplane.ZatPoint(v2x, v2y);

		// clipped heights (classic)
		float bch1a = fch1, bch2a = fch2;
		if (bch1a < bfh1) bch1a = bfh1;
		if (bch2a < bfh2) bch2a = bfh2;
		if (bch1a < bch2a) bch1a = bch2a;
		if (bch2a < bch1a) bch2a = bch1a;
		float bfh1a = bfh1, bfh2a = bfh2;
		if (fch1 < bfh1 && fch2 < bfh2 && (seg->linedef->flags & ML_DRAWFULLHEIGHT) == 0)
		{
			bfh1 = fch1; bfh2 = fch2;
		}

		int orglightlevel = frontsector->lightlevel;

		//--- 3D-floor boundary quads (lightmap index 1 = RENDERWALL_M1S) ---
		// Each F3DFloor shared by the front sector but not the back sector gets a
		// middle-wall quad spanning the model sector's floor/ceiling planes. This
		// mirrors the classic builder's 3D-floor line emission. The model sector is
		// recorded on the surface so the draw path samples its state (fake-flat / 3D
		// floor semantics).
		TArray<F3DFloor *> &frontff = frontsector->e->XFloor.ffloors;
		TArray<F3DFloor *> &backff = backsector->e->XFloor.ffloors;
		int nfrontff = (int)frontff.Size();
		int nbackff = (int)backff.Size();
		for (int j = 0; j < nfrontff; j++)
		{
			F3DFloor *xfloor = frontff[j];
			if (!(xfloor->flags & FF_EXISTS) || !(xfloor->flags & FF_RENDERSIDES)) continue;
			if ((xfloor->flags & FF_INVERTSIDES) == 0) continue;
			bool bothSides = false;
			for (int k = 0; k < nbackff; k++)
			{
				if (backff[k] == xfloor) { bothSides = true; break; }
			}
			if (bothSides) continue;
			sector_t *model = xfloor->model;
			if (model == nullptr) continue;
			FGameTexture *tex = TexMan.GetGameTexture(side->GetTexture(side_t::mid), true);
			if (tex == nullptr || !tex->isValid()) continue;
			WallUV wuv = MakeWallUV(tex, side, side_t::mid, walllen);
			float texturetop = (float)model->ceilingplane.ZatPoint(v1x, v1y)
				+ side->GetTextureYOffset(side_t::mid) + wuv.tci.mRenderHeight;
			float mff1 = (float)model->floorplane.ZatPoint(v1x, v1y);
			float mff2 = (float)model->floorplane.ZatPoint(v2x, v2y);
			float mch1 = (float)model->ceilingplane.ZatPoint(v1x, v1y);
			float mch2 = (float)model->ceilingplane.ZatPoint(v2x, v2y);
			float z[4] = { mff1, mch1, mff2, mch2 };
			int cornerMap[4] = { 0, 3, 1, 2 };
			float uv[4][2];
			BakeWallUVs(seg, wuv.tci, wuv.l_ul, wuv.texlength, z, texturetop, 0, uv);
			float lmuv[4][2] = { {0,0},{0,0},{0,0},{0,0} };
			FillLightmapUV(seg, 1, lmuv);
			uint32_t flags = GetSideLightmap(side, 1) ? LMF_LIGHTMAP : 0;
			if (side->Flags & WALLF_POLYOBJ) flags |= LMF_POLYOBJ;
			flags |= LMF_3DFLOOR;
			uint32_t surf = AddSurface(tex, r, flags);
			BakeLight(mesh.surfaces[surf], side, side_t::mid, orglightlevel);
			EmitWallQuad(seg, z, uv, cornerMap, lmuv, surf, false, nullptr);
		}

		//--- lower wall (lightmap index 3 = RENDERWALL_BOTTOM) ---
		if (bfh1 > ffh1 || bfh2 > ffh2)
		{
			FGameTexture *tex = TexMan.GetGameTexture(side->GetTexture(side_t::bottom), true);
			if (tex && tex->isValid())
			{
				bool peg = (seg->linedef->flags & ML_DONTPEGBOTTOM) > 0;
				WallUV wuv = MakeWallUV(tex, side, side_t::bottom, walllen);
				float backFloorTexZ = backsector->GetPlaneTexZ(sector_t::floor);
				float frontFloorTexZ = frontsector->GetPlaneTexZ(sector_t::floor);
				float texturetop = peg ? frontFloorTexZ + side->GetTextureYOffset(side_t::bottom) + wuv.tci.mRenderHeight
					: backFloorTexZ + side->GetTextureYOffset(side_t::bottom);
				float z[4] = { ffh1, bfh1a, ffh2, bfh2a };
				int cornerMap[4] = { 0, 3, 1, 2 };
				float uv[4][2];
				BakeWallUVs(seg, wuv.tci, wuv.l_ul, wuv.texlength, z, texturetop, 0, uv);
				float lmuv[4][2] = { {0,0},{0,0},{0,0},{0,0} };
				FillLightmapUV(seg, 3, lmuv);
				uint32_t flags = GetSideLightmap(side, 3) ? LMF_LIGHTMAP : 0;
				bool dyn = IsLoadDynamic(frontsector, sector_t::floor) || IsLoadDynamic(backsector, sector_t::floor);
				if (dyn) flags |= LMF_LOADDYN;
				uint32_t surf = AddSurface(tex, r, flags);
				BakeLight(mesh.surfaces[surf], side, side_t::bottom, orglightlevel);
				EmitWallQuad(seg, z, uv, cornerMap, lmuv, surf, dyn, nullptr);
			}
		}

		//--- upper wall (lightmap index 0 = RENDERWALL_TOP) ---
		if (bch1a > fch1 || bch2a > fch2)
		{
			FGameTexture *tex = TexMan.GetGameTexture(side->GetTexture(side_t::top), true);
			bool peg = (seg->linedef->flags & ML_DONTPEGTOP) == 0;
			if (tex && tex->isValid())
			{
				WallUV wuv = MakeWallUV(tex, side, side_t::top, walllen);
				float frontFloorTexZ = frontsector->GetPlaneTexZ(sector_t::floor);
				float backCeilTexZ = backsector->GetPlaneTexZ(sector_t::ceiling);
				float texturetop = peg ? frontFloorTexZ + side->GetTextureYOffset(side_t::top) + wuv.tci.mRenderHeight
					: backCeilTexZ + side->GetTextureYOffset(side_t::top);
				float z[4] = { fch1, bch1a, fch2, bch2a };
				int cornerMap[4] = { 2, 3, 0, 1 };
				float uv[4][2];
				BakeWallUVs(seg, wuv.tci, wuv.l_ul, wuv.texlength, z, texturetop, 0, uv);
				float lmuv[4][2] = { {0,0},{0,0},{0,0},{0,0} };
				FillLightmapUV(seg, 0, lmuv);
				uint32_t flags = GetSideLightmap(side, 0) ? LMF_LIGHTMAP : 0;
				if (frontsector->GetTexture(sector_t::ceiling) == skyflatnum) flags |= LMF_SKY;
				bool dyn = IsLoadDynamic(frontsector, sector_t::ceiling) || IsLoadDynamic(backsector, sector_t::ceiling);
				if (dyn) flags |= LMF_LOADDYN;
				uint32_t surf = AddSurface(tex, r, flags);
				BakeLight(mesh.surfaces[surf], side, side_t::top, orglightlevel);
				// sky scroll kind: 0 sky1 / 1 sky2 / 2 mist. The full sky2/doublesky
				// resolution (MBF line texture, doublesky second layer) is a rendering
				// concern; the data layer records the kind the VS uses to index the
				// per-level sky scroll uniform. Default sky1 for the standard case.
				if (flags & LMF_SKY)
				{
					mesh.surfaces[surf].skyScrollKind = 0;
					mesh.surfaces[surf].skyScrollKind2 = 0;
				}
				EmitWallQuad(seg, z, uv, cornerMap, lmuv, surf, dyn, nullptr);
			}
		}

		//--- middle wall (mid texture, lightmap index 1 = RENDERWALL_M1S) ---
		FGameTexture *midtex = TexMan.GetGameTexture(side->GetTexture(side_t::mid), true);
		if (midtex && midtex->isValid() && (fch1 > ffh1 || fch2 > ffh2))
		{
			WallUV wuv = MakeWallUV(midtex, side, side_t::mid, walllen);
			float texturetop = fch1 + side->GetTextureYOffset(side_t::mid) + wuv.tci.mRenderHeight;
			float z[4] = { ffh1, fch1, ffh2, fch2 };
			int cornerMap[4] = { 0, 3, 1, 2 };
			float uv[4][2];
			BakeWallUVs(seg, wuv.tci, wuv.l_ul, wuv.texlength, z, texturetop, 0, uv);
			float lmuv[4][2] = { {0,0},{0,0},{0,0},{0,0} };
			FillLightmapUV(seg, 1, lmuv);
			uint32_t flags = GetSideLightmap(side, 1) ? LMF_LIGHTMAP : 0;
			if (side->Flags & WALLF_POLYOBJ) flags |= LMF_POLYOBJ;
			bool dyn = IsLoadDynamic(frontsector, sector_t::floor) || IsLoadDynamic(frontsector, sector_t::ceiling);
			if (dyn) flags |= LMF_LOADDYN;
			uint32_t surf = AddSurface(midtex, r, flags);
			BakeLight(mesh.surfaces[surf], side, side_t::mid, orglightlevel);
			EmitWallQuad(seg, z, uv, cornerMap, lmuv, surf, dyn, nullptr);
		}
	}

	//--- flat emission ------------------------------------------------------

	// Emit a flat as a fan over the subsector's polygon vertices (firstline[j].v1),
	// fanned from vertex 0. The flat UV is world-space: u = x/64, v = -y/64 (classic).
	void EmitFlatFan(subsector_t *sub, float z, float lmuv[2], uint32_t surfaceIndex)
	{
		int n = sub->numlines;
		uint32_t base = (uint32_t)mesh.vertices.Size();
		for (int i = 0; i < n; i++)
		{
			vertex_t *vt = sub->firstline[i].v1;
			float u = (float)vt->fX() / 64.f;
			float v = -(float)vt->fY() / 64.f;
			EmitVertex((float)vt->fX(), (float)vt->fY(), z, v, u,
				lmuv[0], lmuv[1], surfaceIndex);
		}
		// fan triangles (0, i, i+1)
		for (int i = 1; i < n - 1; i++)
		{
			mesh.regionIBOs[m_curRegion].Push(base);
			mesh.regionIBOs[m_curRegion].Push(base + i);
			mesh.regionIBOs[m_curRegion].Push(base + i + 1);
		}
		RecordSurfaceVerts(surfaceIndex, base, (uint32_t)n);
	}

	//--- flat build ---------------------------------------------------------

	void BuildFlat(subsector_t *sub)
	{
		sector_t *sec = sub->sector;
		if (sec == nullptr) return;
		int r = SectorRegion(sec);
		SwitchRegion(r);

		float lmuv[2] = { 0, 0 };
		vertex_t *ref = sub->firstline[0].v1;

		//--- floor ---
		if (sec->GetTexture(sector_t::floor) != skyflatnum)
		{
			FGameTexture *tex = TexMan.GetGameTexture(sec->GetTexture(sector_t::floor), true);
			if (tex && tex->isValid())
			{
				float z = (float)sec->floorplane.ZatPoint(ref->fX(), ref->fY());
				uint32_t flags = LMF_ISFLAT;
				if (IsLoadDynamic(sec, sector_t::floor)) flags |= LMF_LOADDYN;
				uint32_t surf = AddSurface(tex, r, flags);
				// sector light for flats
				mesh.surfaces[surf].light = (int16_t)RescaleLightLevel(sec->lightlevel);
				mesh.surfaces[surf].tierLight = sec->planes[sector_t::floor].Light;
				EmitFlatFan(sub, z, lmuv, surf);
			}
		}

		//--- ceiling ---
		if (sec->GetTexture(sector_t::ceiling) != skyflatnum)
		{
			FGameTexture *tex = TexMan.GetGameTexture(sec->GetTexture(sector_t::ceiling), true);
			if (tex && tex->isValid())
			{
				float z = (float)sec->ceilingplane.ZatPoint(ref->fX(), ref->fY());
				uint32_t flags = LMF_ISFLAT;
				if (IsLoadDynamic(sec, sector_t::ceiling)) flags |= LMF_LOADDYN;
				uint32_t surf = AddSurface(tex, r, flags);
				mesh.surfaces[surf].light = (int16_t)RescaleLightLevel(sec->lightlevel);
				mesh.surfaces[surf].tierLight = sec->planes[sector_t::ceiling].Light;
				EmitFlatFan(sub, z, lmuv, surf);
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
				float z = (float)model->floorplane.ZatPoint(ref->fX(), ref->fY());
				uint32_t flags = LMF_ISFLAT | LMF_3DFLOOR;
				if (IsLoadDynamic(model, sector_t::floor)) flags |= LMF_LOADDYN;
				uint32_t surf = AddSurface(ftex, r, flags);
				mesh.surfaces[surf].light = (int16_t)RescaleLightLevel(model->lightlevel);
				mesh.surfaces[surf].tierLight = model->planes[sector_t::floor].Light;
				EmitFlatFan(sub, z, lmuv, surf);
			}
			// model ceiling fan
			FGameTexture *ctex = TexMan.GetGameTexture(model->GetTexture(sector_t::ceiling), true);
			if (ctex && ctex->isValid())
			{
				float z = (float)model->ceilingplane.ZatPoint(ref->fX(), ref->fY());
				uint32_t flags = LMF_ISFLAT | LMF_3DFLOOR;
				if (IsLoadDynamic(model, sector_t::ceiling)) flags |= LMF_LOADDYN;
				uint32_t surf = AddSurface(ctex, r, flags);
				mesh.surfaces[surf].light = (int16_t)RescaleLightLevel(model->lightlevel);
				mesh.surfaces[surf].tierLight = model->planes[sector_t::ceiling].Light;
				EmitFlatFan(sub, z, lmuv, surf);
			}
		}
	}

	//--- top-level build ----------------------------------------------------

	// Walk per region so each region's vertices form a contiguous block in the
	// pool (the spec invariant: one region = one self-contained vertex+index
	// block). Within a region, walls are emitted first, then flats.
	void BuildGeometry()
	{
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
		// The sub-ranges are already in mesh.subRanges in build order (one per
		// surface, with the AABB computed at emission). Compute the per-region
		// [texRangeOffset, texRangeCount] range by walking the surfaces in order.
		int nregions = mesh.regions.Size();
		for (int r = 0; r < nregions; r++)
		{
			LevelMeshRegion &reg = mesh.regions[r];
			uint32_t count = 0;
			uint32_t first = 0xFFFFFFFFu;
			for (uint32_t s = 0; s < mesh.surfaces.Size(); s++)
			{
				if ((int)mesh.surfaces[s].regionIndex != r) continue;
				if (first == 0xFFFFFFFFu) first = s;
				count++;
			}
			reg.texRangeOffset = (first == 0xFFFFFFFFu) ? 0 : first;
			reg.texRangeCount = count;
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
		// per-region actor list: each subsector's sprites are partitioned into
		// the region that owns the subsector's sector. The actor list is a flat
		// array; each region records an [actorOffset, actorCount] range.
		int nregions = mesh.regions.Size();
		TArray<uint32_t> regionActorCount;
		regionActorCount.Resize(nregions);
		int nsub = level.subsectors.Size();
		for (int s = 0; s < nsub; s++)
		{
			subsector_t &sub = level.subsectors[s];
			if (sub.sector == nullptr) continue;
			int r = SectorRegion(sub.sector);
			for (DVisualThinker *spr : sub.sprites)
			{
				AActor *mo = dynamic_cast<AActor *>(spr);
				if (mo == nullptr) continue;
				mesh.actors.Push((uint32_t)mo->tid);
				regionActorCount[r]++;
			}
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
	return mesh;
}
