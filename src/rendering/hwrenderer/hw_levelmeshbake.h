/*
** hw_levelmeshbake.h
**
** Levelmesh bake: static level geometry produced by the classic emitters
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** The bake runs once at level setup when the levelmesh path is selected
** (gl_uselevelmesh on a capable backend). Walls come from HWWall::Process in
** mesh-helper mode, flats from the classic subsector triangulation
** (ADR 0003). Every vertex is plane-anchored: its z is the GPU-identical
** plane evaluation LM_PlaneZ() of its anchor plane plus a baked offset, and
** its wall v coordinate follows the surface's texture-top plane. Nothing is
** re-baked when planes move.
**
** View-dependent emitter output (translucent walls, portals, fog boundaries,
** missing-texture fills, sky / portal / translucent flats) is kept in side
** lists, not in the mesh.
*/

#pragma once

// HEADER FILES ------------------------------------------------------------

#include "tarray.h"
#include "r_defs.h"
#include "hwrenderer/scene/hw_drawinfo.h"
#include "hwrenderer/scene/hw_drawstructs.h"
#include "hwrenderer/scene/hw_walldispatcher.h"
#include "hwrenderer/data/hw_levelmeshdata.h"

// TYPES -------------------------------------------------------------------

struct FLevelLocals;
struct HWDrawInfo;

inline LevelMeshPlane LM_PackPlane(const secplane_t &plane)
{
	return { (float)plane.Normal().X, (float)plane.Normal().Y, (float)plane.fD(), (float)plane.negiC };
}

// A flat that is not part of the opaque mesh (sky / portal / translucent).
struct LevelMeshFlatRef
{
	subsector_t *sub;
	sector_t *sector;		// owner of the plane (model sector for 3D floors)
	F3DFloor *ffloor;		// nullptr for sector planes
	int plane;				// sector_t::floor / ceiling
};

struct LevelMeshBakeStats
{
	unsigned wallSurfaces = 0, flatSurfaces = 0, ffloorFlatSurfaces = 0;
	unsigned vertices = 0, triangles = 0;
	unsigned sidesProcessed = 0, polySidesSkipped = 0, heightsecSurfaces = 0;
	unsigned translucentWalls = 0, portalWalls = 0, fogWalls = 0, upperMissing = 0, lowerMissing = 0;
	unsigned translucentFlats = 0, portalFlats = 0, untexturedFlats = 0;
	unsigned zMismatch = 0, vMismatch = 0;		// round-trip failures beyond 1 ulp (must be 0)
	unsigned zInexact = 0, vInexact = 0;		// reproduced within 1 ulp of the operands, not bit-exact
	unsigned offsetAnchored[4] = {};			// per role: no plane matched exactly, anchored with an offset
	unsigned vScaleInconsistent = 0;			// left/right dv/dz disagree
	unsigned planeRefOverflow = 0;				// surfaces with more than 4 distinct anchor planes
	unsigned ffloorVRefHeuristic = 0;			// 3D-floor sides whose vRef was taken from the top-left anchor
	unsigned edgeUnbounded = 0;					// walls left without edgeTop (a non-top vertex above it at load)
	unsigned deferredSurfaces = 0;				// LMSF_DEFERRED: drawn by the classic path (ticket 08)
	unsigned deferredByTier = 0;				// of those, deferred because another piece of the side tier is
	double wallMs = 0, flatMs = 0, totalMs = 0;
};

// The vertices of a cluster anchored to one plane: their z is
// LM_PlaneZ(plane, x, y) + [minOffset, maxOffset].
struct LevelMeshClusterPlane
{
	uint32_t ref;			// plane ref
	float minOffset, maxOffset;
};

// The dirty state stream's bookkeeping (hw_levelmeshstate.cpp).
struct LevelMeshStateStream
{
	TArray<LevelMeshSectorState> Sectors;	// the records as last handed to the backend
	TArray<uint32_t> SectorStamp, ClusterStamp;	// last update that compared / refreshed them
	TArray<uint32_t> Changed;				// sectors changed by this frame's updates
	TArray<uint32_t> ChangedBefore;			// sectors changed by the previous frame's updates
	TArray<uint32_t> Touched;				// reported by LevelMesh_SectorChanged since the last update
	TArray<uint8_t> TouchedFlag;
	TArray<uint32_t> AnimatedGlow;			// sectors whose wall glow follows a texture animation
	uint64_t Frame = 0;						// LevelMesh_BeginFrame serial of the last update
	uint32_t Stamp = 0;
	unsigned SweepCursor = 0;
};

struct LevelMeshClusterStats
{
	unsigned clusters = 0, triangles = 0;
	unsigned budget = 0;
	unsigned minTris = 0, maxTris = 0, p50Tris = 0, p90Tris = 0;
	unsigned histogram[9] = {};		// triangles per cluster in 32-wide bins; the last bin is > 256
	unsigned chunkedLeaves = 0;		// leaves over budget, split into chunks
	unsigned chunkClusters = 0;		// clusters produced by those splits
	unsigned mixedBucketClusters = 0;
	unsigned splitSurfaces = 0;		// surfaces straddling a chunk boundary (listed in both clusters)
	unsigned clusterSectorRefs = 0;
	unsigned maxSectorClusters = 0;	// largest sector -> cluster fan-out
	unsigned fallbackLeaves = 0;	// walls without a seg subsector, placed by point lookup
	unsigned orphanSurfaces = 0;	// surfaces of leaves outside the BSP (section hole fills), placed by point lookup
	double ms = 0;
};

class LevelMeshBake
{
public:
	TArray<LevelMeshVertex> Vertices;
	TArray<uint32_t> Indices;		// triangle list, cluster-ordered (surface-ordered before clustering), absolute vertex indices
	TArray<LevelMeshSurface> Surfaces;
	TArray<int32_t> SurfaceLeaf;	// BSP leaf (subsector index) per surface; freed after clustering

	// Clusters (LevelMeshClusters_Build). The surface list of a cluster can
	// share its first / last surface with the neighbouring cluster when a
	// leaf over budget was chunked.
	TArray<LevelMeshCluster> Clusters;
	TArray<uint32_t> ClusterSectors;	// cluster -> sectors of its vertex anchors and wall edge bounds
	TArray<uint32_t> ClusterSurfaces;	// cluster -> surfaces
	TArray<uint32_t> SectorClusterStart;	// sector -> clusters (CSR, numsectors + 1 entries)
	TArray<uint32_t> SectorClusters;
	TArray<uint32_t> ClusterPlaneStart;	// cluster -> anchor planes (CSR, numclusters + 1 entries)
	TArray<LevelMeshClusterPlane> ClusterPlanes;

	LevelMeshStateStream Stream;

	// Classic draw-list entries the levelmesh replaces (1 = owned): per side
	// tier (side * 3 + side_t::top/mid/bottom) and per sector plane
	// (sector * 2 + plane). Everything else stays on the classic path.
	TArray<uint8_t> SideTierOwned;
	TArray<uint8_t> SectorPlaneOwned;

	uint64_t Id = 0;				// unique per bake (backend residency)
	bool GeometryReleased = false;	// Vertices / Indices freed after the upload

	// Side lists (tickets 14-16). The HWWall copies keep the classic
	// emitter's view-independent output; pointers into level data stay valid
	// for the level's lifetime (lightlist pointers do not: they can be
	// reallocated by P_Recalculate3DFloors).
	TArray<HWWall> TranslucentWalls, PortalWalls, FogWalls;
	TArray<HWMissing> UpperMissing, LowerMissing;
	TArray<LevelMeshFlatRef> TranslucentFlats, PortalFlats;

	LevelMeshBakeStats Stats;
	LevelMeshClusterStats ClusterStats;

	// Evaluates a vertex's z / v from the current planes (CPU reference of the shader math).
	float VertexZ(FLevelLocals *Level, const LevelMeshVertex &v) const;
	float VertexV(FLevelLocals *Level, const LevelMeshVertex &v, float z) const;

	void PrintStats(const char *mapname) const;
	void PrintClusterStats(const char *mapname) const;
	bool ExportObj(FLevelLocals *Level, const char *filename) const;
};

// PUBLIC FUNCTION PROTOTYPES ----------------------------------------------

// Returns nullptr when the levelmesh path is not selected for this level.
// 'rebuild' re-bakes a level whose path was already latched (device reset).
LevelMeshBake *LevelMeshBake_Create(FLevelLocals *Level, bool rebuild = false);

// Cuts the bake into clusters and orders the index buffer by cluster.
void LevelMeshClusters_Build(FLevelLocals *Level, LevelMeshBake *bake, int budget);

// Makes the level's levelmesh resident on the device: uploads at level setup,
// re-bakes and re-uploads after a device reset, drops the path if the new
// device cannot run it. Called at level setup and once per frame.
void LevelMesh_UpdateResources(FLevelLocals *Level);

// Releases the device resources of the level's levelmesh (level teardown).
void LevelMesh_ReleaseResources(FLevelLocals *Level);

// Packs a sector's GPU state record.
void LevelMesh_FillSectorState(sector_t &sec, LevelMeshSectorState &out);

// Resets the dirty state stream to the records just uploaded.
void LevelMesh_ResetState(FLevelLocals *Level, LevelMeshBake *bake, const TArray<LevelMeshSectorState> &sectors);

// Frame boundary for the state stream (once per rendered frame, before any
// viewpoint).
void LevelMesh_BeginFrame();

// Streams the sector records that changed since the last call (planes, texZ,
// light, colormap, glow) and the z-ranges of the clusters anchored to moved
// planes. Called for every rendered viewpoint, in the D_Render interpolation
// window and before the scene is built, so the uploaded records are the
// values drawn this frame.
void LevelMesh_UpdateState(FLevelLocals *Level);

// The per-view inputs of the shading resolve (hw_levelmeshshade.cpp).
void LevelMesh_FillShading(HWDrawInfo *di, LevelMeshShading &out);

// TArray -> LevelMeshSpan
template<class T> LevelMeshSpan<T> LM_Span(const TArray<T> &a)
{
	return { a.Data(), a.Size() };
}
