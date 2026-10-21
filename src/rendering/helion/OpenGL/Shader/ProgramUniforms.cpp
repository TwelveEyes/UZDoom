/*
** ProgramUniforms.cpp
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
*/

#include <cassert>
#include <cstring>
#include "ProgramUniforms.h"
#include "hwrenderer/data/hw_renderstate.h"

namespace Helion::Render::OpenGL::Shader
{

int ProgramUniforms::AddBlock(const FString& name, int binding, int size)
{
	Block block;
	block.Name = name;
	block.Binding = binding;
	block.Data.Resize((unsigned)AlignUp(size > 0 ? size : 16, 16));
	memset(block.Data.Data(), 0, block.Data.Size());
	return (int)m_blocks.Push(block);
}

void ProgramUniforms::AddUniform(const Uniform& uniform)
{
	// Helion: duplicate uniform names are a program bug (Debug.Assert).
	assert(!m_nameToLocation.CheckKey(FString(uniform.Name).MakeLower()) && "Trying to add duplicate uniform name");

	int location = (int)m_uniforms.Push(uniform);
	// The name lookup is case-insensitive (StringComparer.OrdinalIgnoreCase).
	FString key = uniform.Name;
	key.ToLower();
	m_nameToLocation[key] = location;
}

int ProgramUniforms::GetLocation(const FString& name) const
{
	FString key = name;
	key.ToLower();
	const int* location = m_nameToLocation.CheckKey(key);
	return location != nullptr ? *location : -1;
}

void ProgramUniforms::MarkAllDirty()
{
	for (Block& block : m_blocks)
		block.Dirty = true;
}

void ProgramUniforms::Flush(FRenderState& state)
{
	for (Block& block : m_blocks)
	{
		if (!block.Dirty)
			continue;

		state.SetProgramUniforms(block.Binding, block.Data.Data(), block.Data.Size());
		block.Dirty = false;
	}
}

bool ProgramUniforms::SetBlock(const FString& blockName, const void* data, size_t size)
{
	for (Block& block : m_blocks)
	{
		if (block.Name.Compare(blockName) != 0)
			continue;

		size_t count = size < block.Data.Size() ? size : block.Data.Size();
		if (memcmp(block.Data.Data(), data, count) != 0)
		{
			memcpy(block.Data.Data(), data, count);
			block.Dirty = true;
		}
		return true;
	}
	return false;
}

void ProgramUniforms::WriteBytes(int location, const void* data, size_t size)
{
	if (s_current != nullptr)
		WriteBytesOn(*s_current, location, data, size);
}

void ProgramUniforms::WriteBytesOn(ProgramUniforms& uniforms, int location, const void* data, size_t size)
{
	// glUniform on location -1 is silently ignored.
	if (location < 0 || location >= (int)uniforms.m_uniforms.Size())
		return;

	const Uniform& uniform = uniforms.m_uniforms[location];
	if (uniform.Block < 0)
		return;

	Block& block = uniforms.m_blocks[uniform.Block];
	assert(uniform.Offset + size <= block.Data.Size());
	uint8_t* dest = block.Data.Data() + uniform.Offset;
	if (memcmp(dest, data, size) != 0)
	{
		memcpy(dest, data, size);
		block.Dirty = true;
	}
}

void ProgramUniforms::Set(std::span<const float> value, int location)
{
	if (s_current == nullptr || location < 0 || location >= (int)s_current->m_uniforms.Size())
		return;

	// std140 float arrays have a 16 byte element stride.
	for (size_t i = 0; i < value.size(); i++)
	{
		float element = value[i];
		const Uniform& uniform = s_current->m_uniforms[location];
		if (uniform.Block < 0 || (int)i >= (uniform.ArrayCount > 0 ? uniform.ArrayCount : 1))
			return;

		Block& block = s_current->m_blocks[uniform.Block];
		uint8_t* dest = block.Data.Data() + uniform.Offset + i * 16;
		if (memcmp(dest, &element, sizeof(float)) != 0)
		{
			memcpy(dest, &element, sizeof(float));
			block.Dirty = true;
		}
	}
}

void ProgramUniforms::Set(TextureUnit value, int location)
{
	if (s_current == nullptr || location < 0 || location >= (int)s_current->m_uniforms.Size())
		return;

	// Helion: GL.Uniform1(location, unit - TextureUnit.Texture0)
	s_current->m_uniforms[location].Unit = ToInt(value);
}

void ProgramUniforms::Set(TextureUnit value, const FString& name)
{
	int location = GetLocation(name);
	if (location >= 0)
		m_uniforms[location].Unit = ToInt(value);
}

}
