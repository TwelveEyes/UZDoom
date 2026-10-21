/*
** RenderProgram.cpp
**
** Port of Helion.Render.OpenGL.Shader.RenderProgram.
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

#include <algorithm>
#include <limits>
#include "RenderProgram.h"
#include "ProgramSource.h"
#include "TextureUnits.h"
#include "OpenGL/Vertex/Attributes.h"
#include "hwrenderer/data/hw_renderstate.h"
#include "v_video.h"
#include "textures.h"
#include "engineerrors.h"
#include "printf.h"

namespace Helion::Render::OpenGL::Shader
{

namespace
{

bool GlslToUniformType(const FString& type, UniformType& result)
{
	struct Entry { const char* name; UniformType type; };
	static const Entry entries[] =
	{
		{ "int", UniformType::Int }, { "uint", UniformType::UInt }, { "float", UniformType::Float },
		{ "vec2", UniformType::Vec2 }, { "vec3", UniformType::Vec3 }, { "vec4", UniformType::Vec4 },
		{ "ivec2", UniformType::IVec2 }, { "ivec3", UniformType::IVec3 }, { "ivec4", UniformType::IVec4 },
		{ "uvec2", UniformType::UVec2 }, { "uvec3", UniformType::UVec3 }, { "uvec4", UniformType::UVec4 },
		{ "mat4", UniformType::Mat4 },
	};

	for (const Entry& entry : entries)
	{
		if (type.Compare(entry.name) == 0)
		{
			result = entry.type;
			return true;
		}
	}
	return false;
}

bool SamplerKind(const FString& type, RenderProgramBinding::Kind& kind)
{
	struct Entry { const char* name; RenderProgramBinding::Kind kind; };
	static const Entry entries[] =
	{
		{ "sampler2D", RenderProgramBinding::Sampler2D },
		{ "sampler2DArray", RenderProgramBinding::Sampler2DArray },
		{ "usampler2D", RenderProgramBinding::USampler2D },
		{ "usampler2DArray", RenderProgramBinding::USampler2DArray },
		{ "samplerBuffer", RenderProgramBinding::SamplerBuffer },
		{ "usamplerBuffer", RenderProgramBinding::USamplerBuffer },
		{ "isamplerBuffer", RenderProgramBinding::ISamplerBuffer },
	};

	for (const Entry& entry : entries)
	{
		if (type.Compare(entry.name) == 0)
		{
			kind = entry.kind;
			return true;
		}
	}
	return false;
}

// A loose uniform merged over both stages.
struct LooseUniform
{
	FString type;
	FString name;
	int arrayCount = 0;
	int offset = 0;
};

struct BindingInfo
{
	RenderProgramBinding binding;
	int block = -1;		// index into ProgramUniforms blocks, -1 for external buffers and samplers
};

// std140 offset of the next member. Returns false for types the layout code does not know.
bool PlaceMember(const FString& type, int arrayCount, int& offset, int& memberOffset)
{
	int alignment, size;
	if (!Std140TypeLayout(type, alignment, size))
		return false;

	int total = size;
	if (arrayCount > 0)
	{
		// std140: array elements are rounded up to the size of a vec4
		alignment = 16;
		total = ProgramUniforms::AlignUp(size, 16) * arrayCount;
	}
	else if (arrayCount != 0)
	{
		return false;
	}

	offset = ProgramUniforms::AlignUp(offset, alignment);
	memberOffset = offset;
	offset += total;
	return true;
}

}

RenderProgram::RenderProgram(const FString& label) : Label(label)
{
}

RenderProgram::~RenderProgram()
{
	if (s_current == this)
	{
		s_current = nullptr;
		ProgramUniforms::SetCurrent(nullptr);
	}
}

const char* RenderProgram::StoreName(const FString& name)
{
	m_names.push_back(name);
	return m_names.back().GetChars();
}

void RenderProgram::Initialize()
{
	FString vertexSource = VertexShader();
	std::optional<FString> fragmentSource = FragmentShader();

	ParsedSource vertex = ParseProgramSource(vertexSource);
	ParsedSource fragment = ParseProgramSource(fragmentSource ? *fragmentSource : FString());

	TArray<LooseUniform> loose;
	TArray<bool> looseVertex, looseFragment;
	TArray<BindingInfo> infos;
	TArray<bool> infoVertex, infoFragment;

	auto findLoose = [&](const FString& name) -> int {
		for (unsigned i = 0; i < loose.Size(); i++)
			if (loose[i].name.Compare(name) == 0) return (int)i;
		return -1;
	};
	auto findInfo = [&](const FString& name, bool sampler) -> int {
		for (unsigned i = 0; i < infos.Size(); i++)
		{
			const RenderProgramBinding& b = infos[i].binding;
			bool isSampler = b.kind != RenderProgramBinding::UniformBlock && b.kind != RenderProgramBinding::StorageBuffer;
			if (isSampler != sampler)
				continue;
			if (sampler ? name.Compare(b.name) == 0 : name.Compare(b.blockTypeName) == 0)
				return (int)i;
		}
		return -1;
	};

	TArray<RenderProgramIO> varyings;
	TArray<RenderProgramIO> vertexInputs;
	TArray<RenderProgramIO> fragmentOutputs;

	for (int stage = 0; stage < 2; stage++)
	{
		const ParsedSource& parsed = stage == 0 ? vertex : fragment;
		bool isVertex = stage == 0;

		for (const ProgramDecl& decl : parsed.decls)
		{
			switch (decl.kind)
			{
			case ProgramDecl::Kind::In:
			case ProgramDecl::Kind::Out:
			{
				UniformType type;
				if (!GlslToUniformType(decl.type, type))
					I_FatalError("Render program %s: unsupported interface type '%s' of '%s'", Label.GetChars(), decl.type.GetChars(), decl.name.GetChars());

				RenderProgramIO io;
				io.type = type;
				io.name = decl.name;
				io.location = decl.location;
				io.qualifier = decl.interpolation;

				bool isInput = decl.kind == ProgramDecl::Kind::In;
				if (isVertex && isInput)
				{
					io.position = RenderProgramIO::VInput;
					vertexInputs.Push(io);
				}
				else if (isVertex)
				{
					io.position = RenderProgramIO::VOutput;
					io.location = -1;
					varyings.Push(io);
				}
				else if (isInput)
				{
					// The varyings are taken from the vertex outputs (the prelude emits them as the fragment inputs), so a
					// fragment input declaration carries no information. Like in GL a fragment input the vertex stage does
					// not write is only an error when the fragment code uses it, which then fails to compile.
				}
				else
				{
					io.position = RenderProgramIO::FOutput;
					fragmentOutputs.Push(io);
				}
				break;
			}

			case ProgramDecl::Kind::Uniform:
			{
				int index = findLoose(decl.name);
				if (index < 0)
				{
					LooseUniform uniform;
					uniform.type = decl.type;
					uniform.name = decl.name;
					uniform.arrayCount = decl.arrayCount;
					index = (int)loose.Push(uniform);
					looseVertex.Push(false);
					looseFragment.Push(false);
				}
				(isVertex ? looseVertex : looseFragment)[index] = true;
				break;
			}

			case ProgramDecl::Kind::Sampler:
			case ProgramDecl::Kind::UniformBlock:
			case ProgramDecl::Kind::StorageBuffer:
			{
				bool sampler = decl.kind == ProgramDecl::Kind::Sampler;
				int index = findInfo(sampler ? decl.name : decl.type, sampler);
				if (index < 0)
				{
					BindingInfo info;
					RenderProgramBinding& b = info.binding;
					b.name = nullptr;
					b.binding = -1;
					b.vertex = b.fragment = b.compute = false;
					if (sampler)
					{
						if (!SamplerKind(decl.type, b.kind))
							I_FatalError("Render program %s: unsupported sampler type '%s'", Label.GetChars(), decl.type.GetChars());
						b.name = StoreName(decl.name);
					}
					else
					{
						b.kind = decl.kind == ProgramDecl::Kind::UniformBlock ? RenderProgramBinding::UniformBlock : RenderProgramBinding::StorageBuffer;
						b.name = StoreName(decl.name);	// instance name, empty = members are global
						b.blockTypeName = decl.type;
						b.blockBody = decl.blockBody;
						b.qualifiers = decl.qualifiers;
					}
					index = (int)infos.Push(info);
					infoVertex.Push(false);
					infoFragment.Push(false);
				}
				(isVertex ? infoVertex : infoFragment)[index] = true;
				break;
			}
			}
		}
	}

	m_desc = RenderProgramDesc();
	m_desc.name = Label;
	m_desc.vertexSource = vertex.source;
	m_desc.fragmentSource = fragment.source;
	// UzdoomContract::ShadowDeclarations: the light shadows are traced against the TopLevelAS of the backend.
	m_desc.rayQuery = fragment.source.IndexOf("#define SUPPORTS_RAYTRACING") >= 0;

	// Binding 0 is the block holding the program's loose uniforms (Helion's glUniform* targets).
	int nextBinding = 0;
	int looseBlockBinding = -1;
	int looseBlockSize = 0;
	if (loose.Size() > 0)
	{
		FString body = "{\n";
		int offset = 0;
		for (LooseUniform& uniform : loose)
		{
			int memberOffset;
			if (!PlaceMember(uniform.type, uniform.arrayCount, offset, memberOffset))
				I_FatalError("Render program %s: uniform '%s' of type '%s' cannot be placed in a uniform block", Label.GetChars(), uniform.name.GetChars(), uniform.type.GetChars());
			uniform.offset = memberOffset;
			body.AppendFormat("\t%s %s", uniform.type.GetChars(), uniform.name.GetChars());
			if (uniform.arrayCount > 0)
				body.AppendFormat("[%d]", uniform.arrayCount);
			body << ";\n";
		}
		body << "}";
		looseBlockSize = ProgramUniforms::AlignUp(offset, 16);
		looseBlockBinding = nextBinding++;

		RenderProgramBinding b;
		b.name = StoreName("");
		b.kind = RenderProgramBinding::UniformBlock;
		b.binding = looseBlockBinding;
		b.vertex = b.fragment = b.compute = false;
		for (unsigned i = 0; i < loose.Size(); i++)
		{
			b.vertex |= looseVertex[i];
			b.fragment |= looseFragment[i];
		}
		b.blockTypeName = "HelionUniforms";
		b.blockBody = body;
		m_desc.bindings.Push(b);

		int block = Uniforms.AddBlock("HelionUniforms", looseBlockBinding, looseBlockSize);
		for (const LooseUniform& uniform : loose)
		{
			ProgramUniforms::Uniform u;
			u.Name = uniform.name;
			u.Type = uniform.type;
			u.Block = block;
			u.Offset = uniform.offset;
			u.ArrayCount = uniform.arrayCount;
			Uniforms.AddUniform(u);
		}
	}

	// Storage buffers get the lowest binding numbers: on OpenGL their binding point is ProgramGLBufferBindingBase + binding,
	// and GL_MAX_SHADER_STORAGE_BUFFER_BINDINGS is much smaller than the uniform block and texture unit limits.
	TArray<unsigned> order;
	for (int pass = 0; pass < 2; pass++)
	{
		for (unsigned i = 0; i < infos.Size(); i++)
		{
			if ((infos[i].binding.kind == RenderProgramBinding::StorageBuffer) == (pass == 0))
				order.Push(i);
		}
	}

	for (unsigned i : order)
	{
		RenderProgramBinding b = infos[i].binding;
		b.binding = nextBinding++;
		b.vertex = infoVertex[i];
		b.fragment = infoFragment[i];

		bool isBlock = b.kind == RenderProgramBinding::UniformBlock || b.kind == RenderProgramBinding::StorageBuffer;
		if (!isBlock)
		{
			ProgramUniforms::Uniform u;
			u.Name = b.name;
			u.Type = FString();
			u.Binding = b.binding;
			Uniforms.AddUniform(u);
		}
		else if (b.kind == RenderProgramBinding::UniformBlock)
		{
			// Member layout of lifted uniform blocks, so Uniforms.Set / SetBlock work on them (StreamData, ViewpointUBO).
			// Blocks whose type name contains "Buffer" (LightBufferUBO) and blocks with members the layout code cannot
			// place are buffer-backed: the renderer binds them with FRenderState::SetProgramBuffer.
			TArray<BlockMember> members;
			bool bufferBacked = b.blockTypeName.IndexOf("Buffer") >= 0;
			bool placeable = !bufferBacked && ParseBlockMembers(b.blockBody, members);
			int offset = 0;
			TArray<int> offsets;
			for (const BlockMember& member : members)
			{
				int memberOffset = 0;
				placeable = placeable && PlaceMember(member.type, member.arrayCount, offset, memberOffset);
				offsets.Push(memberOffset);
			}

			if (placeable)
			{
				FString blockName = b.blockTypeName;
				int block = Uniforms.AddBlock(blockName, b.binding, ProgramUniforms::AlignUp(offset, 16));
				for (unsigned m = 0; m < members.Size(); m++)
				{
					ProgramUniforms::Uniform u;
					u.Name = (b.name && *b.name) ? FString(b.name) + "." + members[m].name : members[m].name;
					u.Type = members[m].type;
					u.Block = block;
					u.Offset = offsets[m];
					u.ArrayCount = members[m].arrayCount;
					Uniforms.AddUniform(u);
				}
			}
		}

		m_desc.bindings.Push(b);
	}

	if (nextBinding > MaxProgramBindings)
		I_FatalError("Render program %s uses %d bindings, the limit is %d", Label.GetChars(), nextBinding, MaxProgramBindings);

	for (const RenderProgramIO& io : vertexInputs) m_desc.inputsOutputs.Push(io);
	for (const RenderProgramIO& io : varyings) m_desc.inputsOutputs.Push(io);

	int nextOutput = 0;
	int outputCount = 0;
	for (RenderProgramIO io : fragmentOutputs)
	{
		if (io.location < 0)
			io.location = nextOutput;
		nextOutput = io.location + 1;
		outputCount = std::max(outputCount, nextOutput);
		m_desc.inputsOutputs.Push(io);
	}
	m_desc.numColorOutputs = std::max(outputCount, 1);

	if (const TArray<Vertex::VaoAttribute>* layout = VertexLayout())
	{
		bool instanced = false;
		for (const Vertex::VaoAttribute& attr : *layout)
			instanced |= attr.Divisor > 0;

		m_desc.vertexAttributes = Vertex::Attributes::ToBackendAttributes(*layout);
		if (layout->Size() > 0)
			m_desc.vertexStride[instanced ? 1 : 0] = (size_t)(*layout)[0].Stride;
		m_desc.numBindingPoints = instanced ? 2 : 1;
		m_desc.instanceDivisorBinding = instanced ? 1 : -1;
	}

	Attributes.Populate(m_desc.inputsOutputs);

	// Helion throws on compile/link errors; the backend reports them with I_FatalError("Render program %s: ...").
	m_program.reset(CreateBackendProgram(m_desc));
	if (m_program == nullptr)
		I_FatalError("Render program %s: the video backend does not support render programs", Label.GetChars());
	if (!m_program->IsValid())
		I_FatalError("Render program %s: error creating the program", Label.GetChars());

	m_samplerVersions.Resize(Uniforms.Uniforms().Size());
	for (unsigned& version : m_samplerVersions)
		version = 0;
	m_clipPlaneLocation = Uniforms.GetLocation(ClipPlaneUniform);

	ProgramCreated();
}

IRenderProgram* RenderProgram::CreateBackendProgram(const RenderProgramDesc& desc)
{
	return screen->CreateRenderProgram(desc);
}

int RenderProgram::GetBinding(const FString& name) const
{
	for (const RenderProgramBinding& b : m_desc.bindings)
	{
		if (name.Compare(b.name) == 0 || name.Compare(b.blockTypeName) == 0)
			return b.binding;
	}
	return -1;
}

void RenderProgram::Bind()
{
	FRenderState* state = screen->RenderState();
	state->SetProgram(m_program.get());

	s_current = this;
	ProgramUniforms::SetCurrent(&Uniforms);

	// The backend slots may hold the previous program's data: resend everything.
	Uniforms.MarkAllDirty();
	for (unsigned& version : m_samplerVersions)
		version = std::numeric_limits<unsigned>::max();

	OnBound();
}

void RenderProgram::Unbind()
{
	FRenderState* state = screen->RenderState();
	state->SetProgram(nullptr);
	SetClipDistanceEnabled(*state, false);
	if (s_current == this)
	{
		s_current = nullptr;
		ProgramUniforms::SetCurrent(nullptr);
	}
}

void RenderProgram::SetClipDistanceEnabled(FRenderState& state, bool enabled)
{
	// GL: gl_ClipDistance[0] is only enabled for the programs that write it (undefined otherwise). Vulkan ignores the call.
	if (enabled != s_clipDistanceEnabled)
	{
		state.EnableClipDistance(0, enabled);
		s_clipDistanceEnabled = enabled;
	}
}

void RenderProgram::FlushState()
{
	FRenderState& state = *screen->RenderState();
	if (m_clipPlaneLocation >= 0)
		Uniforms.SetAt(m_clipPlaneLocation, s_clipPlane);
	SetClipDistanceEnabled(state, m_clipPlaneLocation >= 0 && s_clipPlaneActive);
	Uniforms.Flush(state);

	const TArray<ProgramUniforms::Uniform>& uniforms = Uniforms.Uniforms();
	for (unsigned i = 0; i < uniforms.Size(); i++)
	{
		const ProgramUniforms::Uniform& sampler = uniforms[i];
		if (sampler.Binding < 0 || sampler.Unit < 0)
			continue;

		const TextureUnits::Entry& entry = TextureUnits::Get((TextureUnit)sampler.Unit);
		if (m_samplerVersions[i] == entry.version)
			continue;

		m_samplerVersions[i] = entry.version;
		switch (entry.kind)
		{
		case TextureUnits::Kind::Texture:
			state.SetProgramTexture(sampler.Binding, entry.texture, entry.clampmode, entry.filter);
			break;
		case TextureUnits::Kind::TexelBuffer:
			state.SetProgramTexelBuffer(sampler.Binding, entry.buffer);
			break;
		case TextureUnits::Kind::RenderTarget:
			state.SetProgramRenderTargetTexture(sampler.Binding, entry.target, entry.attachment);
			break;
		case TextureUnits::Kind::SceneColor:
			state.SetProgramSceneTexture(sampler.Binding, false);
			break;
		case TextureUnits::Kind::SceneDepth:
			state.SetProgramSceneTexture(sampler.Binding, true);
			break;
		case TextureUnits::Kind::None:
			// Unbind: like GL.BindTexture(target, 0) in Helion the sampler must read black, not the texture of an earlier draw.
			state.SetProgramTexture(sampler.Binding, nullptr, CLAMP_NONE);
			break;
		}
	}
}

void RenderProgram::FlushCurrent()
{
	if (s_current != nullptr)
		s_current->FlushState();
}

}
