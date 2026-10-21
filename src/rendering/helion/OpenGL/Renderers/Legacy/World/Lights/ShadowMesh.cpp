/*
** ShadowMesh.cpp
**
** UZDoom-only (plan M6.3): see ShadowMesh.h. The quad / triangle layout is DoomLevelMesh's (two triangles per wall,
** (0,1,2) and (3,2,1); the acceleration structure is built with the triangle facing cull disabled).
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

#include <algorithm>
#include "OpenGL/Renderers/Legacy/World/Lights/ShadowMesh.h"
#include "g_levellocals.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Lights
{

namespace
{

// DoomLevelMesh::IsDegenerate
bool IsDegenerate(const FVector3& v0, const FVector3& v1, const FVector3& v2)
{
	float ax = v1.X - v0.X, ay = v1.Y - v0.Y, az = v1.Z - v0.Z;
	float bx = v2.X - v0.X, by = v2.Y - v0.Y, bz = v2.Z - v0.Z;
	float crossx = ay * bz - az * by;
	float crossy = az * bx - ax * bz;
	float crossz = ax * by - ay * bx;
	return crossx * crossx + crossy * crossy + crossz * crossz <= 1.e-6f;
}

void AddQuad(ShadowMesh& mesh, int line, const FVector2& a, const FVector2& b, float zLowA, float zLowB, float zHighA, float zHighB)
{
	FVector3 verts[4] = {
		FVector3(a.X, a.Y, zLowA),
		FVector3(b.X, b.Y, zLowB),
		FVector3(a.X, a.Y, zHighA),
		FVector3(b.X, b.Y, zHighB),
	};

	uint32_t pos = mesh.MeshVertices.Size();
	for (int i = 0; i < 4; i++)
	{
		mesh.MeshVertices.Push(verts[i]);
		mesh.MeshUVIndex.Push(i);
	}

	if (!IsDegenerate(verts[0], verts[1], verts[2]))
	{
		mesh.MeshElements.Push(pos + 0);
		mesh.MeshElements.Push(pos + 1);
		mesh.MeshElements.Push(pos + 2);
		mesh.MeshSurfaces.Push(line);
	}
	if (!IsDegenerate(verts[1], verts[2], verts[3]))
	{
		mesh.MeshElements.Push(pos + 3);
		mesh.MeshElements.Push(pos + 2);
		mesh.MeshElements.Push(pos + 1);
		mesh.MeshSurfaces.Push(line);
	}
}

}

std::unique_ptr<ShadowMesh> ShadowMesh::Build(FLevelLocals& level)
{
	std::unique_ptr<ShadowMesh> mesh(new ShadowMesh());

	for (unsigned i = 0; i < level.lines.Size(); i++)
	{
		line_t& line = level.lines[i];
		side_t* front = line.sidedef[0];
		if (front == nullptr || line.frontsector == nullptr)
			continue;
		if (line.isLinePortal() || (front->Flags & WALLF_POLYOBJ))
			continue;

		DVector2 p1 = line.v1->fPos();
		DVector2 p2 = line.v2->fPos();
		FVector2 a((float)p1.X, (float)p1.Y);
		FVector2 b((float)p2.X, (float)p2.Y);

		sector_t* fs = line.frontsector;
		sector_t* bs = line.backsector;
		if (bs == nullptr)
		{
			AddQuad(*mesh, (int)i, a, b, -MaxHeight, -MaxHeight, MaxHeight, MaxHeight);
			continue;
		}

		float ff1 = (float)fs->floorplane.ZatPoint(p1), ff2 = (float)fs->floorplane.ZatPoint(p2);
		float bf1 = (float)bs->floorplane.ZatPoint(p1), bf2 = (float)bs->floorplane.ZatPoint(p2);
		float fc1 = (float)fs->ceilingplane.ZatPoint(p1), fc2 = (float)fs->ceilingplane.ZatPoint(p2);
		float bc1 = (float)bs->ceilingplane.ZatPoint(p1), bc2 = (float)bs->ceilingplane.ZatPoint(p2);

		// the step between the two floors
		if (ff1 != bf1 || ff2 != bf2)
			AddQuad(*mesh, (int)i, a, b, std::min(ff1, bf1), std::min(ff2, bf2), std::max(ff1, bf1), std::max(ff2, bf2));

		// the step between the two ceilings
		if (fc1 != bc1 || fc2 != bc2)
			AddQuad(*mesh, (int)i, a, b, std::min(fc1, bc1), std::min(fc2, bc2), std::max(fc1, bc1), std::max(fc2, bc2));
	}

	return mesh;
}

}
