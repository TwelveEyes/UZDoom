/*
** Attributes.h
**
** Port of Helion.Render.OpenGL.Vertex.Attributes.
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** ReadStructAttributes reads the explicit attribute table of a vertex struct
** (TVertex::VertexAttributes(), see VertexAttributes.h) instead of reflecting
** over the struct's fields; offsets/stride are accumulated exactly like Helion.
** Apply describes the layout to the backend (IVertexBuffer::SetFormat) instead
** of calling glVertexAttribPointer.
**
*/

#pragma once

#include <map>
#include <span>
#include <stdexcept>
#include <typeindex>
#include <unordered_set>
#include "tarray.h"
#include "printf.h"
#include "VaoAttribute.h"
#include "VertexAttributes.h"
#include "VertexArrayObject.h"
#include "OpenGL/Buffer/VertexBufferObject.h"
#include "OpenGL/Shader/ProgramAttributes.h"
#include "hwrenderer/data/buffers.h"

namespace Helion::Render::OpenGL::Vertex
{

class Attributes
{
public:
	// Size in bytes of one component of the given attribute type.
	static int PrimitiveSize(VertexAttribPointerType type);
	static int PrimitiveSize(VertexAttribIntegerType type);

	// Builds the attribute list of a vertex type from its explicit table.
	static TArray<VaoAttribute> BuildAttributes(std::span<const VertexAttributeAttribute> fields);

	// The layout the backend needs: binding point 0 = per vertex, 1 = per instance (Divisor > 0).
	static TArray<FVertexBufferAttribute> ToBackendAttributes(const TArray<VaoAttribute>& attributes);

	template<typename TVertex>
	static const TArray<VaoAttribute>& ReadStructAttributes()
	{
		static const TArray<VaoAttribute> attributes = [] {
			TArray<VaoAttribute> result = BuildAttributes(TVertex::VertexAttributes());
			if (result.Size() > 0 && result[0].Stride != (int)sizeof(TVertex))
				throw std::logic_error("Vertex struct size does not match its attribute table");
			return result;
		}();
		return attributes;
	}

	// Describes the vertex layout to the buffer. Helion's Apply is called with the VAO and VBO bound.
	template<typename TVertex>
	static void Apply(IVertexBuffer* vbo, bool* instanced = nullptr)
	{
		const TArray<VaoAttribute>& attributes = ReadStructAttributes<TVertex>();
		TArray<FVertexBufferAttribute> backendAttributes = ToBackendAttributes(attributes);
		bool isInstanced = false;
		for (const VaoAttribute& attr : attributes)
			isInstanced |= attr.Divisor > 0;

		vbo->SetFormat(isInstanced ? 2 : 1, backendAttributes.SSize(), (size_t)sizeof(TVertex), backendAttributes.Data());
		if (instanced)
			*instanced = isInstanced;
	}

	template<typename TVertex>
	static void BindAndApply(Buffer::Array::Vertex::VertexBufferObject<TVertex>& vbo, VertexArrayObject& vao, const Shader::ProgramAttributes& shaderAttribs)
	{
		AssertCorrectMappingOrThrow<TVertex>(shaderAttribs);

		bool instanced = false;
		Apply<TVertex>(vbo.Vertex(), &instanced);
		vao.SetVertexBuffer(vbo.Vertex(), instanced);
	}

private:
	template<typename TVertex>
	static void AssertCorrectMappingOrThrow([[maybe_unused]] const Shader::ProgramAttributes& shaderAttribs)
	{
#ifndef NDEBUG
		static std::unordered_set<std::type_index> isValid; // If present, there's no undefined attributes, and is safe to use.
		std::type_index type(typeid(TVertex));
		if (isValid.contains(type))
			return;

		for (const VaoAttribute& attr : ReadStructAttributes<TVertex>())
		{
			const Shader::ProgramAttribute* progAttr = FindShaderAttribute(shaderAttribs, attr.Name);
			// Helion: a missing attribute is not an error since the driver optimizes unused attributes away. The
			// program here lists its declarations, so a missing required attribute is a layout mismatch worth flagging.
			if (progAttr == nullptr && attr.Required && shaderAttribs.Count() > 0)
				DPrintf(DMSG_WARNING, "Vertex attribute %s is not declared by the program\n", attr.Name.GetChars());
		}

		isValid.insert(type);
#endif
	}

	static const Shader::ProgramAttribute* FindShaderAttribute(const Shader::ProgramAttributes& shaderAttribs, const FString& name);
};

}
