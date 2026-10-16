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
	unsigned wallTextureNotSide = 0;			// walls drawn with a texture that is not their side tier's (animation not followed)
	unsigned ffloorVRefHeuristic = 0;			// 3D-floor sides whose vRef was taken from the top-left anchor
	unsigned edgeUnbounded = 0;					// walls left without edgeTop (a non-top vertex above it at load)
	unsigned deferredSurfaces = 0;				// LMSF_DEFERRED: drawn by the classic path (ticket 08)
	unsigned deferredByTier = 0;				// of those, deferred because another piece of the side tier is
	unsigned lights3D = 0;						// distinct 3D lights
	unsigned light3DSurfaces = 0;				// surfaces lit by one (lightList >= 0)
	unsigned bandedWalls = 0;					// walls the classic draws with split planes (HWWall::lightlist)
	unsigned bandSurfaces = 0;					// the light-layer surfaces they were cut into
	unsigned bandsDeferred = 0;					// banded walls left to the classic (LMSF_LIGHTLIST)
	unsigned bandPlaneUnmatched = 0;			// of those: a layer plane that is no sector plane
	unsigned edgeClamped = 0;					// tiers with an edgeClamp (HWWall::Process tier clamp)
	unsigned ffGroups = 0, ffGroupsDeferred = 0;	// 3D-floor groups, of those deferred at bake
	unsigned ffWatches = 0;
	unsigned ffFlatsNotPicked = 0;				// opaque 3D-floor planes the classic never draws (ProcessSector's range checks)
	double wallMs = 0, flatMs = 0, totalMs = 0;
};

// The vertices of a cluster anchored to one plane: their z is
// LM_PlaneZ(plane, x, y) + [minOffset, maxOffset].
struct LevelMeshClusterPlane
{
	uint32_t ref;			// plane ref
	float minOffset, maxOffset;
};

// One dirty-only record stream (side tiers, plane xforms): the records as
// last handed to the backend and the candidate lists (hw_levelmeshstate.cpp).
template<class T> struct LevelMeshRecordStream
{
	TArray<T> Records;
	TArray<uint32_t> Stamp;				// last update that compared the record
	TArray<uint32_t> Changed;			// changed by this frame's updates
	TArray<uint32_t> ChangedBefore;		// changed by the previous frame's updates
	TArray<uint32_t> Touched;			// reported by a change hook since the last update
	TArray<uint8_t> TouchedFlag;
	unsigned SweepCursor = 0;

	void Reset(const TArray<T> &records)
	{
		Records = records;
		Stamp.Resize(records.Size());
		for (auto &s : Stamp) s = 0;
		TouchedFlag.Resize(records.Size());
		for (auto &f : TouchedFlag) f = 0;
		Touched.Clear();
		Changed.Clear();
		ChangedBefore.Clear();
		SweepCursor = 0;
	}

	void Touch(unsigned i)
	{
		if (i >= TouchedFlag.Size() || TouchedFlag[i]) return;
		TouchedFlag[i] = 1;
		Touched.Push(i);
	}
};

// What a side tier's baked surfaces assume about its texture state: the
// FTexCoordInfo the u / v were computed with. A state that changes any of it
// (another texture size, a side scale) defers the tier to the classic path
// (LMTS_DEFERRED); offsets are applied by the vertex stage.
struct LevelMeshTierBase
{
	int renderWidth = 0, renderHeight = 0;
	float scaleX = 0, scaleY = 0;
	bool worldPanning = false;
	bool valid = false;			// the tier has levelmesh surfaces
	bool midClip = false;		// two-sided mid texture without wrap: the y offset moves its geometry
	float yOffset = 0;			// at bake (midClip)
	int program = LMP_DEFAULT;	// material program (bucket)
};

// What identifies a 3D light (LevelMeshLight3D) across P_Recalculate3DFloors:
// the lightlist rebuild recreates the clipped (FF_DYNAMIC) copies of the 3D
// floors, so the light is keyed by the values the classic reads, not by
// F3DFloor pointers. LevelMesh_FillLight3D finds the current entry by it.
struct LevelMeshLight3DSource
{
	uint32_t sector;				// the sector whose lightlist it is
	sector_t *lightsourceModel;		// lightsource->model, nullptr: the sector's own colormap
	int lightsourceFlags;			// lightsource->flags & (FF_FADEWALLS | FF_FOG) (F3DFloor::GetColormap)
	short *lightLevel;				// p_lightlevel
	bool casterFadeWalls;			// caster && (caster->flags & FF_FADEWALLS) (CopyFrom3DLight)

	bool operator==(const LevelMeshLight3DSource &o) const
	{
		return sector == o.sector && lightsourceModel == o.lightsourceModel && lightsourceFlags == o.lightsourceFlags &&
			lightLevel == o.lightLevel && casterFadeWalls == o.casterFadeWalls;
	}
};

// A plane the bake's split and 3D lighting of a sector depends on, with its
// value at bake (LevelMeshBake::LayoutPlanes).
struct LevelMeshLayoutPlane
{
	uint32_t ref;
	LevelMeshPlane plane;
};

// What a 3D-floor group's baked surfaces assume (LevelMeshBake::FFWatches):
// a plane at its bake value (the classic clips and picks 3D-floor sides and
// planes by it), a plane texture, or a side tier's texture / offsets /
// scales (the u / v of a 3D-floor side come from its side's and the 3D
// floor's master side's offsets).
enum ELevelMeshFFWatchKind : uint32_t
{
	LMFW_PLANE,			// ref = plane ref
	LMFW_PLANETEX,		// ref = plane ref; also its plane colors
	LMFW_TIER,			// ref = side * 3 + tier; with 'colors' also the side colors of that tier
};

struct LevelMeshFFWatch
{
	uint32_t kind;
	uint32_t ref;
	LevelMeshPlane plane;
	int32_t textureId;
	float xOffset, yOffset, xScale, yScale;
	bool colors;
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
	LevelMeshRecordStream<LevelMeshSideState> Sides;		// side * 3 + tier
	LevelMeshRecordStream<LevelMeshPlaneXform> Xforms;		// plane ref
	LevelMeshRecordStream<LevelMeshLight3D> Lights3D;		// LevelMeshBake::Lights3D
	LevelMeshRecordStream<LevelMeshSideState> FFGroups;		// 3D-floor groups (GPU side record numsides * 3 + group)
	TArray<uint8_t> LayoutMoved;			// per sector: its 3D-floor layout differs from the bake (LML3_DEFERRED)
	TArray<uint32_t> LayoutRecheck;			// sectors whose layout must be compared again (an update that was not uploaded)
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
	~LevelMeshBake();	// detaches the change hooks (hw_levelmeshstate.cpp)

	// Classic draw-list entries the levelmesh replaces (1 = owned): per side
	// tier (side * 3 + side_t::top/mid/bottom) and per sector plane
	// (sector * 2 + plane). Everything else stays on the classic path.
	TArray<uint8_t> SideTierOwned;
	TArray<uint8_t> SectorPlaneOwned;
	// The same at bake; the state stream clears an entry of the arrays above
	// while the tier / plane is LMTS_DEFERRED.
	TArray<uint8_t> SideTierBaked;
	TArray<uint8_t> SectorPlaneBaked;
	TArray<LevelMeshTierBase> TierBase;	// per side tier
	TArray<int8_t> PlaneProgram;		// per sector plane: material program of its surfaces (-2 = none)
	TArray<uint8_t> PlanePortal;		// per sector plane: baked with a sector portal (HWFlat draws it opaque over the portal)

	// 3D lights (LevelMeshSurface::lightList), and per sector with 3D floors
	// (CSR, numsectors + 1 entries each): its lights; the planes its
	// surfaces were split and lit by (its own and its 3D floors' model
	// planes) at their bake values. Per sector, the sectors whose 3D-floor
	// layout depends on its planes, and the lights that read its light
	// level or colormap.
	TArray<LevelMeshLight3DSource> Lights3D;
	TArray<uint32_t> SectorLightStart, SectorLights;
	TArray<uint32_t> LayoutStart;
	TArray<LevelMeshLayoutPlane> LayoutPlanes;
	TArray<uint32_t> LayoutDependStart, LayoutDepend;
	TArray<uint32_t> LightDependStart, LightDepend;

	// 3D-floor groups: the 3D-floor sides of one side, and the opaque
	// 3D-floor planes of one sector, are drawn by the levelmesh or by the
	// classic path together (like a side tier). A group's surfaces have
	// sideRef = numsides * 3 + group: its streamed record
	// (LevelMesh_FillFFGroup) is LMTS_DEFERRED while one of its watches
	// differs from the bake (a moving 3D floor, a texture change), which
	// hides them and gives the classic draw lists back the group.
	TArray<uint32_t> SideFFGroup;		// per side: group, ~0u = none
	TArray<uint32_t> SectorFFGroup;		// per sector: group, ~0u = none
	TArray<uint8_t> FFGroupBaked;		// per group: all its surfaces are drawable at bake
	TArray<uint8_t> FFGroupOwned;		// per group: the levelmesh draws it now
	TArray<uint32_t> FFWatchStart;		// group -> watches (CSR)
	TArray<LevelMeshFFWatch> FFWatches;
	TArray<uint32_t> FFSectorDependStart, FFSectorDepend;	// sector -> groups watching its planes
	TArray<uint32_t> FFSideDependStart, FFSideDepend;		// side -> groups watching its tiers

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

// Packs a sector's / side tier's / plane's GPU state record. The side and
// plane records carry LMTS_DEFERRED when the current state is not
// expressible by the bake's surfaces.
void LevelMesh_FillSectorState(sector_t &sec, LevelMeshSectorState &out);
void LevelMesh_FillSideState(const LevelMeshBake *bake, side_t &side, int tier, LevelMeshSideState &out);
void LevelMesh_FillPlaneXform(const LevelMeshBake *bake, sector_t &sec, int pos, LevelMeshPlaneXform &out);
// A 3D-floor group's record (LevelMeshBake::SideFFGroup): LMTS_DEFERRED
// while a watch differs. 'sectors': the sector records the planes are
// compared with (as uploaded).
void LevelMesh_FillFFGroup(FLevelLocals *Level, const LevelMeshBake *bake, unsigned group,
	const TArray<LevelMeshSectorState> &sectors, LevelMeshSideState &out);
// 'layoutMoved': the light's sector's 3D-floor layout differs from the bake.
void LevelMesh_FillLight3D(FLevelLocals *Level, const LevelMeshLight3DSource &src, bool layoutMoved, LevelMeshLight3D &out);

// The levelmesh program (ELevelMeshProgram) of a texture's material shader,
// -1 for material shaders the levelmesh cannot draw yet (classic path).
int LevelMesh_MaterialProgram(FGameTexture *tex);
bool LevelMesh_NeedsClassicMaterial(FGameTexture *tex);

// Side / plane colors and texture manipulation HWWall::RenderTexturedWall /
// HWFlat::DrawFlat apply (not expressible by the resolve yet: classic path).
bool LevelMesh_HasWallColorEffects(side_t *side, int tier, sector_t *frontsector);
bool LevelMesh_HasPlaneColorEffects(sector_t *sec, int pos);

// Resets the dirty state stream to the records just uploaded and attaches
// the change hooks to the level.
void LevelMesh_ResetState(FLevelLocals *Level, LevelMeshBake *bake, const TArray<LevelMeshSectorState> &sectors,
	const TArray<LevelMeshSideState> &sides, const TArray<LevelMeshPlaneXform> &xforms, const TArray<LevelMeshLight3D> &lights3D);

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
