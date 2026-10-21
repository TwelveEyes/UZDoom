/*
** Attributes.cpp
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
*/

#include <stdexcept>
#include "Attributes.h"
#include "printf.h"

namespace Helion::Render::OpenGL::Vertex
{

int Attributes::PrimitiveSize(VertexAttribPointerType type)
{
	switch (type)
	{
	case VertexAttribPointerType::Byte:
	case VertexAttribPointerType::UnsignedByte:
		return 1;
	case VertexAttribPointerType::Short:
	case VertexAttribPointerType::UnsignedShort:
		return 2;
	case VertexAttribPointerType::Int:
	case VertexAttribPointerType::UnsignedInt:
	case VertexAttribPointerType::Float:
		return 4;
	}
	return 4;
}

int Attributes::PrimitiveSize(VertexAttribIntegerType type)
{
	switch (type)
	{
	case VertexAttribIntegerType::Byte:
	case VertexAttribIntegerType::UnsignedByte:
		return 1;
	case VertexAttribIntegerType::Short:
	case VertexAttribIntegerType::UnsignedShort:
		return 2;
	case VertexAttribIntegerType::Int:
	case VertexAttribIntegerType::UnsignedInt:
		return 4;
	}
	return 4;
}

static VertexAttribIntegerType ToIntegerType(VertexAttribPointerType type)
{
	switch (type)
	{
	case VertexAttribPointerType::Byte: return VertexAttribIntegerType::Byte;
	case VertexAttribPointerType::UnsignedByte: return VertexAttribIntegerType::UnsignedByte;
	case VertexAttribPointerType::Short: return VertexAttribIntegerType::Short;
	case VertexAttribPointerType::UnsignedShort: return VertexAttribIntegerType::UnsignedShort;
	case VertexAttribPointerType::UnsignedInt: return VertexAttribIntegerType::UnsignedInt;
	default: return VertexAttribIntegerType::Int;
	}
}

TArray<VaoAttribute> Attributes::BuildAttributes(std::span<const VertexAttributeAttribute> fields)
{
	int offset = 0;
	int stride = 0;
	int nextAvailableIndex = 0;
	std::unordered_set<int> indexUsed;
	TArray<VaoAttribute> attributes;

	for (const VertexAttributeAttribute& codeAttr : fields)
	{
		if (codeAttr.Name == nullptr)
			throw std::logic_error("Failed to read attribute name");

		int size = codeAttr.Size;
		int index = codeAttr.Index;
		if (codeAttr.InferIndex())
		{
			while (indexUsed.contains(nextAvailableIndex))
				nextAvailableIndex++;
			index = nextAvailableIndex;
		}
		indexUsed.insert(index);

		if (codeAttr.IsIntegral)
			attributes.Push(VaoAttribute(codeAttr.Name, index, size, ToIntegerType(codeAttr.Type), offset, stride, codeAttr.Required));
		else
			attributes.Push(VaoAttribute(codeAttr.Name, index, size, codeAttr.Type, offset, codeAttr.Normalized, stride, codeAttr.Required, codeAttr.Divisor));

		int sizeBytes = size * PrimitiveSize(codeAttr.Type);
		offset += sizeBytes;
		stride += sizeBytes;
	}

	// Doing two passes over everything is extra code, so we'll update at the end
	// with the correct stride value after accumulating one thing from a single pass.
	for (VaoAttribute& attr : attributes)
		attr.Stride = stride;

	return attributes;
}

TArray<FVertexBufferAttribute> Attributes::ToBackendAttributes(const TArray<VaoAttribute>& attributes)
{
	TArray<FVertexBufferAttribute> result;
	for (const VaoAttribute& attr : attributes)
	{
		if (!attr.PointerType || *attr.PointerType != VertexAttribPointerType::Float)
			throw std::logic_error("Render program vertex attributes must be floats");

		int format;
		switch (attr.Size)
		{
		case 4: format = VFmt_Float4; break;
		case 3: format = VFmt_Float3; break;
		case 2: format = VFmt_Float2; break;
		default: format = VFmt_Float; break;
		}

		FVertexBufferAttribute backendAttr;
		backendAttr.binding = attr.Divisor > 0 ? 1 : 0;
		backendAttr.location = attr.Index;
		backendAttr.format = format;
		backendAttr.offset = attr.Offset;
		result.Push(backendAttr);
	}
	return result;
}

const Shader::ProgramAttribute* Attributes::FindShaderAttribute(const Shader::ProgramAttributes& shaderAttribs, const FString& name)
{
	for (int i = 0; i < shaderAttribs.Count(); i++)
	{
		if (shaderAttribs[i].Name.CompareNoCase(name) == 0)
			return &shaderAttribs[i];
	}

	return nullptr;
}

}
