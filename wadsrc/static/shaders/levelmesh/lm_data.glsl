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
	float xOffsetBase, yOffsetBase;
	float texOffsetScale, rowOffsetScale;
	float renderWidth;
	float wrapWidth;
	uint edgeClamp;
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

struct LMSideState
{
	int textureId;
	float xOffset, yOffset;
	float xScale, yScale;
	uint flags;
	uint reserved[2];
};

struct LMPlaneXform
{
	float xOffset, yOffset, baseyOffset;
	float xScale, yScale;
	float angle, baseAngle;
	int textureId;
	uint flags;
	uint reserved[3];
};

struct LMLight3D
{
	int lightLevel;
	uint lightColor;
	uint fadeColor;
	uint colormap;
	uint flags;
	uint reserved[3];
};

struct LMTextureEntry
{
	uint slot;
	uint layers;
	float displayWidth, displayHeight;
	float shaderSpeed;
	uint reserved[3];
};

layout(set = LM_SET, binding = LMBIND_VERTICES, std430) buffer readonly LMVertexBuffer { LMVertex lmVertices[]; };
layout(set = LM_SET, binding = LMBIND_INDICES, std430) buffer readonly LMIndexBuffer { uint lmIndices[]; };
layout(set = LM_SET, binding = LMBIND_SURFACES, std430) buffer readonly LMSurfaceBuffer { LMSurface lmSurfaces[]; };
layout(set = LM_SET, binding = LMBIND_CLUSTERS, std430) buffer readonly LMClusterBuffer { LMCluster lmClusters[]; };
layout(set = LM_SET, binding = LMBIND_SECTORSTATE, std430) buffer readonly LMSectorBuffer { LMSectorState lmSectors[]; };
layout(set = LM_SET, binding = LMBIND_SIDESTATE, std430) buffer readonly LMSideBuffer { LMSideState lmSides[]; };
layout(set = LM_SET, binding = LMBIND_PLANEXFORM, std430) buffer readonly LMPlaneXformBuffer { LMPlaneXform lmPlaneXforms[]; };
layout(set = LM_SET, binding = LMBIND_TEXTURETABLE, std430) buffer readonly LMTextureBuffer { LMTextureEntry lmTextures[]; };
layout(set = LM_SET, binding = LMBIND_LIGHTS3D, std430) buffer readonly LMLight3DBuffer { LMLight3D lmLights3D[]; };
#ifndef LM_RESOLVE_WRITER
layout(set = LM_SET, binding = LMBIND_RESOLVED, std430) buffer readonly LMResolvedBuffer { LMResolved lmResolved[]; };
#endif

// The surface's current texture (untranslated id) and whether the classic
// path draws it instead: walls follow their side tier, sector planes their
// plane (LMSF_STATETEXTURE, LMTS_DEFERRED); surfaces lit by a 3D light
// follow its sector's 3D-floor layout (LML3_DEFERRED); 3D-floor surfaces
// follow their group's record (sideRef past the side tiers).
int LM_SurfaceTexture(LMSurface surf)
{
	if ((surf.flags & LMSF_STATETEXTURE) == 0u) return surf.textureId;
	if ((surf.flags & LMSF_KINDMASK) == LMSK_WALL) return lmSides[surf.sideRef].textureId;
	return lmPlaneXforms[surf.planeRefs[0]].textureId;
}

bool LM_StateDeferred(LMSurface surf)
{
	if (surf.lightList >= 0 && (lmLights3D[surf.lightList].flags & LML3_DEFERRED) != 0u) return true;
	if ((surf.flags & LMSF_3DFLOOR) != 0u) return (lmSides[surf.sideRef].flags & LMTS_DEFERRED) != 0u;
	if ((surf.flags & LMSF_STATETEXTURE) == 0u) return false;
	if ((surf.flags & LMSF_KINDMASK) == LMSK_WALL) return (lmSides[surf.sideRef].flags & LMTS_DEFERRED) != 0u;
	return (lmPlaneXforms[surf.planeRefs[0]].flags & LMTS_DEFERRED) != 0u;
}

// LM_PlaneZ: same operation order as the C++ (no contraction into FMA).
float LM_PlaneZ(LMPlane p, float x, float y)
{
	precise float r = (p.d + p.nx * x + p.ny * y) * p.negiC;
	return r;
}
