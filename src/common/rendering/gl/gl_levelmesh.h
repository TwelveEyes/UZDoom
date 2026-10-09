/*
** gl_levelmesh.h
**
** GL33 vertex GPU objects for the level-mesh draw path (ticket 03, chunk D1).
** The 32-byte vertex pool is uploaded once per level as a single VBO; one
** VAO per region captures the pool's vertex attribute bindings (layout
** matching wadsrc/static/shaders/glsl/levelmesh.vp, see gl_levelmesh.cpp
** for the binding table) together with the region's IBO on
** GL_ELEMENT_ARRAY_BUFFER. Per-region draws are then a VAO bind +
** glDrawElements over the region's sub-range.
**
** Created at level load, destroyed at level unload, through
** DFrameBuffer::SetLevelMeshData. The three record pools (surface records,
** sector state ring, 3D light buffer) are GL texture buffers that
** wadsrc/static/shaders/glsl/levelmesh.vp fetches as samplerBuffers; the
** draw call belongs to a later chunk.
**
**---------------------------------------------------------------------------
**
** Copyright 2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
*/

#pragma once

#include <memory>
#include "gl_buffers.h"
#include "levelmesh.h"

namespace OpenGLRenderer
{

class FGLLevelMesh
{
public:
	FGLLevelMesh() = default;
	~FGLLevelMesh();

	// Rebuild the GPU objects for the given backend-neutral model, or drop
	// them for null. No-op if the objects are already current, so it is safe
	// to call every frame (the draw pass uses it to re-sync after a video
	// mode change, which recreates the GL context).
	void SetMesh(const levelmesh::FLevelMesh *mesh);

	bool IsReady() const { return mVAOs.Size() > 0; }
	size_t RegionCount() const { return mVAOs.Size(); }
	// The model these objects were built from (nullptr if none).
	const levelmesh::FLevelMesh *GetMesh() const { return mMesh; }
	// Number of 32-byte elements in the vertex pool VBO.
	size_t VertexCount() const { return mGpuPool.Size(); }
	// VAO for region r (0 .. RegionCount()-1). Binds it with
	// glBindVertexArray and draws with glDrawElements(GL_TRIANGLES, count,
	// GL_UNSIGNED_INT, byteOffset).
	unsigned int GetVAO(size_t region) const { return mVAOs[region]; }

	//
	// Record pools (chunk D2): the three samplerBuffer textures levelmesh.vp
	// fetches with texelFetch. Created in SetMesh, destroyed in Destroy.
	//

	// Texture bound to the surface record pool: one 24-float (6 float4)
	// record per LevelMeshSurface, slot order exactly as LMFetchSurface
	// fetches it. Static: uploaded once per level load.
	GLuint GetSurfaceTexture() const { return mSurfaceTex; }
	// Texture bound to the sector state ring: HW_MAX_PIPELINE_BUFFERS slots of
	// sectorCount 36-float (9 float4) records, slot-major layout (record r of
	// slot s at float offset s*sectorCount*36 + r*36). GL_DYNAMIC_DRAW.
	GLuint GetSectorStateTexture() const { return mSectorStateTex; }
	// Texture bound to the 3D light buffer: one 12-float (3 float4) record
	// per light. Grow-only capacity, re-uploaded every frame by chunk E.
	GLuint GetLightTexture() const { return mLightTex; }
	// True once all three record pools are created. Independent of IsReady()
	// (which only covers the vertex pool VAOs); both are set together in
	// SetMesh, so a levelmesh draw checks IsRecordsReady() for the fetches.
	bool IsRecordsReady() const { return mSurfaceTex != 0 && mSectorStateTex != 0 && mLightTex != 0; }

	//
	// Per-frame uploads (chunk E calls these from the frame build, inside
	// the interpolation window, right after FLevelMeshState::PackSnapshot).
	//

	// Repack one ring slot (bytes = FLevelMeshState::GetSectorSlot(slot),
	// size = SectorSlotSize() = sectorCount*96) into the 36-float layout and
	// glBufferSubData it at its slot offset. No per-frame allocation: the
	// work area is a member array sized once.
	void UploadSectorSlot(int slot, const uint8_t *bytes);
	// Repack the 3D light buffer (bytes = FLevelMeshState::GetLightData(),
	// byteSize = LightDataSize() = count*12) into the 12-float layout and
	// upload it; glBufferData if the capacity grew, else glBufferSubData at 0.
	void UploadLightData(const uint8_t *bytes, size_t byteSize);

private:
	void Destroy();
	// Create a buffer+texture pair for one samplerBuffer pool: glGenBuffers +
	// glBufferData (data may be null), then glGenTextures + glTexBuffer with
	// GL_RGBA32F and NEAREST/REPEAT parameters. Leaves no state bound.
	void CreateRecordPool(GLuint &buffer, GLuint &tex, size_t byteSize, const void *data, GLenum usage);

	const levelmesh::FLevelMesh *mMesh = nullptr;
	// CPU copy of the vertex pool repacked for the GL attribute layout:
	// byte 28 carries float(surfaceIndex). GLSL 330 has no float->uint
	// bit-cast, so the raw uint32 cannot travel through a float attribute;
	// the float value is exact for indices below 2^24.
	TArray<levelmesh::LevelMeshVertex> mGpuPool;
	// The vertex pool VBO (uploaded once at level load).
	GLVertexBuffer mVertexPool;
	// One IBO per region.
	TArray<std::unique_ptr<GLIndexBuffer>> mRegionIBOs;
	// One VAO per region.
	TArray<unsigned int> mVAOs;

	// Record pools (chunk D2): buffer + texture pairs, 0 = not created. The
	// textures are the objects chunk E binds to sampler units; the buffers
	// are only ever touched through glBufferData/glBufferSubData on the
	// GL_TEXTURE_BUFFER binding point.
	GLuint mSurfaceBuf = 0;		// surface records: surfaceCount * 24 floats, static
	GLuint mSurfaceTex = 0;
	GLuint mSectorStateBuf = 0;	// sector state ring: HW_MAX_PIPELINE_BUFFERS * sectorCount * 36 floats
	GLuint mSectorStateTex = 0;
	GLuint mLightBuf = 0;		// 3D lights: capacity * 12 floats, grow-only
	GLuint mLightTex = 0;
	int mSectorCount = 0;		// sectors per ring slot (offset math for UploadSectorSlot)
	size_t mLightCapacity = 0;	// light record count the light buffer currently holds storage for
	// Repack work area, sized once and grown on demand (no per-frame allocation).
	TArray<float> mRepackWork;
};

// One per GL frame buffer; owned by the OpenGLFrameBuffer.
extern FGLLevelMesh *GLLevelMesh;

}
