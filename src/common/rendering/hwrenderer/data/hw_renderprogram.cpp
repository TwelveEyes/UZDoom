/*
** hw_renderprogram.cpp
**
** GLSL prelude generation for render programs (hw_renderprogram.h)
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

#include "hw_renderprogram.h"
#include "hw_shaderpatcher.h"

//==========================================================================
//
// Stage visibility of a binding.
//
//==========================================================================

static bool BindingUsedInStage(const RenderProgramBinding& b, bool isFrag, bool isCompute)
{
	if (isCompute) return b.compute;
	return isFrag ? b.fragment : b.vertex;
}

//==========================================================================
//
// GenerateProgramPrelude
//
// GL: #version 330 core, or 430 core when the program uses compute, storage
// buffers or images. No layout(binding) is emitted for GL (3.3 lacks it): the
// backend assigns sampler units (= binding), uniform block and storage block
// binding points after linking.
//
// Vulkan: #version 450 (what the other Vulkan shaders use on pre-1.2 devices);
// program bindings live in descriptor set 3, binding = RenderProgramBinding::binding.
// gl_VertexID / gl_InstanceID are mapped to gl_VertexIndex / gl_InstanceIndex and
// VULKAN_COORDINATE_SYSTEM is defined like for the other Vulkan shaders (the vertex
// shader must remap z with it: gl_Position.z = (gl_Position.z + gl_Position.w) / 2.0).
//
// For Image2D bindings blockTypeName holds the image format qualifier ("rgba8" if empty)
// and qualifiers may be "readonly" / "writeonly".
//
//==========================================================================

FString GenerateProgramPrelude(const RenderProgramDesc& desc, bool isVulkan, bool isFrag, bool isCompute)
{
	FString out;

	bool needs430 = isCompute || desc.isCompute;
	for (auto& b : desc.bindings)
	{
		if (b.kind == RenderProgramBinding::StorageBuffer || b.kind == RenderProgramBinding::Image2D)
			needs430 = true;
	}

	if (isVulkan)
	{
		out << "#version 450\n";
		out << "#define VULKAN_COORDINATE_SYSTEM\n";
		out << "#define gl_VertexID gl_VertexIndex\n";
		out << "#define gl_InstanceID gl_InstanceIndex\n";
	}
	else
	{
		out << (needs430 ? "#version 430 core\n" : "#version 330 core\n");
		// these settings are actually pointless but there seem to be some old ATI drivers that fail to compile the shader without setting the precision here.
		out << "precision highp int;\nprecision highp float;\n";
	}

	if (isVulkan && isFrag && desc.rayQuery)
	{
		out << "#extension GL_EXT_ray_query : require\n";
		out << "layout(set = 0, binding = 2) uniform accelerationStructureEXT TopLevelAS;\n";
	}

	if (isCompute)
	{
		out.AppendFormat("layout(local_size_x = %d, local_size_y = %d, local_size_z = %d) in;\n",
			desc.computeLocalSize[0], desc.computeLocalSize[1], desc.computeLocalSize[2]);
	}

	// Resource bindings
	for (auto& b : desc.bindings)
	{
		if (!BindingUsedInStage(b, isFrag, isCompute)) continue;

		FString layout;
		if (isVulkan) layout.Format("set = 3, binding = %d", b.binding);

		auto withLayout = [&](const char* extra) -> FString
		{
			FString l = layout;
			if (extra && *extra)
			{
				if (l.Len()) l << ", ";
				l << extra;
			}
			FString r;
			if (l.Len()) r.Format("layout(%s) ", l.GetChars());
			return r;
		};

		switch (b.kind)
		{
		case RenderProgramBinding::UniformBlock:
		{
			// GL 3.3 has no layout(binding) for blocks: the backend calls glUniformBlockBinding after linking.
			FString type = b.blockTypeName.Len() ? b.blockTypeName : (FString(b.name) + "_t");
			out << withLayout("std140") << "uniform " << type << " " << b.blockBody;
			if (b.name && *b.name) out << " " << b.name;
			out << ";\n";
			break;
		}
		case RenderProgramBinding::StorageBuffer:
		{
			FString type = b.blockTypeName.Len() ? b.blockTypeName : (FString(b.name) + "_t");
			FString ssboLayout = "std430";
			if (!isVulkan) ssboLayout.AppendFormat(", binding = %d", (int)ProgramGLBufferBindingBase + b.binding);	// GL >= 4.3 only (needs430 above)
			out << withLayout(ssboLayout.GetChars()) << b.qualifiers << " buffer " << type << " " << b.blockBody;
			if (b.name && *b.name) out << " " << b.name;
			out << ";\n";
			break;
		}
		case RenderProgramBinding::Sampler2D:
			out << withLayout(nullptr) << "uniform sampler2D " << b.name << ";\n";
			break;
		case RenderProgramBinding::Sampler2DArray:
			out << withLayout(nullptr) << "uniform sampler2DArray " << b.name << ";\n";
			break;
		case RenderProgramBinding::USampler2D:
			out << withLayout(nullptr) << "uniform usampler2D " << b.name << ";\n";
			break;
		case RenderProgramBinding::USampler2DArray:
			out << withLayout(nullptr) << "uniform usampler2DArray " << b.name << ";\n";
			break;
		case RenderProgramBinding::SamplerBuffer:
			out << withLayout(nullptr) << "uniform samplerBuffer " << b.name << ";\n";
			break;
		case RenderProgramBinding::USamplerBuffer:
			out << withLayout(nullptr) << "uniform usamplerBuffer " << b.name << ";\n";
			break;
		case RenderProgramBinding::ISamplerBuffer:
			out << withLayout(nullptr) << "uniform isamplerBuffer " << b.name << ";\n";
			break;
		case RenderProgramBinding::Image2D:
		{
			FString extra;
			if (!isVulkan) extra.Format("binding = %d, ", b.binding);
			extra << (b.blockTypeName.Len() ? b.blockTypeName.GetChars() : "rgba8");
			FString l = layout;
			if (l.Len()) l << ", ";
			l << extra;
			out << "layout(" << l << ") " << b.qualifiers << " uniform image2D " << b.name << ";\n";
			break;
		}
		}
	}

	// Vertex inputs, varyings and fragment outputs
	if (!isCompute)
	{
		int inputIndex = 0, varyingIndex = 0, outputIndex = 0;
		for (auto& io : desc.inputsOutputs)
		{
			const char* type = GetTypeStr(io.type);
			switch (io.position)
			{
			case RenderProgramIO::VInput:
				if (isFrag) break;
				{
					int loc = io.location >= 0 ? io.location : inputIndex;
					inputIndex = loc + 1;
					out.AppendFormat("layout(location = %d) in %s %s;\n", loc, type, io.name.GetChars());
				}
				break;

			case RenderProgramIO::VOutput:
			{
				int loc = varyingIndex++;
				if (isVulkan) out.AppendFormat("layout(location = %d) ", loc);
				if (io.qualifier.Len()) out << io.qualifier << " ";
				out << (isFrag ? "in " : "out ") << type << " " << io.name << ";\n";
				break;
			}

			case RenderProgramIO::FOutput:
				if (!isFrag) break;
				{
					int loc = io.location >= 0 ? io.location : outputIndex;
					outputIndex = loc + 1;
					out.AppendFormat("layout(location = %d) out %s %s;\n", loc, type, io.name.GetChars());
				}
				break;
			}
		}
	}

	out << desc.defines;
	if (desc.defines.Len() && desc.defines[desc.defines.Len() - 1] != '\n') out << "\n";
	out << "#line 1\n";
	return out;
}
