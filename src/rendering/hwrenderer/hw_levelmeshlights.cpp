/*
** hw_levelmeshlights.cpp
**
** Levelmesh dynamic light lists: light slots, owner lists, the frame pass
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** Ticket 19. The classic path gathers the dynamic lights of every surface,
** sprite and model on the CPU each frame from the playsim's touch lists
** (side_t::dlist, FSection::dlist). The levelmesh keeps those lists on the
** GPU instead, retained and streamed dirty-only:
**
**  - every FDynamicLight has a slot; its record (LevelMeshLightRecord,
**    packed like AddLightToList) is compared every update and uploaded when
**    it changed (moved, pulsed, recoloured, shadowmap index);
**  - every side and section with lights has a list in the pool: its portal
**    group, the slots of its lights (ascending), LM_LIGHTLIST_END. A list is
**    rebuilt when the touch lists change: FDynamicLight::LinkLight /
**    UnlinkLight report the sides and sections they leave and enter
**    (LevelMesh_LightTouched), and a released light frees its slot
**    (LevelMesh_LightReleased).
**
** Consumers read one list by owner index (lm_lights.glsl): levelmesh walls
** their side's, flats their leaf's section, sprites / particles / small
** models their section's, decals their wall's. Big models (render radius
** over gl_model_light_optimization_radius) get an inline list in the
** frame's light buffer: the lights of every section their radius touches
** (LevelMesh_InlineLightList).
**
** The pool is a power-of-two block allocator; a list is rewritten in place
** when it keeps its size class. Slots and pool words grow by doubling; the
** backend grows its buffers keeping their contents (VkLevelMesh::
** GrowLightBuffer). Frames are serialized, so a block freed and reused in
** one update is never read with stale contents.
**
** lm_lights 0 is the negative control: every record is uploaded with radius
** 0 (no light reaches a levelmesh surface) and the classic draw paths gather
** their lights on the CPU again.
*/

// HEADER FILES ------------------------------------------------------------

#include <algorithm>
#include <cstring>

#include "hw_levelmeshbake.h"
#include "g_levellocals.h"
#include "a_dynlight.h"
#include "actorinlines.h"
#include "hw_clock.h"
#include "hw_cvars.h"
#include "hw_lightbuffer.h"
#include "hw_dynlightdata.h"
#include "c_cvars.h"
#include "v_video.h"
#include "models.h"

// PUBLIC DATA DEFINITIONS -------------------------------------------------

// Debug: the levelmesh light lists (0: no dynamic light reaches a levelmesh
// surface, sprites / models / decals gather on the CPU as the classic does).
CVAR(Bool, lm_lights, true, CVAR_NOSAVE)
// Debug: refine the flats' light lists per cluster (lightrefine.comp; 0:
// every flat reads its section's whole list, the negative control).
CVAR(Bool, lm_lightrefine, true, CVAR_NOSAVE)

// PRIVATE DATA DEFINITIONS ------------------------------------------------

namespace
{

constexpr unsigned MIN_SLOTS = 1024;
constexpr unsigned MIN_POOL_WORDS = 65536;
constexpr unsigned MIN_BLOCK_CLASS = 2;		// 4 words: group, one slot, end, spare

// Scratch arrays, reused across frames (game thread only).
TArray<uint32_t> LeafSections;
TArray<uint32_t> RecIndices;
TArray<LevelMeshLightRecord> RecRecords;
TArray<LevelMeshLightWord> Words;
TArray<uint32_t> ListSlots;
TArray<uint32_t> InfoScratch;
TArray<uint32_t> RefineClusters;
// Inline lists are built by the scene's worker threads (HWSprite::PutSprite).
thread_local TArray<uint32_t> InlineSlots;
thread_local TArray<uint32_t> InlineStamp;	// per slot: the inline list that took it
thread_local uint32_t InlineSerial = 0;
thread_local FDynLightData InlineData;

unsigned PowerOfTwoClass(unsigned words)
{
	unsigned k = MIN_BLOCK_CLASS;
	while ((1u << k) < words) k++;
	return k;
}

unsigned RoundUpPow2(unsigned n, unsigned minimum)
{
	unsigned v = minimum;
	while (v < n) v *= 2;
	return v;
}

void MarkOwnerDirty(LevelMeshLightState &st, unsigned owner)
{
	if (owner >= st.OwnerDirtyFlag.Size() || st.OwnerDirtyFlag[owner]) return;
	st.OwnerDirtyFlag[owner] = 1;
	st.DirtyOwners.Push(owner);
}

void FillInfo(FLevelLocals *Level, const LevelMeshBake *bake, const LevelMeshLightState &st, TArray<uint32_t> &out)
{
	unsigned groups = std::max(Level->Displacements.size, 1);
	unsigned ffgroups = bake->FFGroupSource.Size();
	out.Resize(LMLI_HEADER + ffgroups + 2 * groups * groups);
	out[LMLI_NUMGROUPS] = groups;
	out[LMLI_NUMSIDES] = st.NumSides;
	out[LMLI_NUMOWNERS] = st.NumOwners;
	out[LMLI_NUMFFGROUPS] = ffgroups;
	// A 3D-floor side group is lit by its side's list (HWWall: seg->sidedef);
	// plane groups are flats (their leaf's section).
	for (unsigned g = 0; g < ffgroups; g++)
	{
		int32_t src = bake->FFGroupSource[g];
		out[LMLI_HEADER + g] = src >= 0 ? (uint32_t)src : LM_LIGHTLIST_END;
	}
	uint32_t *disp = &out[LMLI_HEADER + ffgroups];
	for (unsigned from = 0; from < groups; from++)
	{
		for (unsigned to = 0; to < groups; to++)
		{
			DVector2 off = (int)groups == Level->Displacements.size ? Level->Displacements.getOffset(from, to) : DVector2(0, 0);
			float x = (float)off.X, y = (float)off.Y;
			memcpy(&disp[2 * (from * groups + to)], &x, 4);
			memcpy(&disp[2 * (from * groups + to) + 1], &y, 4);
		}
	}
}

// AddLightToList (hw_dynlightdata.cpp, forceAttenuate false) into a slot
// record; the position stays in the light's own portal group.
void PackRecord(FDynamicLight *light, bool enabled, LevelMeshLightRecord &out)
{
	memset(&out, 0, sizeof(out));
	out.pos[0] = float(light->Pos.X);
	out.pos[1] = float(light->Pos.Z);
	out.pos[2] = float(light->Pos.Y);
	out.radius = enabled ? light->GetRadius() : 0.f;

	uint32_t cls = 0;
	float cs;
	if (light->IsAdditive())
	{
		cs = 0.2f;
		cls = 2;
	}
	else
	{
		cs = 1.0f;
	}
	if (light->target && (light->target->renderflags2 & RF2_LIGHTMULTALPHA))
		cs *= (float)light->target->Alpha;
	cs *= (float)light->GetLightDefIntensity();
	float r = light->GetRed() / 255.0f * cs;
	float g = light->GetGreen() / 255.0f * cs;
	float b = light->GetBlue() / 255.0f * cs;
	if (light->IsSubtractive())
	{
		DVector3 v(r, g, b);
		float length = (float)v.Length();
		r = length - r;
		g = length - g;
		b = length - b;
		cls = 1;
	}
	out.color[0] = r;
	out.color[1] = g;
	out.color[2] = b;

	float shadowIndex = screen->mShadowMap.Enabled() ? light->mShadowmapIndex + 1.0f : 1025.f;
	if (light->IsAttenuated()) shadowIndex = -shadowIndex;
	out.shadowIndex = shadowIndex;

	if (light->IsSpot())
	{
		out.lightType = 1.0f;
		out.spotCosInner = (float)light->pSpotInnerAngle->FastCos();
		out.spotCosOuter = (float)light->pSpotOuterAngle->FastCos();
		DAngle negPitch = -light->Pitch;
		DAngle Angle = light->Yaw;
		double xzLen = negPitch.FastCos();
		out.spotDir[0] = float(-Angle.FastCos() * xzLen);
		out.spotDir[1] = float(-negPitch.FastSin());
		out.spotDir[2] = float(-Angle.FastSin() * xzLen);
	}

	int lf = *light->pLightFlags;
	uint32_t flags = cls;
	if (lf & LF_DONTLIGHTSELF) flags |= LMLF_DONTLIGHTSELF;
	if (lf & LF_DONTLIGHTOTHERS) flags |= LMLF_DONTLIGHTOTHERS;
	if (lf & LF_DONTLIGHTACTORS) flags |= LMLF_DONTLIGHTACTORS;
	if (light->DontLightMap()) flags |= LMLF_DONTLIGHTMAP;
	if (light->IsAdditive()) flags |= LMLF_ADDITIVE;
	if (light->IsVisibleToPlayer()) flags |= LMLF_VISIBLETOPLAYER;
	flags |= (uint32_t)(light->Sector ? light->Sector->PortalGroup : 0) << LMLF_GROUPSHIFT;
	out.flags = flags;
	out.ownerActor = LevelMesh_ActorId(light->target);
	out.refineRadius = enabled && light->IsActive() ? std::max(light->radius, light->GetRadius()) : 0.f;
}

// What the refined cluster lists depend on: position, refine radius, portal group.
bool RefineChanged(const LevelMeshLightRecord &a, const LevelMeshLightRecord &b)
{
	return memcmp(a.pos, b.pos, sizeof(a.pos)) != 0 || a.refineRadius != b.refineRadius || (a.flags >> LMLF_GROUPSHIFT) != (b.flags >> LMLF_GROUPSHIFT);
}

uint32_t AllocBlock(LevelMeshLightState &st, unsigned cls)
{
	if (cls < st.FreeBlocks.Size() && st.FreeBlocks[cls].Size() > 0)
	{
		uint32_t b;
		st.FreeBlocks[cls].Pop(b);
		return b;
	}
	unsigned words = 1u << cls;
	while (st.PoolTop + words > st.PoolWords) st.PoolWords *= 2;
	uint32_t b = st.PoolTop;
	st.PoolTop += words;
	return b;
}

void FreeBlock(LevelMeshLightState &st, uint32_t block, unsigned cls)
{
	if (st.FreeBlocks.Size() <= cls) st.FreeBlocks.Resize(cls + 1);
	st.FreeBlocks[cls].Push(block);
}

TArray<FDynamicLight *> &OwnerLights(FLevelLocals *Level, const LevelMeshLightState &st, unsigned owner)
{
	if (owner < st.NumSides) return Level->sides[owner].dlist;
	return Level->sections.allSections[owner - st.NumSides].dlist;
}

void MarkClusterDirty(LevelMeshLightState &st, uint32_t c)
{
	if (c >= st.ClusterDirtyFlag.Size() || st.ClusterDirtyFlag[c]) return;
	st.ClusterDirtyFlag[c] = 1;
	st.DirtyClusters.Push(c);
}

void MarkSectionClustersDirty(LevelMeshLightState &st, unsigned section)
{
	if (section + 1 >= st.SectionClusterStart.Size()) return;
	for (uint32_t k = st.SectionClusterStart[section]; k < st.SectionClusterStart[section + 1]; k++)
		MarkClusterDirty(st, st.SectionClusters[k]);
}

int OwnerGroup(FLevelLocals *Level, const LevelMeshLightState &st, unsigned owner)
{
	sector_t *sec = owner < st.NumSides ? Level->sides[owner].sector : Level->sections.allSections[owner - st.NumSides].sector;
	return sec ? sec->PortalGroup : 0;
}

} // namespace

// CODE --------------------------------------------------------------------

uint32_t LevelMesh_ActorId(AActor *actor)
{
	if (actor == nullptr) return 0;
	uintptr_t p = (uintptr_t)actor;
	uint32_t id = uint32_t(p) ^ uint32_t(uint64_t(p) >> 32);
	return id ? id : 1;
}

bool LevelMesh_LightLists(FLevelLocals *Level)
{
	LevelMeshBake *bake = Level ? Level->levelMeshBake : nullptr;
	return bake && lm_lights && bake->Lights.NumOwners && screen->LevelMeshResident(bake->Id);
}

int LevelMesh_SideLightOwner(FLevelLocals *Level, side_t *side)
{
	return side ? side->Index() : -1;
}

int LevelMesh_SectionLightOwner(FLevelLocals *Level, FSection *section)
{
	if (section == nullptr || Level->levelMeshBake == nullptr) return -1;
	return int(Level->levelMeshBake->Lights.NumSides) + Level->sections.SectionIndex(section);
}

//==========================================================================
//
// Playsim hooks
//
//==========================================================================

void LevelMesh_LightTouched(FDynamicLight *light)
{
	LevelMeshBake *bake = light->Level ? light->Level->levelMeshBake : nullptr;
	if (bake == nullptr || bake->Lights.NumOwners == 0) return;
	auto &st = bake->Lights;
	FLevelLocals *Level = light->Level;
	for (side_t *side : light->touchlists.wall_tlist)
		if (side) MarkOwnerDirty(st, side->Index());
	for (FSection *sec : light->touchlists.flat_tlist)
		if (sec) MarkOwnerDirty(st, st.NumSides + Level->sections.SectionIndex(sec));
}

void LevelMesh_LightReleased(FDynamicLight *light)
{
	LevelMeshBake *bake = light->Level ? light->Level->levelMeshBake : nullptr;
	int slot = light->lmSlot;
	light->lmSlot = -1;
	if (bake == nullptr || slot < 0) return;
	auto &st = bake->Lights;
	if ((unsigned)slot >= st.SlotLight.Size() || st.SlotLight[slot] != light) return;
	// The light was unlinked before: its lists drop it this frame.
	st.SlotLight[slot] = nullptr;
	st.RecordValid[slot] = 0;
	st.FreeSlots.Push(slot);
	st.LiveSlots--;
}

//==========================================================================
//
// LevelMesh_ResetLights
//
//==========================================================================

void LevelMesh_ResetLights(FLevelLocals *Level, LevelMeshBake *bake, LevelMeshUploadData &data)
{
	auto &st = bake->Lights;
	st = {};
	st.NumSides = Level->sides.Size();
	st.NumOwners = st.NumSides + Level->sections.allSections.Size();

	LeafSections.Resize(Level->subsectors.Size());
	for (unsigned i = 0; i < Level->subsectors.Size(); i++)
	{
		FSection *sec = Level->subsectors[i].section;
		LeafSections[i] = sec ? (uint32_t)Level->sections.SectionIndex(sec) : 0;
	}
	FillInfo(Level, bake, st, st.Info);

	// Every light gets a slot (and every list its lights) at the first update.
	unsigned lights = 0, entries = 0;
	for (FDynamicLight *light = Level->lights; light; light = light->next)
	{
		light->lmSlot = -1;
		lights++;
	}
	st.SlotCapacity = RoundUpPow2(lights * 2, MIN_SLOTS);
	st.SlotLight.Resize(st.SlotCapacity);
	for (auto &l : st.SlotLight) l = nullptr;
	st.Records.Resize(st.SlotCapacity);
	st.RecordValid.Resize(st.SlotCapacity);
	for (auto &v : st.RecordValid) v = 0;

	st.OwnerBlock.Resize(st.NumOwners);
	for (auto &b : st.OwnerBlock) b = LM_LIGHTLIST_END;
	st.OwnerClass.Resize(st.NumOwners);
	for (auto &c : st.OwnerClass) c = 0;
	st.OwnerDirtyFlag.Resize(st.NumOwners);
	for (auto &f : st.OwnerDirtyFlag) f = 0;
	for (unsigned i = 0; i < st.NumOwners; i++)
	{
		auto &dlist = OwnerLights(Level, st, i);
		if (dlist.Size() == 0) continue;
		entries += (1u << PowerOfTwoClass(dlist.Size() + 2));
		MarkOwnerDirty(st, i);
	}
	st.PoolWords = RoundUpPow2(entries * 2, MIN_POOL_WORDS);

	// Section -> clusters of the flats reading the cluster's refined list.
	unsigned numSections = Level->sections.allSections.Size();
	TArray<uint32_t> pairs;	// (section, cluster) per refinable flat
	for (auto &surf : bake->Surfaces)
	{
		if (surf.cluster == LM_LIGHTLIST_END || surf.leaf >= LeafSections.Size()) continue;
		pairs.Push(LeafSections[surf.leaf]);
		pairs.Push(surf.cluster);
	}
	st.SectionClusterStart.Resize(numSections + 1);
	for (auto &v : st.SectionClusterStart) v = 0;
	for (unsigned i = 0; i < pairs.Size(); i += 2) st.SectionClusterStart[pairs[i] + 1]++;
	for (unsigned i = 0; i < numSections; i++) st.SectionClusterStart[i + 1] += st.SectionClusterStart[i];
	st.SectionClusters.Resize(pairs.Size() / 2);
	TArray<uint32_t> fill(numSections, true);
	for (unsigned i = 0; i < numSections; i++) fill[i] = st.SectionClusterStart[i];
	for (unsigned i = 0; i < pairs.Size(); i += 2) st.SectionClusters[fill[pairs[i]]++] = pairs[i + 1];
	st.ClusterDirtyFlag.Resize(bake->Clusters.Size());
	for (auto &f : st.ClusterDirtyFlag) f = 0;
	st.Refine = lm_lightrefine;

	data.leafSections = LM_Span(LeafSections);
	data.lightInfo = LM_Span(st.Info);
	data.numLightOwners = st.NumOwners;
	data.lightSlots = st.SlotCapacity;
	data.lightPoolWords = st.PoolWords;
}

//==========================================================================
//
// LevelMesh_UpdateLights
//
// Called for every LevelMesh_UpdateState (every rendered viewpoint), after
// CollectLights assigned the shadowmap indices: the records of every light
// are compared, the dirty lists rebuilt.
//
//==========================================================================

void LevelMesh_UpdateLights(FLevelLocals *Level, LevelMeshBake *bake, const TArray<LevelMeshClusterBox> &movedClusters, LevelMeshStateUpdate &update)
{
	auto &st = bake->Lights;
	RecIndices.Clear();
	RecRecords.Clear();
	Words.Clear();
	RefineClusters.Clear();
	st.Uploading.Clear();
	st.UploadingClusters.Clear();
	if (st.NumOwners != Level->sides.Size() + Level->sections.allSections.Size()) return;

	LevelMeshLightTime.Clock();
	const bool enabled = Level->HasDynamicLights && lm_lights;

	// Records: a slot for every new light, the changed records.
	for (FDynamicLight *light = Level->lights; light; light = light->next)
	{
		int slot = light->lmSlot;
		if (slot < 0 || (unsigned)slot >= st.SlotLight.Size() || st.SlotLight[slot] != light)
		{
			uint32_t s;
			if (st.FreeSlots.Size() > 0) st.FreeSlots.Pop(s);
			else
			{
				if (st.SlotTop >= st.SlotCapacity)
				{
					st.SlotCapacity *= 2;
					st.SlotLight.Resize(st.SlotCapacity);
					st.Records.Resize(st.SlotCapacity);
					st.RecordValid.Resize(st.SlotCapacity);
					for (unsigned i = st.SlotTop; i < st.SlotCapacity; i++)
					{
						st.SlotLight[i] = nullptr;
						st.RecordValid[i] = 0;
					}
				}
				s = st.SlotTop++;
			}
			slot = light->lmSlot = (int)s;
			st.SlotLight[s] = light;
			st.RecordValid[s] = 0;
			st.LiveSlots++;
		}
		LevelMeshLightRecord rec;
		PackRecord(light, enabled, rec);
		bool valid = st.RecordValid[slot];
		if (valid && memcmp(&rec, &st.Records[slot], sizeof(rec)) == 0) continue;
		// A moved, resized or (de)activated light: the refined lists it may
		// enter or leave (lists it was linked to that are not in its touch
		// lists anymore were marked by the unlink).
		if (!valid || RefineChanged(rec, st.Records[slot]))
		{
			for (FSection *sec : light->touchlists.flat_tlist)
				if (sec) MarkSectionClustersDirty(st, Level->sections.SectionIndex(sec));
		}
		st.Records[slot] = rec;
		st.RecordValid[slot] = 1;
		RecIndices.Push(slot);
		RecRecords.Push(rec);
	}

	// Light info: the portal displacements move with linked portals.
	FillInfo(Level, bake, st, InfoScratch);
	bool allInfo = InfoScratch.Size() != st.Info.Size();	// after an update that was not uploaded
	st.Info.Resize(InfoScratch.Size());
	for (unsigned i = 0; i < InfoScratch.Size(); i++)
	{
		if (!allInfo && InfoScratch[i] == st.Info[i]) continue;
		st.Info[i] = InfoScratch[i];
		Words.Push({ LMLW_INFO | i, InfoScratch[i] });
	}

	// Dirty lists.
	unsigned owners = 0;
	for (uint32_t owner : st.DirtyOwners)
	{
		st.OwnerDirtyFlag[owner] = 0;
		st.Uploading.Push(owner);
		owners++;
		if (owner >= st.NumSides) MarkSectionClustersDirty(st, owner - st.NumSides);
		ListSlots.Clear();
		for (FDynamicLight *light : OwnerLights(Level, st, owner))
			if (light->lmSlot >= 0) ListSlots.Push(light->lmSlot);
		std::sort(ListSlots.begin(), ListSlots.end());
		uint32_t block = st.OwnerBlock[owner];
		if (ListSlots.Size() == 0)
		{
			if (block != LM_LIGHTLIST_END) FreeBlock(st, block, st.OwnerClass[owner]);
			st.OwnerBlock[owner] = LM_LIGHTLIST_END;
			Words.Push({ LMLW_OWNERS | owner, LM_LIGHTLIST_END });
			continue;
		}
		unsigned cls = PowerOfTwoClass(ListSlots.Size() + 2);
		if (block == LM_LIGHTLIST_END || st.OwnerClass[owner] != cls)
		{
			if (block != LM_LIGHTLIST_END) FreeBlock(st, block, st.OwnerClass[owner]);
			block = AllocBlock(st, cls);
			st.OwnerBlock[owner] = block;
			st.OwnerClass[owner] = (uint8_t)cls;
		}
		Words.Push({ LMLW_POOL | block, (uint32_t)OwnerGroup(Level, st, owner) });
		for (unsigned i = 0; i < ListSlots.Size(); i++) Words.Push({ LMLW_POOL | (block + 1 + i), ListSlots[i] });
		Words.Push({ LMLW_POOL | (block + 1 + ListSlots.Size()), LM_LIGHTLIST_END });
		Words.Push({ LMLW_OWNERS | owner, block });
	}
	st.DirtyOwners.Clear();

	// Refined cluster lists: the clusters of changed lists and lights, the
	// moved cluster boxes; all of them when lm_lightrefine flips.
	for (auto &box : movedClusters) MarkClusterDirty(st, box.cluster);
	if (st.Refine != (bool)lm_lightrefine)
	{
		st.Refine = lm_lightrefine;
		for (uint32_t c : st.SectionClusters) MarkClusterDirty(st, c);
	}
	for (uint32_t c : st.DirtyClusters)
	{
		st.ClusterDirtyFlag[c] = 0;
		RefineClusters.Push(c);
	}
	st.DirtyClusters.Clear();
	st.UploadingClusters = RefineClusters;

	update.lightRecIndices = LM_Span(RecIndices);
	update.lightRecs = LM_Span(RecRecords);
	update.lightWords = LM_Span(Words);
	update.lightSlots = st.SlotCapacity;
	update.lightPoolWords = st.PoolWords;
	update.lightClusters = LM_Span(RefineClusters);
	update.lightRefine = st.Refine;
	LevelMeshLightTime.Unclock();

	lmLightStats.slots = st.LiveSlots;
	lmLightStats.records += RecIndices.Size();
	lmLightStats.owners += owners;
	lmLightStats.poolWords += Words.Size();
	lmLightStats.clustersRefined += RefineClusters.Size();
}

void LevelMesh_LightsUploaded(LevelMeshBake *bake, bool uploaded)
{
	if (uploaded) return;
	// Not on the device: send the records and lists again next update.
	auto &st = bake->Lights;
	for (uint32_t slot : RecIndices) st.RecordValid[slot] = 0;
	for (uint32_t owner : st.Uploading) MarkOwnerDirty(st, owner);
	for (uint32_t c : st.UploadingClusters) MarkClusterDirty(st, c);
	st.Info.Clear();
	st.Uploading.Clear();
	st.UploadingClusters.Clear();
}

//==========================================================================
//
// LevelMesh_InlineLightList
//
// hw_GetDynModelLight's big-actor set: the lights of the sections of the
// subsectors within the actor's render radius whose sphere touches it,
// written as a list into the frame's light buffer (the float bits of the
// list words). Its group is the actor's.
//
//==========================================================================

int LevelMesh_InlineLightList(FLevelLocals *Level, AActor *actor)
{
	LevelMeshBake *bake = Level->levelMeshBake;
	auto &st = bake->Lights;
	int fallback = LevelMesh_SectionLightOwner(Level, actor->section);
	if (InlineStamp.Size() < st.SlotCapacity)
	{
		unsigned old = InlineStamp.Size();
		InlineStamp.Resize(st.SlotCapacity);
		for (unsigned i = old; i < InlineStamp.Size(); i++) InlineStamp[i] = 0;
	}
	if (++InlineSerial == 0)
	{
		for (auto &s : InlineStamp) s = 0;
		InlineSerial = 1;
	}

	float actorradius = (float)actor->RenderRadius();
	float x = (float)actor->X();
	float y = (float)actor->Y();
	float z = (float)actor->Center();
	auto &slots = InlineSlots;
	slots.Clear();
	BSPWalkCircle(Level, x, y, actorradius * actorradius, [&](subsector_t *subsector)
	{
		for (FDynamicLight *light : subsector->section->dlist)
		{
			int slot = light->lmSlot;
			if (slot < 0 || (unsigned)slot >= InlineStamp.Size() || InlineStamp[slot] == InlineSerial) continue;
			int group = subsector->sector->PortalGroup;
			DVector3 pos = light->PosRelative(group);
			float radius = (float)(light->GetRadius() + actorradius);
			double dx = pos.X - x;
			double dy = pos.Y - y;
			double dz = pos.Z - z;
			if (dx * dx + dy * dy + dz * dz < radius * radius)
			{
				InlineStamp[slot] = InlineSerial;
				slots.Push(slot);
			}
		}
	});
	std::sort(slots.begin(), slots.end());

	// [group, slots..., end], padded to whole vec4 with end words.
	InlineData.Clear();
	auto push = [](uint32_t w)
	{
		float f;
		memcpy(&f, &w, 4);
		InlineData.arrays[0].Push(f);
	};
	push((uint32_t)(actor->Sector ? actor->Sector->PortalGroup : 0));
	for (uint32_t slot : slots) push(slot);
	push(LM_LIGHTLIST_END);
	while (InlineData.arrays[0].Size() & 3) push(LM_LIGHTLIST_END);
	int index = screen->mLights->UploadLights(InlineData);
	lmLightStats.inlineLists++;
	if (index < 0) return fallback;
	// The list follows the vec4 of range counts UploadLights writes first.
	return -(2 + (index + 1) * 4);
}
