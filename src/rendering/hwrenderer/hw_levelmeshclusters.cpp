/*
** hw_levelmeshclusters.cpp
**
** Levelmesh clusters: BSP subtrees cut at a triangle budget
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** Every surface belongs to one BSP leaf: flats to their subsector, walls to
** the subsector of the seg they were emitted from. The BSP is walked top-down
** and every subtree whose triangle count fits the budget becomes one cluster;
** a single leaf over budget is cut into budget-sized chunks. Inside a
** cluster the surfaces are ordered by material bucket, so each bucket is one
** contiguous index range. The index buffer is rewritten in cluster order and
** the surfaces' index ranges follow it.
**
** Sky surfaces (LMSF_SKY) reach z +-32768: in a cluster with other surfaces
** they would keep its box from ever being occluded. They are clustered on
** their own after the BSP walk, in BSP leaf order.
**
** A baked polyobject's walls (SurfaceLeaf -2 - polyobject) are no BSP leaf's:
** each polyobject gets clusters of its own after the BSP walk, so its
** clusters' boxes can follow it (LevelMeshBake::PolyClusterStart).
**
** The whole build is linear in the number of triangles plus BSP nodes.
*/

// HEADER FILES ------------------------------------------------------------

#include <algorithm>
#include <cassert>

#include "hw_levelmeshbake.h"
#include "g_levellocals.h"
#include "i_time.h"
#include "printf.h"

// PRIVATE DATA DEFINITIONS ------------------------------------------------

namespace
{

uint32_t SurfaceBucket(const LevelMeshSurface &surf)
{
	if (surf.flags & LMSF_SKY) return LM_Bucket(LMP_SKY, false);
	if (surf.flags & LMSF_DEFERRED) return LMB_DEFERRED;
	return LM_Bucket((surf.flags & LMSF_PROGRAMMASK) >> LMSF_PROGRAMSHIFT, (surf.flags & LMSF_MASKED) != 0);
}

struct ClusterBuilder
{
	FLevelLocals *Level;
	LevelMeshBake *Bake;
	unsigned Budget;

	TArray<uint32_t> LeafStart, LeafSurfaces;	// leaf -> surfaces (CSR)
	TArray<uint32_t> LeafTris;
	TArray<uint32_t> NodeTris;
	TArray<uint32_t> Gather;
	TArray<uint32_t> GatherLeaves;				// the BSP leaves of Gather's group (every cluster it is cut into lists them)
	TArray<float> Z;							// vertex z at load
	TArray<uint32_t> OldFirstIndex;
	TArray<uint32_t> NewIndices;
	TArray<uint32_t> SecStamp;
	TArray<uint32_t> PlaneStamp, PlaneSlot;		// per plane ref: cluster stamp, index into ClusterPlanes
	uint32_t Stamp = 0;
	TArray<uint32_t> ClusterTris;
	TArray<uint8_t> Visited;					// per leaf, set by the walk

	// The cluster being filled.
	bool open = false;
	LevelMeshCluster cur;
	unsigned curTris = 0;
	uint32_t lastSurface = ~0u;

	static bool IsLeaf(void *child) { return ((uintptr_t)child & 1) != 0; }
	static subsector_t *Leaf(void *child) { return (subsector_t *)((uint8_t *)child - 1); }
	static node_t *Node(void *child) { return (node_t *)child; }

	uint32_t Count(void *child)
	{
		if (IsLeaf(child)) return LeafTris[Leaf(child)->Index()];
		node_t *node = Node(child);
		uint32_t n = Count(node->children[0]) + Count(node->children[1]);
		NodeTris[node->Index()] = n;
		return n;
	}

	void Collect(void *child)
	{
		if (IsLeaf(child))
		{
			int leaf = Leaf(child)->Index();
			Visited[leaf] = 1;
			GatherLeaves.Push(leaf);
			for (uint32_t k = LeafStart[leaf]; k < LeafStart[leaf + 1]; k++)
				Gather.Push(LeafSurfaces[k]);
			return;
		}
		Collect(Node(child)->children[0]);
		Collect(Node(child)->children[1]);
	}

	void Emit(void *child)
	{
		if (IsLeaf(child))
		{
			int leaf = Leaf(child)->Index();
			Visited[leaf] = 1;
			Gather.Clear();
			GatherLeaves.Clear();
			GatherLeaves.Push(leaf);
			for (uint32_t k = LeafStart[leaf]; k < LeafStart[leaf + 1]; k++)
				Gather.Push(LeafSurfaces[k]);
			EmitGroup(LeafTris[leaf] > Budget);
			return;
		}
		node_t *node = Node(child);
		if (NodeTris[node->Index()] <= Budget)
		{
			Gather.Clear();
			GatherLeaves.Clear();
			Collect(child);
			EmitGroup(false);
			return;
		}
		Emit(node->children[0]);
		Emit(node->children[1]);
	}

	void Open()
	{
		cur = {};
		cur.bmin[0] = cur.bmin[1] = cur.bmin[2] = 1e30f;
		cur.bmax[0] = cur.bmax[1] = cur.bmax[2] = -1e30f;
		cur.firstIndex = NewIndices.Size();
		cur.firstSector = Bake->ClusterSectors.Size();
		cur.firstSurface = Bake->ClusterSurfaces.Size();
		curTris = 0;
		lastSurface = ~0u;
		Stamp++;
		open = true;
	}

	void Close()
	{
		if (!open) return;
		open = false;
		if (curTris == 0) return;
		cur.numIndices = NewIndices.Size() - cur.firstIndex;
		cur.numSectors = Bake->ClusterSectors.Size() - cur.firstSector;
		cur.numSurfaces = Bake->ClusterSurfaces.Size() - cur.firstSurface;
		Bake->ClusterPlaneStart.Push(Bake->ClusterPlanes.Size());
		for (uint32_t b = 0; b < LMB_COUNT; b++)
			if (cur.bucketIndices[b]) cur.bucketMask |= 1u << b;
		uint32_t drawn = cur.bucketMask & ((1u << LMB_DRAWN) - 1);
		if ((drawn & (drawn - 1)) != 0) Bake->ClusterStats.mixedBucketClusters++;
		Bake->Clusters.Push(cur);
		Bake->ClusterLeafStart.Push(Bake->ClusterLeaves.Size());
		for (uint32_t leaf : GatherLeaves) Bake->ClusterLeaves.Push(leaf);
		ClusterTris.Push(curTris);
	}

	// Records that the open cluster has vertices at LM_PlaneZ(ref) + offset.
	void AddPlane(uint32_t ref, float offset)
	{
		uint32_t sec = ref >> 1;
		if (SecStamp[sec] != Stamp)
		{
			SecStamp[sec] = Stamp;
			Bake->ClusterSectors.Push(sec);
		}
		if (PlaneStamp[ref] != Stamp)
		{
			PlaneStamp[ref] = Stamp;
			PlaneSlot[ref] = Bake->ClusterPlanes.Size();
			Bake->ClusterPlanes.Push({ ref, offset, offset });
		}
		else
		{
			auto &cp = Bake->ClusterPlanes[PlaneSlot[ref]];
			cp.minOffset = std::min(cp.minOffset, offset);
			cp.maxOffset = std::max(cp.maxOffset, offset);
		}
	}

	void AddTriangles(uint32_t surface, uint32_t src, uint32_t count, uint32_t bucket)
	{
		auto &verts = Bake->Vertices;
		auto &oldIndices = Bake->Indices;
		for (uint32_t i = src; i < src + count * 3; i++)
		{
			uint32_t vi = oldIndices[i];
			NewIndices.Push(vi);
			const LevelMeshVertex &v = verts[vi];
			float z = Z[vi];
			cur.bmin[0] = std::min(cur.bmin[0], v.x); cur.bmax[0] = std::max(cur.bmax[0], v.x);
			cur.bmin[1] = std::min(cur.bmin[1], v.y); cur.bmax[1] = std::max(cur.bmax[1], v.y);
			cur.bmin[2] = std::min(cur.bmin[2], z); cur.bmax[2] = std::max(cur.bmax[2], z);
			AddPlane(v.anchor & LMV_REFMASK, v.zOffset);
		}
		// A wall's edge bound can become its vertices' z (LevelMeshSurface::edgeTop).
		uint32_t edge = Bake->Surfaces[surface].edgeTop;
		if (edge != LM_NOPLANE) AddPlane(edge, 0.f);
		uint32_t clamp = Bake->Surfaces[surface].edgeClamp;
		if (clamp != LM_NOPLANE) AddPlane(clamp, 0.f);
		cur.bucketIndices[bucket] += count * 3;
		if (lastSurface != surface)
		{
			Bake->ClusterSurfaces.Push(surface);
			lastSurface = surface;
		}
		curTris += count;
	}

	// Emits the surfaces in Gather as one cluster, or as budget-sized chunks.
	void EmitGroup(bool chunked)
	{
		if (Gather.Size() == 0) return;
		auto &surfaces = Bake->Surfaces;
		std::stable_sort(Gather.begin(), Gather.end(), [&](uint32_t a, uint32_t b) { return SurfaceBucket(surfaces[a]) < SurfaceBucket(surfaces[b]); });

		auto &cs = Bake->ClusterStats;
		unsigned before = Bake->Clusters.Size();
		Open();
		for (uint32_t s : Gather)
		{
			auto &surf = surfaces[s];
			uint32_t ntri = surf.numIndices / 3;
			uint32_t src = OldFirstIndex[s];
			uint32_t bucket = SurfaceBucket(surf);
			surf.firstIndex = NewIndices.Size();
			bool split = false;
			uint32_t done = 0;
			while (done < ntri)
			{
				if (!open) { Open(); split = done > 0; }
				uint32_t take = std::min(ntri - done, Budget - curTris);
				AddTriangles(s, src + done * 3, take, bucket);
				done += take;
				if (curTris >= Budget) Close();
			}
			if (split) cs.splitSurfaces++;
		}
		Close();
		if (chunked)
		{
			cs.chunkedLeaves++;
			cs.chunkClusters += Bake->Clusters.Size() - before;
		}
	}

	void Build()
	{
		auto &cs = Bake->ClusterStats;
		auto &surfaces = Bake->Surfaces;
		unsigned nleaf = Level->subsectors.Size();
		unsigned nsurf = surfaces.Size();

		// Leaves the BSP reaches. Section hole fills (SSECF_HOLE) are
		// render-only subsectors outside the tree.
		TArray<uint8_t> reachable(nleaf, true);
		memset(reachable.Data(), 0, nleaf);
		if (Level->nodes.Size() == 0)
		{
			memset(reachable.Data(), 1, nleaf);
		}
		else
		{
			TArray<void *> stack;
			stack.Push(Level->HeadNode());
			while (stack.Size())
			{
				void *child = stack.Last();
				stack.Pop();
				if (IsLeaf(child)) reachable[Leaf(child)->Index()] = 1;
				else
				{
					stack.Push(Node(child)->children[0]);
					stack.Push(Node(child)->children[1]);
				}
			}
		}

		// Polyobject walls: their own clusters, after the BSP walk.
		unsigned npoly = Bake->PolyBaked.Size();
		TArray<uint32_t> polyStart(npoly + 1, true), polySurfaces;
		for (auto &v : polyStart) v = 0;
		for (unsigned s = 0; s < nsurf; s++)
		{
			int leaf = Bake->SurfaceLeaf[s];
			if (leaf <= -2 && unsigned(-2 - leaf) < npoly) polyStart[-2 - leaf + 1]++;
		}
		for (unsigned i = 0; i < npoly; i++) polyStart[i + 1] += polyStart[i];
		polySurfaces.Resize(npoly ? polyStart[npoly] : 0);
		{
			TArray<uint32_t> fill(npoly, true);
			for (unsigned i = 0; i < npoly; i++) fill[i] = polyStart[i];
			for (unsigned s = 0; s < nsurf; s++)
			{
				int leaf = Bake->SurfaceLeaf[s];
				if (leaf <= -2 && unsigned(-2 - leaf) < npoly) polySurfaces[fill[-2 - leaf]++] = s;
			}
		}
		auto isPoly = [&](unsigned s) { int leaf = Bake->SurfaceLeaf[s]; return leaf <= -2 && unsigned(-2 - leaf) < npoly; };
		auto isSky = [&](unsigned s) { return (surfaces[s].flags & LMSF_SKY) != 0; };

		// Surfaces without a reachable leaf: the BSP leaf at their centre.
		for (unsigned s = 0; s < nsurf; s++)
		{
			if (isPoly(s)) continue;
			int leaf = Bake->SurfaceLeaf[s];
			if (leaf >= 0 && leaf < (int)nleaf && reachable[leaf]) continue;
			if (leaf >= 0 && leaf < (int)nleaf) cs.orphanSurfaces++;
			else cs.fallbackLeaves++;
			auto &surf = surfaces[s];
			double x = 0, y = 0;
			for (uint32_t i = 0; i < surf.numVertices; i++)
			{
				x += Bake->Vertices[surf.firstVertex + i].x;
				y += Bake->Vertices[surf.firstVertex + i].y;
			}
			if (surf.numVertices) { x /= surf.numVertices; y /= surf.numVertices; }
			Bake->SurfaceLeaf[s] = Level->PointInRenderSubsector(DVector2(x, y))->Index();
		}

		// Leaf -> surfaces (CSR) and triangle counts.
		LeafStart.Resize(nleaf + 1);
		LeafTris.Resize(nleaf);
		for (auto &v : LeafStart) v = 0;
		for (auto &v : LeafTris) v = 0;
		for (unsigned s = 0; s < nsurf; s++)
		{
			if (isPoly(s) || isSky(s)) continue;
			int leaf = Bake->SurfaceLeaf[s];
			LeafStart[leaf + 1]++;
			LeafTris[leaf] += surfaces[s].numIndices / 3;
		}
		for (unsigned i = 0; i < nleaf; i++) LeafStart[i + 1] += LeafStart[i];
		LeafSurfaces.Resize(LeafStart[nleaf]);
		{
			TArray<uint32_t> fill(nleaf, true);
			for (unsigned i = 0; i < nleaf; i++) fill[i] = LeafStart[i];
			for (unsigned s = 0; s < nsurf; s++)
				if (!isPoly(s) && !isSky(s)) LeafSurfaces[fill[Bake->SurfaceLeaf[s]]++] = s;
		}
		// Sky surfaces per leaf (CSR), in surface order.
		TArray<uint32_t> skyStart(nleaf + 1, true), skySurfaces;
		for (auto &v : skyStart) v = 0;
		for (unsigned s = 0; s < nsurf; s++)
			if (isSky(s)) skyStart[Bake->SurfaceLeaf[s] + 1]++;
		for (unsigned i = 0; i < nleaf; i++) skyStart[i + 1] += skyStart[i];
		skySurfaces.Resize(skyStart[nleaf]);
		{
			TArray<uint32_t> fill(nleaf, true);
			for (unsigned i = 0; i < nleaf; i++) fill[i] = skyStart[i];
			for (unsigned s = 0; s < nsurf; s++)
				if (isSky(s)) skySurfaces[fill[Bake->SurfaceLeaf[s]]++] = s;
		}

		// Vertex z at the load heights.
		Z.Resize(Bake->Vertices.Size());
		for (unsigned i = 0; i < Z.Size(); i++) Z[i] = Bake->VertexZ(Level, Bake->Vertices[i]);

		OldFirstIndex.Resize(nsurf);
		for (unsigned s = 0; s < nsurf; s++) OldFirstIndex[s] = surfaces[s].firstIndex;
		NewIndices.Grow(Bake->Indices.Size());
		SecStamp.Resize(Level->sectors.Size());
		for (auto &v : SecStamp) v = 0;
		PlaneStamp.Resize(Level->sectors.Size() * 2);
		for (auto &v : PlaneStamp) v = 0;
		PlaneSlot.Resize(Level->sectors.Size() * 2);
		Bake->ClusterPlaneStart.Push(0);
		Visited.Resize(nleaf);
		memset(Visited.Data(), 0, nleaf);

		if (Level->nodes.Size() == 0)
		{
			for (unsigned i = 0; i < nleaf; i++)
			{
				Gather.Clear();
				GatherLeaves.Clear();
				GatherLeaves.Push(i);
				for (uint32_t k = LeafStart[i]; k < LeafStart[i + 1]; k++) Gather.Push(LeafSurfaces[k]);
				EmitGroup(LeafTris[i] > Budget);
			}
		}
		else
		{
						NodeTris.Resize(Level->nodes.Size());
			Count(Level->HeadNode());
			Emit(Level->HeadNode());
			// Safety net: every leaf with surfaces is reachable after the remap above.
			for (unsigned i = 0; i < nleaf; i++)
			{
				if (Visited[i] || LeafStart[i] == LeafStart[i + 1]) continue;
				Gather.Clear();
				GatherLeaves.Clear();
				GatherLeaves.Push(i);
				for (uint32_t k = LeafStart[i]; k < LeafStart[i + 1]; k++) Gather.Push(LeafSurfaces[k]);
				EmitGroup(LeafTris[i] > Budget);
			}
		}
		// Sky: leaves in BSP order (the tree's leaf order, then any leaf
		// outside it), cut into budget-sized clusters.
		{
			TArray<uint32_t> order;
			TArray<uint8_t> seen(nleaf, true);
			memset(seen.Data(), 0, nleaf);
			if (Level->nodes.Size() > 0)
			{
				TArray<void *> stack;
				stack.Push(Level->HeadNode());
				while (stack.Size())
				{
					void *child = stack.Last();
					stack.Pop();
					if (IsLeaf(child))
					{
						int leaf = Leaf(child)->Index();
						if (!seen[leaf]) { seen[leaf] = 1; order.Push(leaf); }
					}
					else
					{
						stack.Push(Node(child)->children[1]);
						stack.Push(Node(child)->children[0]);
					}
				}
			}
			for (unsigned i = 0; i < nleaf; i++)
				if (!seen[i]) order.Push(i);
			Gather.Clear();
			GatherLeaves.Clear();
			uint32_t tris = 0;
			for (uint32_t leaf : order)
			{
				for (uint32_t k = skyStart[leaf]; k < skyStart[leaf + 1]; k++)
				{
					uint32_t s = skySurfaces[k];
					uint32_t n = surfaces[s].numIndices / 3;
					if (Gather.Size() && tris + n > Budget)
					{
						EmitGroup(false);
						Gather.Clear();
						tris = 0;
					}
					Gather.Push(s);
					tris += n;
				}
			}
			EmitGroup(tris > Budget);
		}

		Bake->PolyClusterStart.Resize(npoly + 1);
		for (unsigned i = 0; i < npoly; i++)
		{
			Bake->PolyClusterStart[i] = Bake->Clusters.Size();
			Gather.Clear();
			uint32_t tris = 0;
			for (uint32_t k = polyStart[i]; k < polyStart[i + 1]; k++)
			{
				Gather.Push(polySurfaces[k]);
				tris += surfaces[polySurfaces[k]].numIndices / 3;
			}
			EmitGroup(tris > Budget);
		}
		Bake->PolyClusterStart[npoly] = Bake->Clusters.Size();
		if (NewIndices.Size() != Bake->Indices.Size())
			Printf(TEXTCOLOR_RED "LevelMeshClusters: index count changed (%u -> %u)\n", Bake->Indices.Size(), NewIndices.Size());
		assert(NewIndices.Size() == Bake->Indices.Size());
		Bake->Indices = std::move(NewIndices);

		// Sector -> clusters (CSR).
		unsigned nsec = Level->sectors.Size();
		auto &start = Bake->SectorClusterStart;
		start.Resize(nsec + 1);
		for (auto &v : start) v = 0;
		for (auto sec : Bake->ClusterSectors) start[sec + 1]++;
		for (unsigned i = 0; i < nsec; i++)
		{
			cs.maxSectorClusters = std::max(cs.maxSectorClusters, start[i + 1]);
			start[i + 1] += start[i];
		}
		Bake->SectorClusters.Resize(Bake->ClusterSectors.Size());
		{
			TArray<uint32_t> fill(nsec, true);
			for (unsigned i = 0; i < nsec; i++) fill[i] = start[i];
			for (unsigned c = 0; c < Bake->Clusters.Size(); c++)
			{
				auto &cl = Bake->Clusters[c];
				for (uint32_t k = cl.firstSector; k < cl.firstSector + cl.numSectors; k++)
					Bake->SectorClusters[fill[Bake->ClusterSectors[k]]++] = c;
			}
		}

		// Cluster -> leaves is complete; subsector -> clusters (CSR).
		unsigned ncl = Bake->Clusters.Size();
		Bake->ClusterLeafStart.Push(Bake->ClusterLeaves.Size());
		auto &lstart = Bake->LeafClusterStart;
		lstart.Resize(nleaf + 1);
		for (auto &v : lstart) v = 0;
		for (auto leaf : Bake->ClusterLeaves) lstart[leaf + 1]++;
		for (unsigned i = 0; i < nleaf; i++) lstart[i + 1] += lstart[i];
		Bake->LeafClusters.Resize(Bake->ClusterLeaves.Size());
		{
			TArray<uint32_t> fill(nleaf, true);
			for (unsigned i = 0; i < nleaf; i++) fill[i] = lstart[i];
			for (unsigned c = 0; c < ncl; c++)
				for (uint32_t k = Bake->ClusterLeafStart[c]; k < Bake->ClusterLeafStart[c + 1]; k++)
					Bake->LeafClusters[fill[Bake->ClusterLeaves[k]]++] = c;
		}

		// Cluster adjacency: the other clusters of its leaves (chunk
		// siblings) and the clusters of the leaves across its leaves' segs.
		Bake->ClusterAdjStart.Resize(ncl + 1);
		Bake->ClusterAdj.Clear();
		{
			TArray<uint32_t> adj;
			auto addLeaf = [&](unsigned leaf, unsigned c)
			{
				for (uint32_t k = lstart[leaf]; k < lstart[leaf + 1]; k++)
					if (Bake->LeafClusters[k] != c) adj.Push(Bake->LeafClusters[k]);
			};
			for (unsigned c = 0; c < ncl; c++)
			{
				Bake->ClusterAdjStart[c] = Bake->ClusterAdj.Size();
				adj.Clear();
				for (uint32_t k = Bake->ClusterLeafStart[c]; k < Bake->ClusterLeafStart[c + 1]; k++)
				{
					unsigned leaf = Bake->ClusterLeaves[k];
					addLeaf(leaf, c);
					subsector_t &sub = Level->subsectors[leaf];
					for (uint32_t i = 0; i < sub.numlines; i++)
					{
						seg_t *partner = sub.firstline[i].PartnerSeg;
						if (partner && partner->Subsector) addLeaf(partner->Subsector->Index(), c);
					}
				}
				std::sort(adj.begin(), adj.end());
				auto end = std::unique(adj.begin(), adj.end());
				unsigned n = unsigned(end - adj.begin());
				for (unsigned i = 0; i < n; i++) Bake->ClusterAdj.Push(adj[i]);
				cs.maxAdjacency = std::max(cs.maxAdjacency, n);
			}
			Bake->ClusterAdjStart[ncl] = Bake->ClusterAdj.Size();
			cs.adjacencyEdges = Bake->ClusterAdj.Size();
		}

		// Stats.
		cs.clusters = Bake->Clusters.Size();
		cs.triangles = Bake->Indices.Size() / 3;
		cs.clusterSectorRefs = Bake->ClusterSectors.Size();
		if (ClusterTris.Size())
		{
			for (auto t : ClusterTris) cs.histogram[std::min(t > 0 ? (t - 1) / 32 : 0u, 8u)]++;
			std::sort(ClusterTris.begin(), ClusterTris.end());
			cs.minTris = ClusterTris[0];
			cs.maxTris = ClusterTris.Last();
			cs.p50Tris = ClusterTris[ClusterTris.Size() / 2];
			cs.p90Tris = ClusterTris[std::min(ClusterTris.Size() - 1, ClusterTris.Size() * 9 / 10)];
		}
	}
};

} // namespace

//==========================================================================
//
// LevelMeshClusters_Build
//
//==========================================================================

void LevelMeshClusters_Build(FLevelLocals *Level, LevelMeshBake *bake, int budget)
{
	double t0 = I_msTimeF();
	bake->Clusters.Clear();
	bake->ClusterSectors.Clear();
	bake->ClusterSurfaces.Clear();
	bake->ClusterPlanes.Clear();
	bake->ClusterPlaneStart.Clear();
	bake->ClusterStats = {};
	bake->PolyClusterStart.Clear();
	bake->ClusterLeafStart.Clear();
	bake->ClusterLeaves.Clear();
	bake->LeafClusterStart.Clear();
	bake->LeafClusters.Clear();
	bake->ClusterAdjStart.Clear();
	bake->ClusterAdj.Clear();
	bake->ClusterStats.budget = std::clamp(budget, 16, 65536);
	if (bake->Surfaces.Size() > 0 && Level->subsectors.Size() > 0)
	{
		ClusterBuilder b;
		b.Level = Level;
		b.Bake = bake;
		b.Budget = bake->ClusterStats.budget;
		b.Build();
	}
	if (bake->PolyClusterStart.Size() != bake->PolyBaked.Size() + 1)
	{
		bake->PolyClusterStart.Resize(bake->PolyBaked.Size() + 1);
		for (auto &v : bake->PolyClusterStart) v = bake->Clusters.Size();
	}
	if (bake->ClusterLeafStart.Size() != bake->Clusters.Size() + 1)
	{
		// No BSP walk ran: no cluster has leaves.
		bake->ClusterLeafStart.Resize(bake->Clusters.Size() + 1);
		for (auto &v : bake->ClusterLeafStart) v = 0;
		bake->ClusterLeaves.Clear();
		bake->ClusterAdjStart = bake->ClusterLeafStart;
		bake->ClusterAdj.Clear();
		bake->LeafClusterStart.Resize(Level->subsectors.Size() + 1);
		for (auto &v : bake->LeafClusterStart) v = 0;
		bake->LeafClusters.Clear();
	}
	bake->GatherClusterStamp.Resize(bake->Clusters.Size());
	bake->GatherLeafStamp.Resize(Level->subsectors.Size());
	bake->GatherSectorStamp.Resize(Level->sectors.Size());
	for (auto &v : bake->GatherClusterStamp) v = 0;
	for (auto &v : bake->GatherLeafStamp) v = 0;
	for (auto &v : bake->GatherSectorStamp) v = 0;
	bake->GatherStamp = 0;
	bake->SurfaceLeaf.Reset();
	bake->Clusters.ShrinkToFit();
	bake->ClusterSectors.ShrinkToFit();
	bake->ClusterSurfaces.ShrinkToFit();
	bake->ClusterPlanes.ShrinkToFit();
	bake->ClusterPlaneStart.ShrinkToFit();
	bake->ClusterLeaves.ShrinkToFit();
	bake->LeafClusters.ShrinkToFit();
	bake->ClusterAdj.ShrinkToFit();
	bake->ClusterStats.ms = I_msTimeF() - t0;
}

void LevelMeshBake::PrintClusterStats(const char *mapname) const
{
	auto &c = ClusterStats;
	Printf("LevelMeshClusters %s: %u clusters (budget %u), %u triangles, %.1f ms; tris/cluster min %u p50 %u p90 %u max %u; histogram",
		mapname, c.clusters, c.budget, c.triangles, c.ms, c.minTris, c.p50Tris, c.p90Tris, c.maxTris);
	for (int i = 0; i < 9; i++) Printf(" %u", c.histogram[i]);
	Printf("; chunked leaves %u (%u clusters), split surfaces %u, mixed-bucket clusters %u, cluster-sector refs %u, max sector fan-out %u, fallback leaves %u, hole surfaces %u, adjacency %u (max %u)\n",
		c.chunkedLeaves, c.chunkClusters, c.splitSurfaces, c.mixedBucketClusters, c.clusterSectorRefs, c.maxSectorClusters, c.fallbackLeaves, c.orphanSurfaces, c.adjacencyEdges, c.maxAdjacency);
}
