/*
** hw_levelmeshresources.cpp
**
** Levelmesh device residency: initial state records and upload
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** The bake is uploaded once at level setup. Afterwards its CPU vertices and
** indices are freed (lm_keepgeometry keeps them for debugging). A device
** reset (vid_restart) loses the device copy; the next frame re-bakes the
** level from its current state and uploads again, or drops the levelmesh
** path for the rest of the level when the new device cannot run it.
*/

// HEADER FILES ------------------------------------------------------------

#include "hw_levelmeshbake.h"
#include "g_levellocals.h"
#include "texturemanager.h"
#include "c_cvars.h"
#include "c_dispatch.h"
#include "printf.h"
#include "v_video.h"

// PUBLIC DATA DEFINITIONS -------------------------------------------------

// Debug: keep the CPU vertices / indices after the upload (lm_dumpobj).
CVAR(Bool, lm_keepgeometry, false, 0)

// PRIVATE DATA DEFINITIONS ------------------------------------------------

namespace
{

void Upload(FLevelLocals *Level, LevelMeshBake *bake)
{
	TArray<LevelMeshSectorState> sectors(Level->sectors.Size(), true);
	TArray<LevelMeshPlaneXform> xforms(Level->sectors.Size() * 2, true);
	TArray<LevelMeshSideState> sides(Level->sides.Size() * 3, true);
	unsigned ntex = TexMan.NumTextures();
	TArray<uint8_t> used(ntex, true);
	memset(used.Data(), 0, ntex);
	auto use = [&](int id) { if (id > 0 && (unsigned)id < ntex) used[id] = 1; };

	for (unsigned i = 0; i < Level->sectors.Size(); i++)
	{
		auto &sec = Level->sectors[i];
		LevelMesh_FillSectorState(sec, sectors[i]);
		for (int p = 0; p < 2; p++)
		{
			LevelMesh_FillPlaneXform(sec, p, xforms[i * 2 + p]);
			use(sec.GetTexture(p).GetIndex());
		}
	}
	for (unsigned i = 0; i < Level->sides.Size(); i++)
	{
		for (int t = 0; t < 3; t++)
		{
			LevelMesh_FillSideState(Level->sides[i], t, sides[i * 3 + t]);
			use(sides[i * 3 + t].textureId);
		}
	}
	for (auto &surf : bake->Surfaces) use(surf.textureId);
	TArray<int> textureIds;
	for (unsigned i = 0; i < ntex; i++)
		if (used[i]) textureIds.Push(i);

	LevelMeshUploadData data;
	data.id = bake->Id;
	data.vertices = LM_Span(bake->Vertices);
	data.indices = LM_Span(bake->Indices);
	data.surfaces = LM_Span(bake->Surfaces);
	data.clusters = LM_Span(bake->Clusters);
	data.clusterSectors = LM_Span(bake->ClusterSectors);
	data.clusterSurfaces = LM_Span(bake->ClusterSurfaces);
	data.sectors = LM_Span(sectors);
	data.sides = LM_Span(sides);
	data.planeXforms = LM_Span(xforms);
	data.textureIds = LM_Span(textureIds);
	data.translationTableSize = ntex;
	screen->SetLevelMeshData(&data);
	LevelMesh_ResetState(Level, bake, sectors, sides, xforms);

	if (!lm_keepgeometry)
	{
		bake->Vertices.Reset();
		bake->Indices.Reset();
		bake->GeometryReleased = true;
	}
}

} // namespace

//==========================================================================
//
// LevelMesh_UpdateResources
//
//==========================================================================

void LevelMesh_UpdateResources(FLevelLocals *Level)
{
	LevelMeshBake *bake = Level->levelMeshBake;
	if (bake == nullptr || screen == nullptr) return;
	if (screen->LevelMeshResident(bake->Id)) return;

	if (bake->GeometryReleased)
	{
		// The device was reset: re-bake from the level's current state.
		Printf("Levelmesh: device reset, rebuilding %s\n", Level->MapName.GetChars());
		delete bake;
		bake = Level->levelMeshBake = LevelMeshBake_Create(Level, true);
		if (bake == nullptr)
		{
			Printf("Levelmesh: the device cannot run the levelmesh path; using the classic renderer for this level\n");
			return;
		}
	}
	else if (!screen->SupportsLevelMesh(true))
	{
		delete bake;
		Level->levelMeshBake = nullptr;
		return;
	}
	Upload(Level, bake);
}

void LevelMesh_ReleaseResources(FLevelLocals *Level)
{
	if (Level->levelMeshBake && screen) screen->SetLevelMeshData(nullptr);
}

//==========================================================================
//
// Console
//
//==========================================================================

CCMD(lm_stats)
{
	if (!primaryLevel || !primaryLevel->levelMeshBake)
	{
		Printf("No levelmesh for this level (gl_uselevelmesh off at level setup, or the device cannot run it).\n");
		return;
	}
	auto bake = primaryLevel->levelMeshBake;
	const char *map = primaryLevel->MapName.GetChars();
	Printf("Levelmesh %s: %u surfaces, %u clusters, %u triangles; CPU geometry %s\n", map, bake->Surfaces.Size(), bake->Clusters.Size(),
		bake->ClusterStats.triangles, bake->GeometryReleased ? "freed" : "kept");
	bake->PrintClusterStats(map);
	screen->PrintLevelMeshStats();
}
