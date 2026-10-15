/*
** lm_data.glsl
**
** Levelmesh GPU records (std430 mirrors of hw_levelmeshdata.h)
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** The including stage defines LM_SET (the descriptor set of the
** LevelMeshSet) and the LMBIND_* / LMB_* / LMSF_* constants; the backend
** generates them from the C++ enums (VkLevelMesh::ShaderDefines).
** Every struct is 32-bit scalars only, so std430 matches the C++ layout.
*/

struct LMVertex
{
	float x, y, zOffset;
	float u, vOffset;
	float lu, lv;
	uint anchor;
	uint surface;
};

struct LMSurface
{
	uint flags;
	uint planeRefs[4];
	uint vRef;
	float vScale;
	int textureId;
	uint sideRef;
	uint lightSector;
	int lightList;
	int lightLevel;
	int relLight;
	int sideLight;
	int fakeContrast;
	uint sideFlags;
	int lightmapPage;
	float normal[3];
	uint firstVertex, numVertices;
	uint firstIndex, numIndices;
	uint edgeTop;
	uint reserved[3];
};

struct LMCluster
{
	float bmin[3];
	uint firstIndex;
	float bmax[3];
	uint numIndices;
	uint bucketIndices[LMB_COUNT];
	uint bucketMask;
	uint firstSector, numSectors;
	uint firstSurface, numSurfaces;
};

struct LMPlane
{
	float nx, ny, d, negiC;
};

struct LMSectorState
{
	LMPlane planes[2];
	float texZ[2];
	int lightLevel;
	int planeLight[2];
	uint lightColor;
	uint fadeColor;
	uint colormap;
	float glowTop[4];
	float glowBottom[4];
};

struct LMResolved
{
	uint color;
	uint fogColor;
	int fogMode;
	float softLight;
	float fogDensity;
	float lightFactor;
	float lightDist;
	uint glow;
	uint reserved[4];
};

struct LMPlaneXform
{
	float xOffset, yOffset, baseyOffset;
	float xScale, yScale;
	float angle, baseAngle;
	uint reserved;
};

struct LMTextureEntry
{
	uint slot;
	uint layers;
	float displayWidth, displayHeight;
};

layout(set = LM_SET, binding = LMBIND_VERTICES, std430) buffer readonly LMVertexBuffer { LMVertex lmVertices[]; };
layout(set = LM_SET, binding = LMBIND_INDICES, std430) buffer readonly LMIndexBuffer { uint lmIndices[]; };
layout(set = LM_SET, binding = LMBIND_SURFACES, std430) buffer readonly LMSurfaceBuffer { LMSurface lmSurfaces[]; };
layout(set = LM_SET, binding = LMBIND_CLUSTERS, std430) buffer readonly LMClusterBuffer { LMCluster lmClusters[]; };
layout(set = LM_SET, binding = LMBIND_SECTORSTATE, std430) buffer readonly LMSectorBuffer { LMSectorState lmSectors[]; };
layout(set = LM_SET, binding = LMBIND_PLANEXFORM, std430) buffer readonly LMPlaneXformBuffer { LMPlaneXform lmPlaneXforms[]; };
layout(set = LM_SET, binding = LMBIND_TEXTURETABLE, std430) buffer readonly LMTextureBuffer { LMTextureEntry lmTextures[]; };
#ifndef LM_RESOLVE_WRITER
layout(set = LM_SET, binding = LMBIND_RESOLVED, std430) buffer readonly LMResolvedBuffer { LMResolved lmResolved[]; };
#endif

// LM_PlaneZ: same operation order as the C++ (no contraction into FMA).
float LM_PlaneZ(LMPlane p, float x, float y)
{
	precise float r = (p.d + p.nx * x + p.ny * y) * p.negiC;
	return r;
}
