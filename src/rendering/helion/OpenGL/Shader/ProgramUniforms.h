/*
** ProgramUniforms.h
**
** Port of Helion.Render.OpenGL.Shader.ProgramUniforms.
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** Keeps Helion's name -> location API. A location is an index into the program's
** uniform list; Set(value, location) is static and applies to the program that is
** currently bound, exactly like glUniform*. Instead of calling GL, the value is
** written into the std140 CPU copy of the uniform block the uniform lives in; the
** blocks are forwarded to the backend with FRenderState::SetProgramUniforms by
** RenderProgram::FlushState (before every draw). Samplers are not uniform block
** members: Set(TextureUnit, location) selects the TextureUnits slot the sampler reads.
**
*/

#pragma once

#include <span>
#include "tarray.h"
#include "zstring.h"
#include "vectors.h"
#include "matrix.h"
#include "OpenGL/Util/TextureUnit.h"
#include "Util/Geometry.h"

class FRenderState;

namespace Helion::Render::OpenGL::Shader
{

class ProgramUniforms
{
public:
	struct Uniform
	{
		FString Name;
		FString Type;			// GLSL type
		int Block = -1;			// index into Blocks, -1 for samplers
		int Offset = 0;			// byte offset in the block
		int ArrayCount = 0;		// 0 = not an array
		int Binding = -1;		// sampler binding index
		int Unit = -1;			// sampler: texture unit read
	};

	struct Block
	{
		FString Name;
		int Binding = 0;
		TArray<uint8_t> Data;	// std140 image, size rounded up to 16
		bool Dirty = true;
	};

	// Population by RenderProgram once it has laid out the program's interface.
	int AddBlock(const FString& name, int binding, int size);
	void AddUniform(const Uniform& uniform);

	const TArray<Uniform>& Uniforms() const { return m_uniforms; }
	TArray<Block>& Blocks() { return m_blocks; }
	const TArray<Block>& Blocks() const { return m_blocks; }

	// Returns -1 when the program has no such uniform (e.g. it was not declared by this program variant).
	int GetLocation(const FString& name) const;

	// Marks every block dirty (the program is about to be bound, backend slots may hold another program's data).
	void MarkAllDirty();

	// Copies the dirty blocks into the render state.
	void Flush(FRenderState& state);

	// Writes a raw block image (e.g. HWViewpointUniforms for a ViewpointUBO block). No-op false if the block does not exist.
	bool SetBlock(const FString& blockName, const void* data, size_t size);

	// The program the static setters write to; set by RenderProgram::Bind.
	static ProgramUniforms* Current() { return s_current; }
	static void SetCurrent(ProgramUniforms* uniforms) { s_current = uniforms; }

	static void Set(bool value, int location) { Write(location, (int)(value ? 1 : 0)); }
	static void Set(int value, int location) { Write(location, value); }
	static void Set(float value, int location) { Write(location, value); }
	static void Set(std::span<const float> value, int location);
	static void Set(const Geometry::Vec2F& value, int location) { WriteBytes(location, &value, sizeof(float) * 2); }
	static void Set(const Geometry::Vec3F& value, int location) { WriteBytes(location, &value, sizeof(float) * 3); }
	static void Set(const FVector4& value, int location) { WriteBytes(location, &value, sizeof(float) * 4); }
	static void Set(const Geometry::mat4& value, int location) { WriteBytes(location, value.get(), sizeof(float) * 16); }
	static void Set(TextureUnit value, int location);
	static void Set(const Geometry::Vec2I& value, int location) { WriteBytes(location, &value, sizeof(int) * 2); }

	void Set(bool value, const FString& name) { SetOn(*this, GetLocation(name), (int)(value ? 1 : 0)); }
	void Set(int value, const FString& name) { SetOn(*this, GetLocation(name), value); }
	void Set(float value, const FString& name) { SetOn(*this, GetLocation(name), value); }
	void Set(const Geometry::Vec2F& value, const FString& name) { WriteBytesOn(*this, GetLocation(name), &value, sizeof(float) * 2); }
	void Set(const Geometry::Vec3F& value, const FString& name) { WriteBytesOn(*this, GetLocation(name), &value, sizeof(float) * 3); }
	void Set(const FVector4& value, const FString& name) { WriteBytesOn(*this, GetLocation(name), &value, sizeof(float) * 4); }
	void Set(const Geometry::mat4& value, const FString& name) { WriteBytesOn(*this, GetLocation(name), value.get(), sizeof(float) * 16); }
	void Set(TextureUnit value, const FString& name);
	// The uniform at a location of these uniforms (not the current program's).
	void SetAt(int location, const FVector4& value) { WriteBytesOn(*this, location, &value, sizeof(float) * 4); }

	// std140 offsets: bytes of a block holding the given members. Used by RenderProgram.
	static int AlignUp(int value, int alignment) { return (value + alignment - 1) / alignment * alignment; }

private:
	TArray<Block> m_blocks;
	TArray<Uniform> m_uniforms;
	TMap<FString, int> m_nameToLocation;

	inline static ProgramUniforms* s_current = nullptr;

	template<typename T>
	static void Write(int location, T value) { WriteBytes(location, &value, sizeof(T)); }
	template<typename T>
	static void SetOn(ProgramUniforms& uniforms, int location, T value) { WriteBytesOn(uniforms, location, &value, sizeof(T)); }

	static void WriteBytes(int location, const void* data, size_t size);
	static void WriteBytesOn(ProgramUniforms& uniforms, int location, const void* data, size_t size);
};

}
