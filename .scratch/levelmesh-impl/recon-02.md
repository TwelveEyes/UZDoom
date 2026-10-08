# Recon 02 — FLevelMesh data layer

## 1. Existing DoomLevelMesh (verbatim h+cpp)

### doom_levelmesh.h (full file)

`src/rendering/hwrenderer/doom_levelmesh.h` (lines 1-66):

```cpp
/*
** doom_levelmesh.h
**
**
**
**---------------------------------------------------------------------------
**
** Copyright 2017-2025 GZDoom Maintainers and Contributors
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
*/

#pragma once

#include "hw_levelmesh.h"
#include "tarray.h"
#include "vectors.h"
#include "r_defs.h"

struct FLevelLocals;

struct Surface
{
	SurfaceType type;
	int typeIndex;
	int numVerts;
	unsigned int startVertIndex;
	secplane_t plane;
	sector_t *controlSector;
	bool bSky;
};

class DoomLevelMesh : public hwrenderer::LevelMesh
{
public:
	DoomLevelMesh(FLevelLocals &doomMap);

	TArray<Surface> Surfaces;

private:
	void CreateSubsectorSurfaces(FLevelLocals &doomMap);
	void CreateCeilingSurface(FLevelLocals &doomMap, subsector_t *sub, sector_t *sector, int typeIndex, bool is3DFloor);
	void CreateFloorSurface(FLevelLocals &doomMap, subsector_t *sub, sector_t *sector, int typeIndex, bool is3DFloor);
	void CreateSideSurfaces(FLevelLocals &doomMap, side_t *side);

	static bool IsTopSideSky(sector_t* frontsector, sector_t* backsector, side_t* side);
	static bool IsTopSideVisible(side_t* side);
	static bool IsBottomSideVisible(side_t* side);
	static bool IsSkySector(sector_t* sector);
	static bool IsControlSector(sector_t* sector);

	static secplane_t ToPlane(const FVector3& pt1, const FVector3& pt2, const FVector3& pt3)
	{
		FVector3 n = ((pt2 - pt1) ^ (pt3 - pt2)).Unit();
		float d = pt1 | n;
		secplane_t p;
		p.set(n.X, n.Y, n.Z, d);
		return p;
	}

	static FVector2 ToFVector2(const DVector2& v) { return FVector2((float)v.X, (float)v.Y); }
	static FVector3 ToFVector3(const DVector3& v) { return FVector3((float)v.X, (float)v.Y, (float)v.Z); }
	static FVector4 ToFVector4(const DVector4& v) { return FVector4((float)v.X, (float)v.Y, (float)v.Z, (float)v.W); }

	static bool IsDegenerate(const FVector3 &v0, const FVector3 &v1, const FVector3 &v2);
};
```

### doom_levelmesh.cpp (full file)

`src/rendering/hwrenderer/doom_levelmesh.cpp` (lines 1-320):

```cpp
/*
** doom_levelmesh.cpp
**
**
**
**---------------------------------------------------------------------------
**
** Copyright 2017-2025 GZDoom Maintainers and Contributors
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
*/

#include "templates.h"
#include "doom_levelmesh.h"
#include "g_levellocals.h"
#include "texturemanager.h"

DoomLevelMesh::DoomLevelMesh(FLevelLocals &doomMap)
{
	for (unsigned int i = 0; i < doomMap.sides.Size(); i++)
	{
		CreateSideSurfaces(doomMap, &doomMap.sides[i]);
	}

	CreateSubsectorSurfaces(doomMap);

	for (size_t i = 0; i < Surfaces.Size(); i++)
	{
		const Surface &s = Surfaces[i];
		int numVerts = s.numVerts;
		unsigned int pos = s.startVertIndex;
		FVector3* verts = &MeshVertices[pos];

		for (int j = 0; j < numVerts; j++)
		{
			MeshUVIndex.Push(j);
		}

		if (s.type == ST_FLOOR || s.type == ST_CEILING)
		{
			for (int j = 2; j < numVerts; j++)
			{
				if (!IsDegenerate(verts[0], verts[j - 1], verts[j]))
				{
					MeshElements.Push(pos);
					MeshElements.Push(pos + j - 1);
					MeshElements.Push(pos + j);
					MeshSurfaces.Push((int)i);
				}
			}
		}
		else if (s.type == ST_MIDDLEWALL || s.type == ST_UPPERWALL || s.type == ST_LOWERWALL)
		{
			if (!IsDegenerate(verts[0], verts[1], verts[2]))
			{
				MeshElements.Push(pos + 0);
				MeshElements.Push(pos + 1);
				MeshElements.Push(pos + 2);
				MeshSurfaces.Push((int)i);
			}
			if (!IsDegenerate(verts[1], verts[2], verts[3]))
			{
				MeshElements.Push(pos + 3);
				MeshElements.Push(pos + 2);
				MeshElements.Push(pos + 1);
				MeshSurfaces.Push((int)i);
			}
		}
	}
}

void DoomLevelMesh::CreateSideSurfaces(FLevelLocals &doomMap, side_t *side)
{
	sector_t *front;
	sector_t *back;

	front = side->sector;
	back = (side->linedef->frontsector == front) ? side->linedef->backsector : side->linedef->frontsector;

	if (IsControlSector(front))
		return;

	FVector2 v1 = ToFVector2(side->V1()->fPos());
	FVector2 v2 = ToFVector2(side->V2()->fPos());

	float v1Top = (float)front->ceilingplane.ZatPoint(v1);
	float v1Bottom = (float)front->floorplane.ZatPoint(v1);
	float v2Top = (float)front->ceilingplane.ZatPoint(v2);
	float v2Bottom = (float)front->floorplane.ZatPoint(v2);

	int typeIndex = side->Index();

	FVector2 dx(v2.X - v1.X, v2.Y - v1.Y);
	float distance = dx.Length();

	if (back)
	{
		for (unsigned int j = 0; j < front->e->XFloor.ffloors.Size(); j++)
		{
			F3DFloor *xfloor = front->e->XFloor.ffloors[j];

			// Don't create a line when both sectors have the same 3d floor
			bool bothSides = false;
			for (unsigned int k = 0; k < back->e->XFloor.ffloors.Size(); k++)
			{
				if (back->e->XFloor.ffloors[k] == xfloor)
				{
					bothSides = true;
					break;
				}
			}
			if (bothSides)
				continue;

			Surface surf;
			surf.type = ST_MIDDLEWALL;
			surf.typeIndex = typeIndex;
			surf.controlSector = xfloor->model;

			FVector3 verts[4];
			verts[0].X = verts[2].X = v2.X;
			verts[0].Y = verts[2].Y = v2.Y;
			verts[1].X = verts[3].X = v1.X;
			verts[1].Y = verts[3].Y = v1.Y;
			verts[0].Z = (float)xfloor->model->floorplane.ZatPoint(v2);
			verts[1].Z = (float)xfloor->model->floorplane.ZatPoint(v1);
			verts[2].Z = (float)xfloor->model->ceilingplane.ZatPoint(v2);
			verts[3].Z = (float)xfloor->model->ceilingplane.ZatPoint(v1);

			surf.startVertIndex = MeshVertices.Size();
			surf.numVerts = 4;
			MeshVertices.Push(verts[0]);
			MeshVertices.Push(verts[1]);
			MeshVertices.Push(verts[2]);
			MeshVertices.Push(verts[3]);

			surf.plane = ToPlane(verts[0], verts[1], verts[2]);
			Surfaces.Push(surf);
		}

		float v1TopBack = (float)back->ceilingplane.ZatPoint(v1);
		float v1BottomBack = (float)back->floorplane.ZatPoint(v1);
		float v2TopBack = (float)back->ceilingplane.ZatPoint(v2);
		float v2BottomBack = (float)back->floorplane.ZatPoint(v2);

		if (v1Top == v1TopBack && v1Bottom == v1BottomBack && v2Top == v2TopBack && v2Bottom == v2BottomBack)
		{
			return;
		}

		// bottom seg
		if (v1Bottom < v1BottomBack || v2Bottom < v2BottomBack)
		{
			if (IsBottomSideVisible(side))
			{
				Surface surf;

				FVector3 verts[4];
				verts[0].X = verts[2].X = v1.X;
				verts[0].Y = verts[2].Y = v1.Y;
				verts[1].X = verts[3].X = v2.X;
				verts[1].Y = verts[3].Y = v2.Y;
				verts[0].Z = v1Bottom;
				verts[1].Z = v2Bottom;
				verts[2].Z = v1BottomBack;
				verts[3].Z = v2BottomBack;

				surf.startVertIndex = MeshVertices.Size();
				surf.numVerts = 4;
				MeshVertices.Push(verts[0]);
				MeshVertices.Push(verts[1]);
				MeshVertices.Push(verts[2]);
				MeshVertices.Push(verts[3]);

				surf.plane = ToPlane(verts[0], verts[1], verts[2]);
				surf.type = ST_LOWERWALL;
				surf.typeIndex = typeIndex;
				surf.controlSector = nullptr;

				Surfaces.Push(surf);
			}

			v1Bottom = v1BottomBack;
			v2Bottom = v2BottomBack;
		}

		// top seg
		if (v1Top > v1TopBack || v2Top > v2TopBack)
		{
			bool bSky = IsTopSideSky(front, back, side);
			if (bSky || IsTopSideVisible(side))
			{
				Surface surf;

				FVector3 verts[4];
				verts[0].X = verts[2].X = v1.X;
				verts[0].Y = verts[2].Y = v1.Y;
				verts[1].X = verts[3].X = v2.X;
				verts[1].Y = verts[3].Y = v2.Y;
				verts[0].Z = v1TopBack;
				verts[1].Z = v2TopBack;
				verts[2].Z = v1Top;
				verts[3].Z = v2Top;

				surf.startVertIndex = MeshVertices.Size();
				surf.numVerts = 4;
				MeshVertices.Push(verts[0]);
				MeshVertices.Push(verts[1]);
				MeshVertices.Push(verts[2]);
				MeshVertices.Push(verts[3]);

				surf.plane = ToPlane(verts[0], verts[1], verts[2]);
				surf.type = ST_UPPERWALL;
				surf.typeIndex = typeIndex;
				surf.bSky = bSky;
				surf.controlSector = nullptr;

				Surfaces.Push(surf);
			}

			v1Top = v1TopBack;
			v2Top = v2TopBack;
		}
	}

	// middle seg
	if (back == nullptr)
	{
		Surface surf;

		FVector3 verts[4];
		verts[0].X = verts[2].X = v1.X;
		verts[0].Y = verts[2].Y = v1.Y;
		verts[1].X = verts[3].X = v2.X;
		verts[1].Y = verts[3].Y = v2.Y;
		verts[0].Z = v1Bottom;
		verts[1].Z = v2Bottom;
		verts[2].Z = v1Top;
		verts[3].Z = v2Top;

		surf.startVertIndex = MeshVertices.Size();
		surf.numVerts = 4;
		MeshVertices.Push(verts[0]);
		MeshVertices.Push(verts[1]);
		MeshVertices.Push(verts[2]);
		MeshVertices.Push(verts[3]);

		surf.plane = ToPlane(verts[0], verts[1], verts[2]);
		surf.type = ST_MIDDLEWALL;
		surf.typeIndex = typeIndex;
		surf.controlSector = nullptr;

		Surfaces.Push(surf);
	}
}

void DoomLevelMesh::CreateFloorSurface(FLevelLocals &doomMap, subsector_t *sub, sector_t *sector, int typeIndex, bool is3DFloor)
{
	Surface surf;

	if (!is3DFloor)
	{
		surf.plane = sector->floorplane;
	}
	else
	{
		surf.plane = sector->ceilingplane;
		surf.plane.FlipVert();
	}

	surf.numVerts = sub->numlines;
	surf.startVertIndex = MeshVertices.Size();
	MeshVertices.Resize(surf.startVertIndex + surf.numVerts);
	FVector3* verts = &MeshVertices[surf.startVertIndex];

	for (int j = 0; j < surf.numVerts; j++)
	{
		seg_t *seg = &sub->firstline[(surf.numVerts - 1) - j];
		FVector2 v1 = ToFVector2(seg->v1->fPos());

		verts[j].X = v1.X;
		verts[j].Y = v1.Y;
		verts[j].Z = (float)surf.plane.ZatPoint(verts[j]);
	}

	surf.type = ST_FLOOR;
	surf.typeIndex = typeIndex;
	surf.controlSector = is3DFloor ? sector : nullptr;

	Surfaces.Push(surf);
}

void DoomLevelMesh::CreateCeilingSurface(FLevelLocals &doomMap, subsector_t *sub, sector_t *sector, int typeIndex, bool is3DFloor)
{
	Surface surf;
	surf.bSky = IsSkySector(sector);

	if (!is3DFloor)
	{
		surf.plane = sector->ceilingplane;
	}
	else
	{
		surf.plane = sector->floorplane;
		surf.plane.FlipVert();
	}

	surf.numVerts = sub->numlines;
	surf.startVertIndex = MeshVertices.Size();
	MeshVertices.Resize(surf.startVertIndex + surf.numVerts);
	FVector3* verts = &MeshVertices[surf.startVertIndex];

	for (int j = 0; j < surf.numVerts; j++)
	{
		seg_t *seg = &sub->firstline[j];
		FVector2 v1 = ToFVector2(seg->v1->fPos());

		verts[j].X = v1.X;
		verts[j].Y = v1.Y;
		verts[j].Z = (float)surf.plane.ZatPoint(verts[j]);
	}

	surf.type = ST_CEILING;
	surf.typeIndex = typeIndex;
	surf.controlSector = is3DFloor ? sector : nullptr;

	Surfaces.Push(surf);
}

void DoomLevelMesh::CreateSubsectorSurfaces(FLevelLocals &doomMap)
{
	for (unsigned int i = 0; i < doomMap.subsectors.Size(); i++)
	{
		subsector_t *sub = &doomMap.subsectors[i];

		if (sub->numlines < 3)
		{
			continue;
		}

		sector_t *sector = sub->sector;
		if (!sector || IsControlSector(sector))
			continue;

		CreateFloorSurface(doomMap, sub, sector, i, false);
		CreateCeilingSurface(doomMap, sub, sector, i, false);

		for (unsigned int j = 0; j < sector->e->XFloor.ffloors.Size(); j++)
		{
			CreateFloorSurface(doomMap, sub, sector->e->XFloor.ffloors[j]->model, i, true);
			CreateCeilingSurface(doomMap, sub, sector->e->XFloor.ffloors[j]->model, i, true);
		}
	}
}

bool DoomLevelMesh::IsTopSideSky(sector_t* frontsector, sector_t* backsector, side_t* side)
{
	return IsSkySector(frontsector) && IsSkySector(backsector);
}

bool DoomLevelMesh::IsTopSideVisible(side_t* side)
{
	auto tex = TexMan.GetGameTexture(side->GetTexture(side_t::top), true);
	return tex && tex->isValid();
}

bool DoomLevelMesh::IsBottomSideVisible(side_t* side)
{
	auto tex = TexMan.GetGameTexture(side->GetTexture(side_t::bottom), true);
	return tex && tex->isValid();
}

bool DoomLevelMesh::IsSkySector(sector_t* sector)
{
	return sector->GetTexture(sector_t::ceiling) == skyflatnum;
}

bool DoomLevelMesh::IsControlSector(sector_t* sector)
{
	//return sector->controlsector;
	return false;
}

bool DoomLevelMesh::IsDegenerate(const FVector3 &v0, const FVector3 &v1, const FVector3 &v2)
{
	// A degenerate triangle has a zero cross product for two of its sides.
	float ax = v1.X - v0.X;
	float ay = v1.Y - v0.Y;
	float az = v1.Z - v0.Z;
	float bx = v2.X - v0.X;
	float by = v2.Y - v0.Y;
	float bz = v2.Z - v0.Z;
	float crossx = ay * bz - az * by;
	float crossy = az * bx - ax * bz;
	float crossz = ax * by - ay * bx;
	float crosslengthsqr = crossx * crossx + crossy * crossy + crossz * crossz;
	return crosslengthsqr <= 1.e-6f;
}
```

**Sky decision for a line quad (`bSky`)**: `IsTopSideSky(front, back, side)` returns `IsSkySector(front) && IsSkySector(back)`, where `IsSkySector` checks `sector->GetTexture(sector_t::ceiling) == skyflatnum`.

## 2. LevelMesh base + consumers

### hw_levelmesh.h (full file)

`src/common/rendering/hwrenderer/data/hw_levelmesh.h` (lines 1-34):

```cpp
/*
** hw_levelmesh.h
**
**
**
**---------------------------------------------------------------------------
**
** Copyright 2017-2025 GZDoom Maintainers and Contributors
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
*/

#pragma once

#include "tarray.h"
#include "vectors.h"

namespace hwrenderer
{

class LevelMesh
{
public:
	virtual ~LevelMesh() = default;

	TArray<FVector3> MeshVertices;
	TArray<int> MeshUVIndex;
	TArray<uint32_t> MeshElements;
	TArray<int> MeshSurfaces;
};

} // namespace
```

### All consumers of hwrenderer::LevelMesh and its arrays

| File:Line | Usage |
|-----------|-------|
| `src/rendering/hwrenderer/doom_levelmesh.h:37` | `class DoomLevelMesh : public hwrenderer::LevelMesh` |
| `src/rendering/hwrenderer/doom_levelmesh.cpp:36` | `FVector3* verts = &MeshVertices[pos]` |
| `src/rendering/hwrenderer/doom_levelmesh.cpp:40` | `MeshUVIndex.Push(j)` |
| `src/rendering/hwrenderer/doom_levelmesh.cpp:49-52` | `MeshElements.Push(...)` / `MeshSurfaces.Push(...)` |
| `src/rendering/hwrenderer/doom_levelmesh.cpp:60-70` | `MeshElements.Push(...)` / `MeshSurfaces.Push(...)` |
| `src/rendering/hwrenderer/doom_levelmesh.cpp:134-250` | `MeshVertices.Push/Resize` (surface building) |
| `src/common/rendering/v_video.h:178` | `virtual void SetLevelMesh(hwrenderer::LevelMesh *mesh) { }` |
| `src/common/rendering/vulkan/system/vk_renderdevice.h:91` | `void SetLevelMesh(hwrenderer::LevelMesh* mesh) override;` |
| `src/common/rendering/vulkan/system/vk_renderdevice.cpp:531` | `void VulkanRenderDevice::SetLevelMesh(hwrenderer::LevelMesh* mesh)` |
| `src/common/rendering/vulkan/renderer/vk_raytrace.h:35` | `void SetLevelMesh(hwrenderer::LevelMesh* mesh);` |
| `src/common/rendering/vulkan/renderer/vk_raytrace.h:48` | `hwrenderer::LevelMesh NullMesh;` |
| `src/common/rendering/vulkan/renderer/vk_raytrace.h:49` | `hwrenderer::LevelMesh* Mesh = nullptr;` |
| `src/common/rendering/vulkan/renderer/vk_raytrace.cpp:31-44` | `NullMesh.MeshVertices.Push(...)` / `NullMesh.MeshElements.Push(...)` |
| `src/common/rendering/vulkan/renderer/vk_raytrace.cpp:49-61` | `SetLevelMesh` body |
| `src/common/rendering/vulkan/renderer/vk_raytrace.cpp:92-106` | reads `Mesh->MeshVertices` / `Mesh->MeshElements` for GPU upload |
| `src/common/rendering/vulkan/renderer/vk_raytrace.cpp:154` | `accelStructBLDesc.geometry.triangles.maxVertex = Mesh->MeshVertices.Size() - 1;` |
| `src/common/rendering/vulkan/renderer/vk_raytrace.cpp:162` | `uint32_t maxPrimitiveCount = Mesh->MeshElements.Size() / 3;` |
| `src/rendering/hwrenderer/hw_entrypoint.cpp:108` | `screen->SetLevelMesh(camera->Level->levelMesh);` |
| `src/maploader/maploader.cpp:3251` | `Level->levelMesh = new DoomLevelMesh(*Level);` |
| `src/g_levellocals.h:529` | `DoomLevelMesh* levelMesh = nullptr;` |
| `src/p_setup.cpp:378` | `if (levelMesh) delete levelMesh;` |

## 3. VkRaytrace consumption

### SetLevelMesh function

`src/common/rendering/vulkan/renderer/vk_raytrace.cpp` (lines 49-61):

```cpp
void VkRaytrace::SetLevelMesh(hwrenderer::LevelMesh* mesh)
{
	if (!mesh)
		mesh = &NullMesh;

	if (mesh != Mesh)
	{
		Reset();
		Mesh = mesh;
		if (fb->RaytracingEnabled())
		{
			CreateVulkanObjects();
		}
	}
}
```

### NullMesh fallback cube

`src/common/rendering/vulkan/renderer/vk_raytrace.cpp` (lines 29-46):

```cpp
	NullMesh.MeshVertices.Push({ -1.0f, -1.0f, -1.0f });
	NullMesh.MeshVertices.Push({  1.0f, -1.0f, -1.0f });
	NullMesh.MeshVertices.Push({  1.0f,  1.0f, -1.0f });
	NullMesh.MeshVertices.Push({ -1.0f, -1.0f, -1.0f });
	NullMesh.MeshVertices.Push({ -1.0f,  1.0f, -1.0f });
	NullMesh.MeshVertices.Push({  1.0f,  1.0f, -1.0f });
	NullMesh.MeshVertices.Push({ -1.0f, -1.0f,  1.0f });
	NullMesh.MeshVertices.Push({  1.0f, -1.0f,  1.0f });
	NullMesh.MeshVertices.Push({  1.0f,  1.0f,  1.0f });
	NullMesh.MeshVertices.Push({ -1.0f, -1.0f,  1.0f });
	NullMesh.MeshVertices.Push({ -1.0f,  1.0f,  1.0f });
	NullMesh.MeshVertices.Push({  1.0f,  1.0f,  1.0f });
	for (int i = 0; i < 3 * 4; i++)
		NullMesh.MeshElements.Push(i);
```

### Vertex format and acceleration structure creation

`src/common/rendering/vulkan/renderer/vk_raytrace.cpp` (lines 85-165):

```cpp
void VkRaytrace::CreateVertexAndIndexBuffers()
{
	static_assert(sizeof(FVector3) == 3 * 4, "sizeof(FVector3) is not 12 bytes!");

	size_t vertexbuffersize = (size_t)Mesh->MeshVertices.Size() * sizeof(FVector3);
	size_t indexbuffersize = (size_t)Mesh->MeshElements.Size() * sizeof(uint32_t);
	size_t transferbuffersize = vertexbuffersize + indexbuffersize;
	size_t vertexoffset = 0;
	size_t indexoffset = vertexoffset + vertexbuffersize;

	// ... buffer creation and memcpy of MeshVertices/MeshElements ...
}

void VkRaytrace::CreateBottomLevelAccelerationStructure()
{
	// vertexFormat = VK_FORMAT_R32G32B32_SFLOAT  (i.e., 3×float32 = FVector3)
	// vertexStride = sizeof(FVector3)  (12 bytes)
	// indexType = VK_INDEX_TYPE_UINT32
	// maxVertex = Mesh->MeshVertices.Size() - 1
	// maxPrimitiveCount = Mesh->MeshElements.Size() / 3
	// ... standard bottom-level AS build with VK_GEOMETRY_OPAQUE_BIT_KHR ...
}
```

No UV handling at all in the raytracer. It only uses positions + indices to build the BLAS/TLAS.

### vk_raytrace.h class declaration (mesh-relevant portion)

`src/common/rendering/vulkan/renderer/vk_raytrace.h` (lines 31-65):

```cpp
class VkRaytrace
{
public:
	VkRaytrace(VulkanRenderDevice* fb);

	void SetLevelMesh(hwrenderer::LevelMesh* mesh);

	VulkanAccelerationStructure* GetAccelStruct() { return tlAccelStruct.get(); }

private:
	void Reset();
	void CreateVulkanObjects();
	void CreateVertexAndIndexBuffers();
	void CreateBottomLevelAccelerationStructure();
	void CreateTopLevelAccelerationStructure();

	VulkanRenderDevice* fb = nullptr;

	hwrenderer::LevelMesh NullMesh;
	hwrenderer::LevelMesh* Mesh = nullptr;

	std::unique_ptr<VulkanBuffer> vertexBuffer;
	std::unique_ptr<VulkanBuffer> indexBuffer;
	std::unique_ptr<VulkanBuffer> transferBuffer;
	// ... BLAS/TLAS buffers omitted ...
};
```

## 4. Build site + lifetime + SetLevelMesh chain

### Build site: maploader.cpp line 3251

`src/maploader/maploader.cpp` (lines 3190-3265):

```cpp
	Spawn3DFloors();

	SpawnThings(position);

	// Load and link lightmaps - must be done after P_Spawn3DFloors (and SpawnThings? Potentially for baking static model actors?)
	if (!ForceNodeBuild)
	{
		LoadLightmap(map);
	}

	for (unsigned int i = 0; i < MAXPLAYERS; ++i)
	{
		if (Level->PlayerInGame(i) && Level->Players[i]->mo != nullptr)
			Level->Players[i]->health = Level->Players[i]->mo->health;
	}
	if (!map->HasBehavior && !map->isText)
		TranslateTeleportThings();	// [RH] Assign teleport destination TIDs

	if (oldvertextable != nullptr)
	{
		delete[] oldvertextable;
	}

	// set up world state
	SpawnSpecials();

	// disable reflective planes on sloped sectors.
	for (auto &sec : Level->sectors)
	{
		if (sec.floorplane.isSlope()) sec.reflect[sector_t::floor] = 0;
		if (sec.ceilingplane.isSlope()) sec.reflect[sector_t::ceiling] = 0;
		sec.CheckExColorFlag();
	}
	for (auto &node : Level->nodes)
	{
		double fdx = FIXED2DBL(node.dx);
		double fdy = FIXED2DBL(node.dy);
		node.len = (float)g_sqrt(fdx * fdx + fdy * fdy);
	}

	InitRenderInfo();				// create hardware independent renderer resources for the level.
	Level->ClearDynamic3DFloorData();
	CreateVBO(screen->mVertexData, Level->sectors);

	screen->InitLightmap(Level->LMTextureSize, Level->LMTextureCount, Level->LMTextureData);

	for (auto &sec : Level->sectors)
	{
		P_Recalculate3DFloors(&sec);
	}

	InitPortalGroups(Level);
	P_InitHealthGroups(Level);

	if (reloop) LoopSidedefs(false);
	PO_Init();				// Initialize the polyobjs
	if (!Level->IsReentering())
		Level->FinalizePortals();

	Level->aabbTree = new DoomLevelAABBTree(Level);
	Level->levelMesh = new DoomLevelMesh(*Level);   // <-- line 3251
```

At this point:
- 3DFloors spawned and recalculated
- Things spawned (subsectors assigned)
- `SpawnSpecials()` called (DScrollers, movers, etc.)
- `InitRenderInfo()` called (sets transdoor, heights)
- `P_Recalculate3DFloors()` called (final heights)
- Portal groups initialized
- PolyObjs initialized
- AABB tree built immediately before levelMesh

### levelMesh field declaration

`src/g_levellocals.h` (lines 525-530):

```cpp
	TArray<FLinePortalSpan> linePortalSpans;
	FSectionContainer sections;
	FCanvasTextureInfo canvasTextureInfo;
	EventManager *localEventManager = nullptr;
	DoomLevelAABBTree* aabbTree = nullptr;
	DoomLevelMesh* levelMesh = nullptr;
```

### Teardown

`src/p_setup.cpp` (lines 370-390):

```cpp
	PolyBlockMap.Reset();

	deathmatchstarts.Clear();
	AllPlayerStarts.Clear();
	memset(playerstarts, 0, sizeof(playerstarts));
	Scrolls.Clear();
	if (automap) automap->Destroy();
	Behaviors.UnloadModules();
	localEventManager->Shutdown();
	if (aabbTree) delete aabbTree;
	if (levelMesh) delete levelMesh;
	aabbTree = nullptr;
	levelMesh = nullptr;
	VisualThinkerHead = nullptr;
	ActorBehaviors.Clear();
	ClientSideActorBehaviors.Clear();
	if (screen)
		screen->SetAABBTree(nullptr);
}
```

### SetLevelMesh chain

`src/rendering/hwrenderer/hw_entrypoint.cpp` (line 108):
```cpp
	screen->SetLevelMesh(camera->Level->levelMesh);
```

`src/common/rendering/vulkan/system/vk_renderdevice.cpp` (lines 531-534):
```cpp
void VulkanRenderDevice::SetLevelMesh(hwrenderer::LevelMesh* mesh)
{
	mRaytrace->SetLevelMesh(mesh);
}
```

`src/common/rendering/v_video.h` (line 178):
```cpp
	virtual void SetLevelMesh(hwrenderer::LevelMesh *mesh) { }
```

`src/common/rendering/vulkan/system/vk_renderdevice.h` (line 91):
```cpp
	void SetLevelMesh(hwrenderer::LevelMesh* mesh) override;
```

## 5. CMake wiring

The source list is an **explicit list** in `src/CMakeLists.txt`, not a glob. New `.cpp` files in `src/common/rendering/` must be added to the main source list.

`src/CMakeLists.txt` (lines 1047-1051) — where the top-level `common/rendering/` .cpp files are registered:
```
	common/rendering/v_framebuffer.cpp
	common/rendering/v_video.cpp
	common/rendering/r_thread.cpp
	common/rendering/r_videoscale.cpp
	common/rendering/stb_include.cpp
	common/rendering/hwrenderer/hw_draw2d.cpp
```

A new `src/common/rendering/levelmesh.cpp` would be added to this list (after `common/rendering/stb_include.cpp`).

The header include path is already set up at line 565:
```
	common/rendering/*.h
```

Vulkan-specific sources are in a separate `VULKAN_SOURCES` list starting at line 657.

## 6. Sky resolution (HWSkyInfo)

### HWSkyInfo struct

`src/rendering/hwrenderer/scene/hw_portal.h` (lines 31-51):

```cpp
struct HWSkyInfo
{
	float x_offset[3];
	float y_offset;		// doubleskies don't have a y-offset
	FGameTexture * texture[3];
	FTextureID skytexno1;
	bool mirrored;
	bool doublesky;
	bool sky2;
	PalEntry fadecolor;

	bool operator==(const HWSkyInfo & inf)
	{
		return !memcmp(this, &inf, sizeof(*this));
	}
	bool operator!=(const HWSkyInfo & inf)
	{
		return !!memcmp(this, &inf, sizeof(*this));
	}
	void init(HWDrawInfo *di, sector_t* sec, int skypos, int sky1, PalEntry fadecolor);
};
```

### HWSkyInfo::init

`src/rendering/hwrenderer/scene/hw_sky.cpp` (lines 56-110):

```cpp
void HWSkyInfo::init(HWDrawInfo *di, sector_t* sec, int skypos, int sky1, PalEntry FadeColor)
{
	memset(this, 0, sizeof(*this));
	if ((sky1 & PL_SKYFLAT) && (sky1 & (PL_SKYFLAT - 1)))
	{
		const line_t *l = &di->Level->lines[(sky1&(PL_SKYFLAT - 1)) - 1];
		const side_t *s = l->sidedef[0];
		int pos;

		if (di->Level->flags & LEVEL_SWAPSKIES && s->GetTexture(side_t::bottom).isValid())
		{
			pos = side_t::bottom;
		}
		else
		{
			pos = side_t::top;
		}

		FTextureID texno = s->GetTexture(pos);
		auto tex = TexMan.GetGameTexture(texno, true);
		if (!tex || !tex->isValid()) goto normalsky;
		texture[0] = tex;
		skytexno1 = texno;
		x_offset[0] = s->GetTextureXOffset(pos) * (360.f/65536.f);
		y_offset = s->GetTextureYOffset(pos);
		mirrored = !l->args[2];
	}
	else
	{
	normalsky:
		auto skytex1 = GetSkyTexture(sec, skypos, false);
		auto skytex2 = GetSkyTexture(sec, skypos, true);
		if (di->Level->flags&LEVEL_DOUBLESKY)
		{
			auto tex1 = TexMan.GetGameTexture(skytex1);
			texture[1] = tex1;
			x_offset[1] = di->Level->hw_sky1pos;
			doublesky = true;
		}

		if ((di->Level->flags&LEVEL_SWAPSKIES || (sky1 == PL_SKYFLAT) || (di->Level->flags&LEVEL_DOUBLESKY)) &&
			skytex2 != skytex1)	// If both skies are equal use the scroll offset of the first!
		{
			texture[0] = TexMan.GetGameTexture(skytex2, true);
			skytexno1 = skytex2;
			sky2 = true;
			x_offset[0] = di->Level->hw_sky2pos;
		}
		else if (!doublesky)
		{
			texture[0] = TexMan.GetGameTexture(skytex1, true);
			skytexno1 = di->Level->skytexture1;
			x_offset[0] = di->Level->hw_sky1pos;
		}
	}
	// fadecolor set below (omitted for brevity — sets this->fadecolor = FadeColor)
}
```

### GetSkyTexture

`src/rendering/hwrenderer/scene/hw_sky.cpp` (lines 43-47):

```cpp
FTextureID GetSkyTexture(sector_t* sec, int plane, int second)
{
	auto tex = sec->planes[plane].skytexture[second];
	if (tex.isValid()) return tex;
	return second ? sec->Level->skytexture2 : sec->Level->skytexture1;
}
```

### levelSkyPos field

`src/g_levellocals.h` (lines 683-684):
```cpp
	double		sky1pos, sky2pos;
	float		hw_sky1pos, hw_sky2pos, hw_skymistpos, hw_skymistyscale;
```

Set per-frame in `src/rendering/r_sky.cpp` (lines 146-148):
```cpp
		Level->hw_sky1pos = (float)(fmod((mstime * Level->skyspeed1), 1024.) * (90. / 256.));
		Level->hw_sky2pos = (float)(fmod((mstime * Level->skyspeed2), 1024.) * (90. / 256.));
		Level->hw_skymistpos = (float)(fmod((mstime * Level->skymistspeed), 1024.) * (90. / 256.));
```

## 7. Plane math

### secplane_t

`src/gamedata/r_defs.h` (lines 288-460):

```cpp
struct secplane_t
{
	// the plane is defined as a*x + b*y + c*z + d = 0
	// ic is 1/c, for faster Z calculations
	DVector3 normal;
	double  D, negiC;	// negative iC because that also saves a negation in all methods using this.
public:
	bool dithertransflag;
	friend FSerializer &Serialize(FSerializer &arc, const char *key, secplane_t &p, secplane_t *def);

	void set(double aa, double bb, double cc, double dd)
	{
		normal.X = aa;
		normal.Y = bb;
		normal.Z = cc;
		D = dd;
		negiC = -1 / cc;
	}

	void setD(double dd) { D = dd; }
	double fC() const { return normal.Z; }
	double fD() const { return D; }
	bool isSlope() const { return !normal.XY().isZero(); }
	const DVector3 &Normal() const { return normal; }

	int PointOnSide(const DVector3 &pos) const
	{
		double v = (normal | pos) + D;
		return v < -EQUAL_EPSILON ? -1 : v > EQUAL_EPSILON ? 1 : 0;
	}

	double Zat0() const { return negiC*D; }
	fixed_t ZatPoint(fixed_t x, fixed_t y) const = delete;

	double ZatPoint (double x, double y) const
	{
		return (D + normal.X*x + normal.Y*y) * negiC;
	}
	double ZatPoint(const DVector2 &pos) const { return (D + normal.X*pos.X + normal.Y*pos.Y) * negiC; }
	double ZatPoint(const DVector3& pos) const { return (D + normal.X * pos.X + normal.Y * pos.Y) * negiC; }
	double ZatPoint(const FVector2 &pos) const { return (D + normal.X*pos.X + normal.Y*pos.Y) * negiC; }
	double ZatPoint(const FVector3& pos) const { return (D + normal.X * pos.X + normal.Y * pos.Y) * negiC; }
	double ZatPoint(const vertex_t *v) const { return (D + normal.X*v->fX() + normal.Y*v->fY()) * negiC; }
	double ZatPointDist(const vertex_t *v, double dist) { return (dist + normal.X*v->fX() + normal.Y*v->fY()) * negiC; }

	void FlipVert ()
	{
		normal = -normal;
		D = -D;
		negiC = -negiC;
	}

	bool operator== (const secplane_t &other) const { return normal == other.normal && D == other.D; }
	bool operator!= (const secplane_t &other) const { return normal != other.normal || D != other.D; }

	void SetAtHeight(double height, int ceiling) { /* normal=0,0,±1; D=±height */ }
	bool CopyPlaneIfValid (secplane_t *dest, const secplane_t *opp) const;
	inline double ZatPoint(const AActor *ac) const;
};
```

### GetPlaneTexZ

`src/gamedata/r_defs.h` (lines 974-976):
```cpp
	double GetPlaneTexZ(int pos) const
	{
		return planes[pos].TexZ;
	}
```

### plane_t / splane members of sector_t relevant to TexZ

`src/gamedata/r_defs.h` (lines 630-641):
```cpp
	struct splane
	{
		FTransform xform;
		int Flags;
		int Light;
		double alpha;
		double TexZ;
		PalEntry GlowColor;
		float GlowHeight;
		FTextureID Texture;
		TextureManipulation TextureFx;
		FTextureID skytexture[2];
	};

	splane planes[2];
```

`sector_t` also has:
```cpp
	secplane_t	floorplane, ceilingplane;	// line 671
```

## 8. Load-dynamic detection (scroll/transdoor/alpha)

### (a) Scroll sector special mechanism

Scroll sectors are implemented as `DScroller` thinkers created during `SpawnSpecials()` (called at maploader line 3214, before levelMesh at line 3251).

`EScroll` enum in `src/playsim/p_spec.h` (lines 34-41):
```cpp
enum class EScroll : int
{
		sc_side,
		sc_floor,
		sc_ceiling,
		sc_carry,
		sc_carry_ceiling,
};
```

`DScroller` class in `src/playsim/mapthinkers/a_scroll.h` (lines 43-81):
- `m_Type` (EScroll) determines whether it scrolls floor/ceiling/side
- `m_Sector` — affected sector
- `m_Side` — affected side (for sc_side)
- Detection: iterate `Level->GetThinkerIterator<DScroller>(NAME_None, STAT_SCROLLER)`, check `scroller->GetSector() == sector` and `scroller->IsType(EScroll::sc_floor)` / `sc_ceiling`

### (b) Transdoor flag

`src/gamedata/r_defs.h` (line 693):
```cpp
	bool transdoor;							// For transparent door hacks
```
(line 707):
```cpp
	double transdoorheight;	// for transparent door hacks
```

Set in `src/maploader/renderinfo.cpp` (lines 317-318):
```cpp
	sector->transdoorheight=sector->GetPlaneTexZ(sector_t::floor);
	sector->transdoor= !(sector->e->XFloor.ffloors.Size() || sector->heightsec || sector->floorplane.isSlope());
```
Then cleared if conditions not met (lines 320-374). This happens during `InitRenderInfo()`, which is called before levelMesh construction.

### (c) Alpha flats detection

`src/rendering/hwrenderer/scene/hw_flats.cpp` (line 537):
```cpp
		if (alpha != 0.f && frontsector->GetTexture(sector_t::floor) != skyflatnum)
```
`src/rendering/hwrenderer/scene/hw_flats.cpp` (line 594):
```cpp
		if (alpha != 0.f && frontsector->GetTexture(sector_t::ceiling) != skyflatnum)
```

The `alpha` value comes from `sector_t::planes[pos].alpha` (the `splane::alpha` field). Wall alpha: `seg->sidedef->HasAlpha()` and `seg->linedef->alpha != 0` at `hw_walls.cpp:1628-1638`.

## 9. 3D floors

### F3DFloor struct

`src/playsim/p_3dfloors.h` (lines 87-122):

```cpp
struct F3DFloor
{
	struct planeref
	{
		secplane_t *	plane;
		const FTextureID *	texture;
		sector_t *		model;
		int				isceiling;
		int				vindex;
		bool			copied;

		void copyPlane(planeref * other) { *this = *other; copied = true; }
	};

	planeref			bottom;
	planeref			top;

	short				*toplightlevel;

	unsigned int		flags;
	line_t*				master;

	sector_t *			model;
	sector_t *			target;

	int					lastlight;
	int					alpha;

	FColormap GetColormap();
	void UpdateColormap(FColormap &map);
	PalEntry GetBlend();
};
```

`P_Recalculate3DFloors` signature: `void P_Recalculate3DFloors(sector_t *);` (p_3dfloors.h line 143).
The 3D floor's `model` field is a `sector_t*` that holds the floor/ceiling planes used for Z calculation.

## 10. Lighting fields + fake contrast formulas

### side_t lighting fields

`src/gamedata/r_defs.h` (side_t struct, lines 1195-1325):
```cpp
	int16_t		Light;
	int16_t		TierLights[3];	// per-tier light levels
	uint16_t	Flags;
```

WALLF flags (lines 1170-1193):
```cpp
	WALLF_ABSLIGHTING			= 1,
	WALLF_NOAUTODECALS			= 2,
	WALLF_NOFAKECONTRAST		= 4,
	WALLF_SMOOTHLIGHTING		= 8,
	WALLF_CLIP_MIDTEX			= 16,
	WALLF_WRAP_MIDTEX			= 32,
	WALLF_POLYOBJ				= 64,
	WALLF_LIGHT_FOG				= 128,
	WALLF_EXTCOLOR				= 256,
	WALLF_ABSLIGHTING_TIER		= 512,
	WALLF_ABSLIGHTING_TOP		= 512,
	WALLF_ABSLIGHTING_MID		= 1024,
	WALLF_ABSLIGHTING_BOTTOM 	= 2048,
	WALLF_BLOCKRENDERING		= 4096,
	WALLF_DITHERTRANS			= 8192,
	WALLF_DITHERTRANS_TOP		= 8192,
	WALLF_DITHERTRANS_MID		= 16384,
	WALLF_DITHERTRANS_BOTTOM	= 32768,
```

### sector_t lighting fields

```cpp
	short		lightlevel;
	FColormap Colormap;
	uint32_t selfmap, bottommap, midmap, topmap;
```

### FColormap

`src/common/engine/fcolormap.h` (lines 30-88):
```cpp
struct FColormap
{
	PalEntry		LightColor;		// a is saturation
	PalEntry		FadeColor;		// a is fadedensity>>1
	uint8_t			Desaturation;
	uint8_t			BlendFactor;
	uint16_t		FogDensity;

	void Clear() { LightColor = Color::str("#fff"); FadeColor = 0; Desaturation = 0; BlendFactor = 0; FogDensity = 0; }
	void MakeWhite() { LightColor = Color::str("#fff"); }
	void ClearColor() { LightColor = Color::str("#fff"); BlendFactor = 0; Desaturation = 0; }
	void CopyLight(FColormap &from) { LightColor = from.LightColor; Desaturation = from.Desaturation; BlendFactor = from.BlendFactor; }
	void CopyFog(FColormap &from) { FadeColor = from.FadeColor; FogDensity = from.FogDensity; }
	void Decolorize() { LightColor.Decolorize(); }
	bool operator == (const FColormap &other) { /* ... */ }
	bool operator != (const FColormap &other) { return !operator==(other); }
};
```

### CalcRelLight (fake contrast formula)

`src/rendering/hwrenderer/scene/hw_walls.cpp` (lines 2100-2115):
```cpp
inline int CalcRelLight(int lightlevel, int orglightlevel, int rel)
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
```

### side_t::GetLightLevel

`src/playsim/p_sectors.cpp` (lines 1579-1634):
```cpp
int side_t::GetLightLevel (bool foggy, int baselight, int which, bool is3dlight, int *pfakecontrast) const
{
	if (!is3dlight)
	{
		if (Flags & (WALLF_ABSLIGHTING_TIER << which))
			baselight = TierLights[which];
		else if (Flags & WALLF_ABSLIGHTING)
			baselight = Light + TierLights[which];
	}

	if (pfakecontrast != NULL) *pfakecontrast = 0;

	if (!foggy || sector->Level->flags3 & LEVEL3_FORCEFAKECONTRAST)
	{
		if (!(Flags & WALLF_NOFAKECONTRAST) && r_fakecontrast != 0)
		{
			DVector2 delta = linedef->Delta();
			int rel;
			if (((sector->Level->flags2 & LEVEL2_SMOOTHLIGHTING) || (Flags & WALLF_SMOOTHLIGHTING) || r_fakecontrast == 2) &&
				delta.X != 0)
			{
				rel = RoundHalfUp(
					sector->Level->WallHorizLight
					+ fabs(atan(delta.Y / delta.X) / 1.57079)
					* (sector->Level->WallVertLight - sector->Level->WallHorizLight)
				);
			}
			else
			{
				rel = delta.X == 0 ? sector->Level->WallVertLight :
					  delta.Y == 0 ? sector->Level->WallHorizLight : 0;
			}
			if (pfakecontrast != NULL) *pfakecontrast = rel;
			else baselight += rel;
		}
	}
	if (!is3dlight && !(Flags & WALLF_ABSLIGHTING) && !(Flags & (WALLF_ABSLIGHTING_TIER << which)) && (!foggy || (Flags & WALLF_LIGHT_FOG)))
	{
		baselight += this->Light + this->TierLights[which];
	}
	return baselight;
}
```

### RescaleLightLevel

`src/rendering/hwrenderer/scene/hw_lighting.h` (lines 34-45):
```cpp
template<bool doClamp = true>
inline int RescaleLightLevel(int lightlevel)
{
	if constexpr(doClamp)
		return max(int((clamp(lightlevel, 0, 255) / 255.0) * (255.0 - r_extralight)) + r_extralight, 0);
	else
		return max(int((max(lightlevel, 0) / 255.0) * (255.0 - r_extralight)) + r_extralight, 0);
}
```

### CalcLightColor signature

`src/rendering/hwrenderer/scene/hw_drawinfo.h` (line 374):
```cpp
PalEntry CalcLightColor(ELightMode lightmode, int light, PalEntry pe, int blendfactor);
```

## 11. Band mechanism (main.vp)

No file `src/rendering/hwrenderer/shaders/main.vp` exists in the tree. The band mechanism (uBandIndex / uSplitTopPlane / uSplitBottomPlane) is a design element for the new levelmesh renderer and has no existing implementation in the current codebase. The existing classic renderer does not use a band mechanism.

This is a gap — the band mechanism is a new concept from the levelmesh design, not a reference to existing code.

## 12. perflog pattern

`src/common/rendering/hwrenderer/data/hw_clock.cpp` (lines 205-290):

The `r_perflog` cvar system:
- `CVAR(String, r_perflog, "", CVAR_NOSAVE)` at line 227
- `PerfLogUpdate()` reads the cvar, opens/closes the file, writes per-frame lines
- `PerfLogHeader()` writes column headers: frame, tic, total, render, bsp, clip, wall, flat, sprite, 2d, 3d, finish, portal, job, drawcall, wall count/splits/verts, flat count/prims/verts, sprite count, decal count, portal count, cbuf count
- `PerfLogWriteLevel()` writes a level summary line: map, sectors, lines, subsectors, sprites, polyobjs, lineportals, portalgroups, ffloors
- The file is flushed per frame
- Comment at lines 215-223: "Exposes the per-frame profiling timers above to a file... Used by the levelmesh verification tooling (tools/abtest)"

## 13. ADRs summary

- **0001 (full parity)**: The levelmesh path must achieve full parity with the classic path including line portals, sector_link, fake flats, and all sector effects. Rejected pragmatic parity because portal maps are a visible part of UZDoom content.

- **0002 (vertex warp)**: Moving sector geometry is warped in the vertex shader from a per-frame sector state buffer carrying single current CPU-interpolated plane values. No dynamic VBO, no prev/current pair. This makes A/B parity on moving geometry exact by construction.

- **0003 (full streaming)**: Every frame, the levelmesh snapshot pass copies all sector state records (96 bytes each) into the current ring slot. No dirty tracking. This makes the A/B tool deterministic. The ring slots exist for multi-frame pipeline depth.

- **0004 (A/B policy)**: Zero-tolerance pixel diffing with a frozen known-diff allowlist for z-fighting only. The allowlist is a permanent reviewed repo artifact that can only shrink. A diff outside it is a bug by definition.

## 14. FLevelLocals fields + teardown

`src/g_levellocals.h` (lines 484-495):
```cpp
	TArray<vertex_t> vertexes;
	TArray<sector_t> sectors;
	TArray<extsector_t> extsectors;
	TArray<line_t*> linebuffer;
	TArray<subsector_t*> subsectorbuffer;
	TArray<line_t> lines;
	TArray<side_t> sides;
	TArray<seg_t *> segbuffer;
	TArray<seg_t> segs;
	TArray<subsector_t> subsectors;
	TArray<node_t> nodes;
	TArray<subsector_t> gamesubsectors;
```

The `levelMesh` field is at line 529:
```cpp
	DoomLevelMesh* levelMesh = nullptr;
```

Teardown is in `P_FreeLevelData()` in `src/p_setup.cpp` (lines 377-380):
```cpp
	if (aabbTree) delete aabbTree;
	if (levelMesh) delete levelMesh;
	aabbTree = nullptr;
	levelMesh = nullptr;
```

A new `FLevelMesh*` field would be added adjacent to `levelMesh` and freed in the same area.

## 15. Sprite subsector assignment

The main assignment site is via `P_EnqueueSprites` / `P_EnqueueMobjSprites` which use `Level->PointInRenderSubsector(pos)` to find the subsector. The direct assignment visible in the codebase:

`src/playsim/p_effect.cpp` (line 331):
```cpp
		particle->subsector = Level->PointInRenderSubsector(particle->Pos);
```

`src/g_level.cpp` (line 1720):
```cpp
		mo->subsector = nullptr;
```
(This is a null-out during level cleanup.)

The standard pattern is `AActor::subsector` is set during `P_EnqueueMobjSprites` / `P_EnqueueSprites` by calling `PointInRenderSubsector` on the actor's position. This happens in the render preparation, not at load time. At load time, `SpawnThings` in maploader assigns the actor's position, and the subsector is resolved at render time.

## 16. Core structs (line_t, sector_t, subsector_t, side_t)

### linebase_t

`src/gamedata/r_defs.h` (lines 1510-1524):
```cpp
struct linebase_t
{
	vertex_t* v1, * v2;
	DVector2	delta;
	DVector2 Delta() const { return delta; }
	void setDelta(double x, double y) { delta = { x, y }; }
};
```

### line_t

`src/gamedata/r_defs.h` (lines 1526-1560):
```cpp
struct line_t : public linebase_t
{
	uint32_t	flags, flags2;
	uint32_t	activation;
	int			special;
	int			args[5];
	double		alpha;
	side_t		*sidedef[2];
	double		bbox[4];
	sector_t	*frontsector, *backsector;
	int 		validcount;
	int			locknumber;
	unsigned	portalindex;
	unsigned	portaltransferred;
	AutomapLineStyle automapstyle;
	int			health;
	int			healthgroup;
	int			linenum;

	void setAlpha(double a) { alpha = a; }
	FSectorPortal *GetTransferredPortal();
	void AdjustLine();
	inline FLevelLocals *GetLevel() const;
	inline FLinePortal *getPortal() const;
	inline bool isLinePortal() const;
	inline bool isVisualPortal() const;
	inline line_t *getPortalDestination() const;
	inline int getPortalFlags() const;
	inline int getPortalAlignment() const;
	inline int getPortalType() const;
	inline DVector2 getPortalDisplacement() const;
	inline DAngle getPortalAngleDiff() const;
	inline bool hitSkyWall(AActor* mo) const;
	int Index() const { return linenum; }
};
```

### side_t (key fields)

`src/gamedata/r_defs.h` (lines 1195-1290):
```cpp
struct side_t
{
	enum ETexpart { top=0, mid=1, bottom=2, none=1 };
	enum EColorSlot { walltop = 0, wallbottom = 1 };
	enum ESkew { skew_none=0, skew_front_floor=1, skew_front_ceiling=2, skew_back_floor=3, skew_back_ceiling=4 };

	struct part {
		enum EPartFlags { NoGradient=1, FlipGradient=2, ClampGradient=4, UseOwnSpecialColors=8, UseOwnAdditiveColor=16 };
		double xOffset, yOffset, xScale, yScale;
		TObjPtr<DInterpolation*> interpolation;
		int16_t flags;
		int8_t skew;
		FTextureID texture;
		TextureManipulation TextureFx;
		PalEntry SpecialColors[2];
		PalEntry AdditiveColor;
	};

	sector_t*	sector;
	DBaseDecal*	AttachedDecals;
	part		textures[3];
	line_t		*linedef;
	uint32_t	LeftSide, RightSide;
	uint16_t	TexelLength;
	int16_t		Light;
	int16_t		TierLights[3];
	uint16_t	Flags;
	double		alpha;
	int			UDMFIndex;
	LightmapSurface* lightmap;
	TArray<FDynamicLight*> dlist;
	seg_t **segs;
	int numsegs;
	int sidenum;
	int dithertranscount;

	int GetLightLevel(bool foggy, int baselight, int which, bool is3dlight=false, int *pfakecontrast=NULL) const;
	void SetLight(int16_t l) { Light = l; }
	void SetLight(int16_t l, int which) { TierLights[which] = l; }
	void SetAlpha(double a) { alpha = a; }
	void ClearAlpha() { alpha = DBL_MAX; }
	bool HasAlpha() { return alpha != DBL_MAX; }
	FLevelLocals *GetLevel() { return sector->Level; }
	FTextureID GetTexture(int which) const { return textures[which].texture; }
	// ... offset/scale accessors ...
};
```

### sector_t (key fields)

`src/gamedata/r_defs.h` (lines 613-720):
```cpp
struct sector_t
{
	enum { floor, ceiling, walltop, wallbottom, sprites };
	enum { CeilingMove, FloorMove, CeilingScroll, FloorScroll };
	enum { vbo_fakefloor = floor+2, vbo_fakeceiling = ceiling+2 };
	enum { INVALIDATE_PLANES = 1, INVALIDATE_OTHER = 2 };

	struct splane {
		FTransform xform;
		int Flags;
		int Light;
		double alpha;
		double TexZ;
		PalEntry GlowColor;
		float GlowHeight;
		FTextureID Texture;
		TextureManipulation TextureFx;
		FTextureID skytexture[2];
	};
	splane planes[2];

	FLevelLocals *Level;
	extsector_t *e;
	secplane_t	floorplane, ceilingplane;
	DVector2	centerspot;
	TStaticPointedArray<line_t *> Lines;
	sector_t *heightsec;
	struct msecnode_t *sectorportal_thinglist;
	struct msecnode_t *touching_renderthings;
	PalEntry SpecialColors[5];
	PalEntry AdditiveColors[5];
	FColormap Colormap;
	int		special;
	int			skytransfer;
	int 		validcount;
	uint32_t selfmap, bottommap, midmap, topmap;
	bool transdoor;
	short		lightlevel;
	uint16_t MoreFlags;
	uint32_t Flags;
	unsigned Portals[2];
	int PortalGroup;
	int							sectornum;
	int							subsectorcount;
	float						reflect[2];
	double						transdoorheight;
	subsector_t **				subsectors;
	FSectorPortalGroup *		portals[2];
	int				vboindex[4];
	int				iboindex[4];
	double			vboheight[HW_MAX_PIPELINE_BUFFERS][2];
	int				vbocount[2];
	int				ibocount;
	bool HasLightmaps = false;
};
```

### subsector_t

`src/gamedata/r_defs.h` (lines 1675-1692):
```cpp
struct subsector_t
{
	sector_t	*sector;
	FPolyNode	*polys;
	FMiniBSP	*BSP;
	seg_t		*firstline;
	sector_t	*render_sector;
	FSection	*section;
	int			subsectornum;
	uint32_t	numlines;
	uint16_t	flags;
	short		mapsection;
	FBoundingBox	bbox;
	int				validcount;
	// ... (sprites array, lightmap, etc. follow)
};
```

### FWall

There is no `FWall` struct in this codebase. The wall concept is represented by `side_t` (the sidedef), which carries `Light`, `TierLights[3]`, and `Flags` (WALLF_*). The `side_t` struct IS the per-wall record.

## 17. HW_MAX_PIPELINE_BUFFERS

`src/common/rendering/hwrenderer/data/buffers.h` (lines 31-37):
```cpp
#ifdef __ANDROID__
#define HW_MAX_PIPELINE_BUFFERS 4
#define HW_BLOCK_SSBO 1
#else
// On desktop this is only useful fpr letting the GPU run in parallel with the playsim and for that 2 buffers are enough.
#define HW_MAX_PIPELINE_BUFFERS 2
#endif
```

## 18. gl_uselevelmesh cvar

`src/common/rendering/hwrenderer/data/hw_cvars.cpp` (line 50):
```cpp
	CVAR(Bool, gl_uselevelmesh, false, CVAR_ARCHIVE|CVAR_GLOBALCONFIG)
```

Nothing reads it yet (it was added by ticket 01). It is declared but has no `if (gl_uselevelmesh)` check anywhere in the tree.

## 19. A/B tool interface

`tools/abtest.py` subcommands:
- `run` — drive A/B capture + pixel-diff. Options: `--determinism`, `--map NAME`, `--only PAT`, `--mode M`, `--backend B`, `--manifest F`, `--allowlists DIR`, `--engine PATH`, `--timeout S`, `--runs DIR`
- `diff DIRA DIRB` — standalone diff of two run dirs
- `record MAP MODE BACKEND DIRA DIRB` — record a new allowlist
- `validate` — validate manifest + allowlists

`tools/abtest/ab_manifest.json` structure (first 40 lines):
- `"format": "ab-manifest/1"`
- `"resolution": [2560, 1440]`
- `"backends": { "gl33": 0, "vulkan": 1 }`
- `"determinism_map": "DOOM2_MAP01"`
- `"static_ticks_default": [1,2,3,4,6,8,10,12,16,20,24,28,32,40,48,56,64,72,80,96,112,128,144,160,176,192,200,250,300,400]`
- `"demo_ticks": { "start": 30, "step": 30, "last": 2599 }`
- `"maps": [ { "name": ..., "iwad": ..., "pwad": ..., "warp": ..., "demo": ..., "static_ticks": ... }, ... ]`
- 26 maps total across DOOM2, HEXEN, HERETIC, MYHOUSE, PIRATES, SOS, PLANISF

## 20. Git state

- **Branch**: `agent-levelmesh`
- **Status**: clean (no untracked/modified files)
- **Last 5 commits**:
  1. `8cf1077264` ab capture: lock all inputs during capture session
  2. `45b2ba4814` Fix Vulkan present clipping right/bottom to black when swapchain is clamped
  3. `8e9f9ca835` abtest: don't crash the matrix on an oversized diff cluster
  4. `4f63f884b7` Add A/B capture mode for levelmesh rendering (ticket 01)
  5. `dcacc04a7f` levelmesh: break the handoff spec into implementation tickets (new effort)

## 21. Gaps/notes

1. **Band mechanism (item 14)**: No `main.vp` file exists. The band mechanism (uBandIndex / uSplitTopPlane / uSplitBottomPlane) is a new design concept for the levelmesh renderer, not a reference to existing code. The implementer should treat this as a greenfield shader feature, not a port.

2. **FWall struct (item 19)**: There is no `FWall` struct. The per-wall lighting data lives on `side_t` directly: `int16_t Light`, `int16_t TierLights[3]`, `uint16_t Flags` (WALLF_*). The 64-byte surface record's int16 light block should mirror `side_t::Light` + `side_t::TierLights[3]` (total 4×int16 = 8 bytes for the light portion).

3. **Sprite subsector (item 18)**: Subsector assignment happens at render time via `PointInRenderSubsector()`, not at load time. The `AActor::subsector` field is populated during sprite enqueue. For the levelmesh's per-region actor list, the subsector is determined by the same `PointInRenderSubsector` lookup at build time.

4. **`FLevelLocals` has no destructor**: The teardown is in `P_FreeLevelData()` in `p_setup.cpp`. A new `FLevelMesh*` field must be freed there, adjacent to the existing `levelMesh` delete.

5. **Sky resolution for levelmesh**: The `HWSkyInfo::init` function is per-frame (uses `hw_sky1pos` which changes with time). For a load-time frozen mesh, only the sky *type* (sky texture ID, doublesky, sky2, fadecolor) needs to be baked. The scroll offset is a per-frame parameter. The `IsSkySector` check (`ceiling == skyflatnum`) is the load-time signal.

6. **`IsControlSector` always returns false**: The existing DoomLevelMesh has `IsControlSector` stubbed to return `false` (commented-out `sector->controlsector` check). This means 3D floor control sectors are currently not excluded from the mesh.

7. **CMake**: A new `src/common/rendering/levelmesh.cpp` must be added to the explicit source list in `src/CMakeLists.txt` around line 1051. The include path `common/rendering/*.h` is already configured.

8. **VkRaytrace is unchanged**: The raytracer only reads `MeshVertices` (as `VK_FORMAT_R32G32B32_SFLOAT`) and `MeshElements` (as `VK_INDEX_TYPE_UINT32`). The `DoomLevelMesh` adapter just needs to keep populating these arrays identically. No UV or surface data is consumed by the raytracer.

9. **`side_t::alpha` uses `DBL_MAX` as sentinel**: `ClearAlpha()` sets alpha to `DBL_MAX` meaning "not set, use linedef alpha". `HasAlpha()` checks `alpha != DBL_MAX`. This is relevant for alpha flat detection in the levelmesh.

10. **`P_Recalculate3DFloors` is called before levelMesh construction**: At maploader line 3240, all 3D floor heights are finalized before the levelMesh is built at line 3251. The F3DFloor `model` sector's planes are at their final values.
