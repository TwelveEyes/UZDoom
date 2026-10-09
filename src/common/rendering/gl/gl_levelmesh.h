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
** DFrameBuffer::SetLevelMeshData. The record buffers (samplerBuffers) and
** the draw call belong to later chunks.
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

private:
	void Destroy();

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
};

// One per GL frame buffer; owned by the OpenGLFrameBuffer.
extern FGLLevelMesh *GLLevelMesh;

}
