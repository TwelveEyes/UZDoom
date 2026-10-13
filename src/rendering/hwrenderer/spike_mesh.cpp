/*
** spike_mesh.cpp
**
** Ticket 05 spike (throwaway, never merged): cut the classic DoomLevelMesh
** into BSP-subtree clusters with a triangle budget, drive the Vulkan GPU cull
** prototype, and refresh the z-range of clusters touched by moving planes.
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

#include <chrono>
#include <algorithm>
#include "spike_mesh.h"
#include "doom_levelmesh.h"
#include "g_levellocals.h"
#include "d_player.h"
#include "v_video.h"
#include "c_cvars.h"
#include "printf.h"
#include "hw_spike.h"
#include "r_data/r_interpolate.h"
#include "a_plats.h"

EXTERN_CVAR(Int, r_spike_debug)

CVAR(Int, r_spike, 0, 0)			// SPIKE_* mode; 0 = classic
CVAR(Int, r_spike_budget, 128, 0)	// cluster triangle budget
CVAR(Int, r_spike_movers, 0, 0)		// start perpetual lifts on the N sectors nearest the camera at maptime 35

namespace
{
	struct SpikeState
	{
		FLevelLocals *Level = nullptr;
		hwrenderer::LevelMesh *Mesh = nullptr;
		int Budget = 0;
		bool Built = false;
		bool MoversStarted = false;
		FLevelLocals *MoversLevel = nullptr;
		hwrenderer::LevelMesh *MoversMesh = nullptr;

		SpikeMeshData Data;
		TArray<uint32_t> ClSecStart, ClSecList;		// cluster -> referenced sectors
		TArray<uint32_t> SecClStart, SecClList;		// sector -> clusters
		TArray<uint32_t> DirtyStamp;
		uint32_t Stamp = 0;
		TArray<int> Moving;
		TArray<uint32_t> Dirty;
		TArray<SpikeClusterUpdate> Updates;
		sector_t *MoversSample = nullptr;
	};

	SpikeState S;

	struct Builder
	{
		FLevelLocals *Level;
		DoomLevelMesh *Mesh;
		int Budget;
		TArray<int> SurfLeaf;
		TArray<uint32_t> LeafStart, LeafTris;
		TArray<int> NodeCount;
		TArray<int> Gather;
		TArray<uint32_t> SecStamp;
		uint32_t SecStampValue = 0;

		static bool IsLeaf(void *child) { return ((uintptr_t)child & 1) != 0; }
		subsector_t *Leaf(void *child) { return (subsector_t *)((uint8_t *)child - 1); }
		node_t *Node(void *child) { return (node_t *)child; }
		int LeafCount(int leaf) { return (int)(LeafStart[leaf + 1] - LeafStart[leaf]); }

		int Count(void *child)
		{
			if (IsLeaf(child))
				return LeafCount(Leaf(child)->Index());
			node_t *node = Node(child);
			int n = Count(node->children[0]) + Count(node->children[1]);
			NodeCount[node - &Level->nodes[0]] = n;
			return n;
		}

		void Collect(void *child)
		{
			if (IsLeaf(child))
			{
				int leaf = Leaf(child)->Index();
				for (uint32_t t = LeafStart[leaf]; t < LeafStart[leaf + 1]; t++)
					Gather.Push(LeafTris[t]);
				return;
			}
			Collect(Node(child)->children[0]);
			Collect(Node(child)->children[1]);
		}

		void AddSector(sector_t *sec)
		{
			if (!sec)
				return;
			int i = sec->Index();
			if (SecStamp[i] == SecStampValue)
				return;
			SecStamp[i] = SecStampValue;
			S.ClSecList.Push(i);
		}

		void EmitCluster(const int *tris, int count)
		{
			if (count <= 0)
				return;
			SpikeCluster c;
			c.bmin[0] = c.bmin[1] = c.bmin[2] = 1e30f;
			c.bmax[0] = c.bmax[1] = c.bmax[2] = -1e30f;
			c.firstIndex = S.Data.Indices.Size();
			c.indexCount = count * 3;
			SecStampValue++;
			S.ClSecStart.Push(S.ClSecList.Size());
			for (int k = 0; k < count; k++)
			{
				int t = tris[k];
				for (int j = 0; j < 3; j++)
				{
					uint32_t vi = Mesh->MeshElements[t * 3 + j];
					S.Data.Indices.Push(vi);
					const SpikeVertex &v = S.Data.Vertices[vi];
					c.bmin[0] = std::min(c.bmin[0], v.x); c.bmax[0] = std::max(c.bmax[0], v.x);
					c.bmin[1] = std::min(c.bmin[1], v.y); c.bmax[1] = std::max(c.bmax[1], v.y);
					c.bmin[2] = std::min(c.bmin[2], v.z); c.bmax[2] = std::max(c.bmax[2], v.z);
				}
				const Surface &surf = Mesh->Surfaces[Mesh->MeshSurfaces[t]];
				if (surf.type == ST_FLOOR || surf.type == ST_CEILING)
				{
					AddSector(Level->subsectors[surf.typeIndex].sector);
				}
				else
				{
					side_t *side = &Level->sides[surf.typeIndex];
					AddSector(side->linedef->frontsector);
					AddSector(side->linedef->backsector);
				}
				AddSector(surf.controlSector);
			}
			S.Data.Clusters.Push(c);
		}

		void EmitChunks(const int *tris, int count)
		{
			for (int k = 0; k < count; k += Budget)
				EmitCluster(tris + k, std::min(Budget, count - k));
		}

		void Emit(void *child)
		{
			if (IsLeaf(child))
			{
				int leaf = Leaf(child)->Index();
				EmitChunks((const int *)&LeafTris[LeafStart[leaf]], LeafCount(leaf));
				return;
			}
			node_t *node = Node(child);
			if (NodeCount[node - &Level->nodes[0]] <= Budget)
			{
				Gather.Clear();
				Collect(child);
				EmitCluster(Gather.Data(), Gather.Size());
				return;
			}
			Emit(node->children[0]);
			Emit(node->children[1]);
		}

		void Build()
		{
			auto &d = S.Data;
			d.Vertices.Clear();
			d.Indices.Clear();
			d.Clusters.Clear();
			d.Budget = Budget;
			S.ClSecStart.Clear();
			S.ClSecList.Clear();

			// Vertices in GL world coordinates, tagged with their surface.
			d.Vertices.Resize(Mesh->MeshVertices.Size());
			for (unsigned s = 0; s < Mesh->Surfaces.Size(); s++)
			{
				const Surface &surf = Mesh->Surfaces[s];
				for (int j = 0; j < surf.numVerts; j++)
				{
					const FVector3 &p = Mesh->MeshVertices[surf.startVertIndex + j];
					d.Vertices[surf.startVertIndex + j] = { p.X, p.Z, p.Y, s };
				}
			}

			// Surface -> BSP leaf. Flats know their subsector; walls take the
			// subsector of their side's first seg.
			TArray<int> sideLeaf(Level->sides.Size(), true);
			for (auto &v : sideLeaf) v = -1;
			for (auto &seg : Level->segs)
			{
				if (seg.sidedef && seg.Subsector && sideLeaf[seg.sidedef->Index()] < 0)
					sideLeaf[seg.sidedef->Index()] = seg.Subsector->Index();
			}
			SurfLeaf.Resize(Mesh->Surfaces.Size());
			for (unsigned s = 0; s < Mesh->Surfaces.Size(); s++)
			{
				const Surface &surf = Mesh->Surfaces[s];
				int leaf;
				if (surf.type == ST_FLOOR || surf.type == ST_CEILING)
				{
					leaf = surf.typeIndex;
				}
				else
				{
					leaf = sideLeaf[surf.typeIndex];
					if (leaf < 0)
					{
						side_t *side = &Level->sides[surf.typeIndex];
						DVector2 mid = (side->V1()->fPos() + side->V2()->fPos()) * 0.5;
						leaf = Level->PointInRenderSubsector(mid)->Index();
					}
				}
				SurfLeaf[s] = leaf;
			}

			// Leaf -> triangles (CSR).
			unsigned nleaf = Level->subsectors.Size();
			unsigned ntri = Mesh->MeshSurfaces.Size();
			LeafStart.Resize(nleaf + 1);
			for (auto &v : LeafStart) v = 0;
			for (unsigned t = 0; t < ntri; t++)
				LeafStart[SurfLeaf[Mesh->MeshSurfaces[t]] + 1]++;
			for (unsigned i = 0; i < nleaf; i++)
				LeafStart[i + 1] += LeafStart[i];
			LeafTris.Resize(ntri);
			TArray<uint32_t> fill(nleaf, true);
			for (unsigned i = 0; i < nleaf; i++) fill[i] = LeafStart[i];
			for (unsigned t = 0; t < ntri; t++)
				LeafTris[fill[SurfLeaf[Mesh->MeshSurfaces[t]]]++] = t;

			SecStamp.Resize(Level->sectors.Size());
			for (auto &v : SecStamp) v = 0;

			// BSP subtrees with <= Budget triangles become clusters.
			NodeCount.Resize(Level->nodes.Size());
			if (Level->nodes.Size() == 0)
			{
				for (unsigned i = 0; i < nleaf; i++)
					EmitChunks((const int *)&LeafTris[LeafStart[i]], LeafCount(i));
			}
			else
			{
				Count(Level->HeadNode());
				Emit(Level->HeadNode());
			}
			S.ClSecStart.Push(S.ClSecList.Size());

			// Sector -> clusters back-links.
			unsigned nsec = Level->sectors.Size();
			S.SecClStart.Resize(nsec + 1);
			for (auto &v : S.SecClStart) v = 0;
			for (unsigned i = 0; i < S.ClSecList.Size(); i++)
				S.SecClStart[S.ClSecList[i] + 1]++;
			for (unsigned i = 0; i < nsec; i++)
				S.SecClStart[i + 1] += S.SecClStart[i];
			S.SecClList.Resize(S.ClSecList.Size());
			TArray<uint32_t> sfill(nsec, true);
			for (unsigned i = 0; i < nsec; i++) sfill[i] = S.SecClStart[i];
			for (unsigned c = 0; c < d.Clusters.Size(); c++)
				for (uint32_t k = S.ClSecStart[c]; k < S.ClSecStart[c + 1]; k++)
					S.SecClList[sfill[S.ClSecList[k]]++] = c;

			S.DirtyStamp.Resize(d.Clusters.Size());
			for (auto &v : S.DirtyStamp) v = 0;
			S.Stamp = 0;
		}
	};

	double NowMS()
	{
		using namespace std::chrono;
		return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count() / 1e6;
	}

	void StartMovers(FLevelLocals *Level)
	{
		int n = r_spike_movers;
		AActor *cam = players[consoleplayer].camera;
		if (n <= 0 || cam == nullptr)
			return;
		DVector2 pos = cam->Pos().XY();
		TArray<std::pair<double, int>> order;
		for (auto &sec : Level->sectors)
		{
			if (sec.PlaneMoving(sector_t::floor))
				continue;
			order.Push({ (sec.centerspot - pos).LengthSquared(), sec.Index() });
		}
		std::sort(order.begin(), order.end());
		int started = 0;
		for (unsigned i = 0; i < order.Size() && started < n; i++)
		{
			sector_t *sec = &Level->sectors[order[i].second];
			DPlat *plat = Level->CreateThinker<DPlat>(sec);
			plat->m_Type = DPlat::platPerpetualRaise;
			plat->m_Crush = -1;
			plat->m_Tag = 0;
			plat->m_Speed = 4;
			plat->m_Wait = 1;	// 0 would park the plat in 'waiting' forever
			plat->m_Count = 0;
			double z = sec->CenterFloor();
			plat->m_High = sec->floorplane.PointToDist(sec->centerspot, z);
			plat->m_Low = sec->floorplane.PointToDist(sec->centerspot, z - 48);
			plat->m_Status = DPlat::down;
			if (!S.MoversSample) S.MoversSample = sec;
			started++;
		}
		Printf("Spike05: started %d perpetual lifts near (%.0f, %.0f)\n", started, pos.X, pos.Y);
	}

	void UpdateMoving(FLevelLocals *Level)
	{
		double t0 = NowMS();
		R_SpikeCollectMovingPlanes(Level, S.Moving);
		S.Stamp++;
		S.Dirty.Clear();
		for (int p : S.Moving)
		{
			int sec = p >> 1;
			for (uint32_t k = S.SecClStart[sec]; k < S.SecClStart[sec + 1]; k++)
			{
				uint32_t c = S.SecClList[k];
				if (S.DirtyStamp[c] != S.Stamp)
				{
					S.DirtyStamp[c] = S.Stamp;
					S.Dirty.Push(c);
				}
			}
		}
		S.Updates.Resize(S.Dirty.Size());
		for (unsigned i = 0; i < S.Dirty.Size(); i++)
		{
			uint32_t c = S.Dirty[i];
			SpikeCluster cl = S.Data.Clusters[c];	// load-time bounds (the geometry stays at load heights)
			for (uint32_t k = S.ClSecStart[c]; k < S.ClSecStart[c + 1]; k++)
			{
				sector_t *sec = &Level->sectors[S.ClSecList[k]];
				float f = (float)sec->CenterFloor();
				float ce = (float)sec->CenterCeiling();
				cl.bmin[1] = std::min(cl.bmin[1], std::min(f, ce));
				cl.bmax[1] = std::max(cl.bmax[1], std::max(f, ce));
			}
			S.Updates[i].index = c;
			S.Updates[i].cluster = cl;
		}
		if (S.Updates.Size() > 0)
			screen->SpikeUpdateClusters(S.Updates.Data(), S.Updates.Size());
		spikestats.movPlanes = S.Moving.Size();
		static int dbgframe = 0;
		if ((r_spike_debug & 32) && ++dbgframe % 200 == 0 && S.MoversSample)
		{
			Printf("Spike05 movers: maptime %d moving %u sample z %.1f floordata %p\n", Level->maptime, S.Moving.Size(), S.MoversSample->CenterFloor(), (void*)S.MoversSample->floordata.Get());
		}
		spikestats.movDirty = S.Updates.Size();
		spikestats.movCPU = NowMS() - t0;
	}
}

void Spike_TickMovers(FLevelLocals *Level)
{
	if (S.MoversLevel != Level || S.MoversMesh != Level->levelMesh)
	{
		S.MoversLevel = Level;
		S.MoversMesh = Level->levelMesh;
		S.MoversStarted = false;
		S.MoversSample = nullptr;
	}
	if (!S.MoversStarted && r_spike_movers > 0 && Level->maptime >= 35)
	{
		S.MoversStarted = true;
		StartMovers(Level);
	}
}

int Spike_Frame(FLevelLocals *Level)
{
	int mode = r_spike;
	spikestats.mode = mode;
	if (mode <= SPIKE_OFF || mode > SPIKE_NOCULL || !screen->SpikeSupported() || Level->levelMesh == nullptr)
	{
		spikestats.movPlanes = spikestats.movDirty = 0;
		spikestats.movCPU = 0;
		return SPIKE_OFF;
	}

	int budget = std::clamp((int)r_spike_budget, 16, 65536);
	if (!S.Built || S.Level != Level || S.Mesh != Level->levelMesh || S.Budget != budget)
	{
		double t0 = NowMS();
		Builder b;
		b.Level = Level;
		b.Mesh = static_cast<DoomLevelMesh *>(Level->levelMesh);
		b.Budget = budget;
		b.Build();
		double t1 = NowMS();
		S.Level = Level;
		S.Mesh = Level->levelMesh;
		S.Budget = budget;
		S.Built = true;
		screen->SpikeSetMesh(&S.Data);
		Printf("Spike05: built %u clusters (budget %d) in %.1f ms, upload %.1f ms\n", S.Data.Clusters.Size(), budget, t1 - t0, NowMS() - t1);
	}

	UpdateMoving(Level);
	return mode;
}
