/*
** ProgramRegistry.cpp
**
** UZDoom-only. The registry of the render programs compiled at startup (plan decision D22).
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

#include <map>
#include <tuple>
#include <vector>
#include "ProgramRegistry.h"
#include "v_video.h"
#include "engineerrors.h"
#include "OpenGL/Renderers/Legacy/World/Shader/ShaderVars.h"
#include "OpenGL/Renderers/Legacy/World/StaticShader.h"
#include "OpenGL/Renderers/Legacy/World/StaticWallClipShader.h"
#include "OpenGL/Renderers/Legacy/World/StaticWallClipAlphaShader.h"
#include "OpenGL/Renderers/Legacy/World/StaticPlaneClipShader.h"
#include "OpenGL/Renderers/Legacy/World/StaticPlaneClipAlphaShader.h"
#include "OpenGL/Renderers/Legacy/World/StaticPlaneClipShaderMrt.h"
#include "OpenGL/Renderers/Legacy/World/StaticTransparentShader.h"
#include "OpenGL/Renderers/Legacy/World/StaticCompositeShader.h"
#include "OpenGL/Renderers/Legacy/World/InterpolationShader.h"
#include "OpenGL/Renderers/Legacy/World/InterpolationWallClipShader.h"
#include "OpenGL/Renderers/Legacy/World/InterpolationWallClipAlphaShader.h"
#include "OpenGL/Renderers/Legacy/World/InterpolationPlaneClipShader.h"
#include "OpenGL/Renderers/Legacy/World/InterpolationPlaneClipAlphaShader.h"
#include "OpenGL/Renderers/Legacy/World/InterpolationPlaneClipShaderMrt.h"
#include "OpenGL/Renderers/Legacy/World/InterpolationTransparentShader.h"
#include "OpenGL/Renderers/Legacy/World/InterpolationCompositeShader.h"
#include "OpenGL/Renderers/Legacy/World/Geometry/Portals/FloodFill/FloodFillProgram.h"
#include "OpenGL/Renderers/Legacy/World/Geometry/Portals/PortalStencilProgram.h"
#include "OpenGL/Renderers/Legacy/World/Sky/Sphere/SkySphereShader.h"
#include "OpenGL/Renderers/Legacy/World/Sky/Sphere/SkySphereForegroundShader.h"
#include "OpenGL/Renderers/Legacy/World/Sky/Sphere/SkySphereFogShader.h"
#include "OpenGL/Renderers/Legacy/World/Sky/Sphere/SkySphereGeometryShader.h"
#include "OpenGL/Renderers/Legacy/World/Sky/SkyBox/SkyBoxShader.h"
#include "OpenGL/Renderers/Legacy/World/Entities/EntityProgram.h"
#include "OpenGL/Renderers/Legacy/World/Entities/ModelProgram.h"
#include "OpenGL/Renderers/Legacy/World/Entities/CoronaProgram.h"
#include "OpenGL/Renderers/Legacy/World/Geometry/DecalProgram.h"
#include "OpenGL/Renderers/Legacy/World/Primitives/PrimitiveShader.h"
#include "OpenGL/Renderers/FramebufferRenderer.h"
#include "OpenGL/Renderers/Legacy/World/Lights/ClusterAssignProgram.h"

namespace Helion::Render::OpenGL::Shader
{

using namespace Renderers::Legacy::World;
using Renderers::Legacy::World::Shader::MaterialProgramOptions;
using Renderers::Legacy::World::Shader::UzdoomContract;

namespace
{

using Factory = std::unique_ptr<RenderProgram>(*)(const MaterialProgramOptions&);

struct ProgramTypeInfo
{
	ProgramType Type;
	const char* Name;
	bool MaterialAware;		// one program per material shader and pass instead of one program
	Factory Create;
};

template<typename T>
std::unique_ptr<RenderProgram> CreateArray(const MaterialProgramOptions&)
{
	return RenderProgram::Create<T>();
}

std::unique_ptr<RenderProgram> CreateStaticArray(const MaterialProgramOptions&)
{
	return RenderProgram::Create<StaticShader>("Static");
}

std::unique_ptr<RenderProgram> CreateInterpolationArray(const MaterialProgramOptions&)
{
	return RenderProgram::Create<InterpolationShader>("Interpolation");
}

std::unique_ptr<RenderProgram> CreateFloodFill(const MaterialProgramOptions&)
{
	return RenderProgram::Create<Geometry::Portals::FloodFill::FloodFillProgram>("Main");
}

std::unique_ptr<RenderProgram> CreateStaticMaterial(const MaterialProgramOptions& material)
{
	return RenderProgram::Create<StaticShader>("Static", material);
}

std::unique_ptr<RenderProgram> CreateInterpolationMaterial(const MaterialProgramOptions& material)
{
	return RenderProgram::Create<InterpolationShader>("Interpolation", material);
}

std::unique_ptr<RenderProgram> CreateEntityMain(const MaterialProgramOptions&)
{
	return RenderProgram::Create<Entities::EntityProgram>("Main");
}

std::unique_ptr<RenderProgram> CreateEntityMaterial(const MaterialProgramOptions& material)
{
	return RenderProgram::Create<Entities::EntityProgram>("Main", material);
}

std::unique_ptr<RenderProgram> CreateModel(const MaterialProgramOptions& material)
{
	return RenderProgram::Create<Entities::ModelProgram>("Main", material);
}

const ProgramTypeInfo ProgramTypes[] =
{
	{ ProgramType::StaticArray, "StaticArray", false, CreateStaticArray },
	{ ProgramType::StaticWallClip, "StaticWallClip", false, CreateArray<StaticWallClipShader> },
	{ ProgramType::StaticWallClipAlpha, "StaticWallClipAlpha", false, CreateArray<StaticWallClipAlphaShader> },
	{ ProgramType::StaticPlaneClip, "StaticPlaneClip", false, CreateArray<StaticPlaneClipShader> },
	{ ProgramType::StaticPlaneClipAlpha, "StaticPlaneClipAlpha", false, CreateArray<StaticPlaneClipAlphaShader> },
	{ ProgramType::StaticPlaneClipMrt, "StaticPlaneClipMrt", false, CreateArray<StaticPlaneClipShaderMrt> },
	{ ProgramType::StaticTransparent, "StaticTransparent", false, CreateArray<StaticTransparentShader> },
	{ ProgramType::StaticComposite, "StaticComposite", false, CreateArray<StaticCompositeShader> },
	{ ProgramType::InterpolationArray, "InterpolationArray", false, CreateInterpolationArray },
	{ ProgramType::InterpolationWallClip, "InterpolationWallClip", false, CreateArray<InterpolationWallClipShader> },
	{ ProgramType::InterpolationWallClipAlpha, "InterpolationWallClipAlpha", false, CreateArray<InterpolationWallClipAlphaShader> },
	{ ProgramType::InterpolationPlaneClip, "InterpolationPlaneClip", false, CreateArray<InterpolationPlaneClipShader> },
	{ ProgramType::InterpolationPlaneClipAlpha, "InterpolationPlaneClipAlpha", false, CreateArray<InterpolationPlaneClipAlphaShader> },
	{ ProgramType::InterpolationPlaneClipMrt, "InterpolationPlaneClipMrt", false, CreateArray<InterpolationPlaneClipShaderMrt> },
	{ ProgramType::InterpolationTransparent, "InterpolationTransparent", false, CreateArray<InterpolationTransparentShader> },
	{ ProgramType::InterpolationComposite, "InterpolationComposite", false, CreateArray<InterpolationCompositeShader> },
	{ ProgramType::FloodFill, "FloodFill", false, CreateFloodFill },
	{ ProgramType::FloodFillWallClip, "FloodFillWallClip", false, CreateArray<Geometry::Portals::FloodFill::FloodFillWallClipProgram> },
	{ ProgramType::PortalStencil, "PortalStencil", false, CreateArray<Geometry::Portals::PortalStencilProgram> },
	{ ProgramType::SkySphere, "SkySphere", false, CreateArray<Sky::Sphere::SkySphereShader> },
	{ ProgramType::SkySphereForeground, "SkySphereForeground", false, CreateArray<Sky::Sphere::SkySphereForegroundShader> },
	{ ProgramType::SkySphereFog, "SkySphereFog", false, CreateArray<Sky::Sphere::SkySphereFogShader> },
	{ ProgramType::SkySphereGeometry, "SkySphereGeometry", false, CreateArray<Sky::Sphere::SkySphereGeometryShader> },
	{ ProgramType::SkyBox, "SkyBox", false, CreateArray<Sky::SkyBox::SkyBoxShader> },
	{ ProgramType::EntityMain, "EntityMain", false, CreateEntityMain },
	{ ProgramType::EntityTransparent, "EntityTransparent", false, CreateArray<Entities::EntityTransparentProgram> },
	{ ProgramType::EntityComposite, "EntityComposite", false, CreateArray<Entities::EntityCompositeProgram> },
	{ ProgramType::EntityFuzzRefraction, "EntityFuzzRefraction", false, CreateArray<Entities::EntityFuzzRefractionProgram> },
	{ ProgramType::Decal, "Decal", false, CreateArray<Geometry::DecalProgram> },
	{ ProgramType::Corona, "Corona", false, CreateArray<Entities::CoronaProgram> },
	{ ProgramType::Primitive, "Primitive", false, CreateArray<Primitives::PrimitiveShader> },
	{ ProgramType::Framebuffer, "Framebuffer", false, CreateArray<Renderers::FramebufferProgram> },
	{ ProgramType::StaticMaterial, "StaticMaterial", true, CreateStaticMaterial },
	{ ProgramType::InterpolationMaterial, "InterpolationMaterial", true, CreateInterpolationMaterial },
	{ ProgramType::EntityMaterial, "EntityMaterial", true, CreateEntityMaterial },
	{ ProgramType::Model, "Model", true, CreateModel },
};

static_assert(sizeof(ProgramTypes) / sizeof(ProgramTypes[0]) == (size_t)ProgramType::Count, "ProgramTypes has to list every ProgramType in order");

struct Key
{
	ProgramType Type;
	int MaterialShaderIndex;
	EPassType Pass;
	int Flags;

	bool operator<(const Key& other) const
	{
		return std::tie(Type, MaterialShaderIndex, Pass, Flags) < std::tie(other.Type, other.MaterialShaderIndex, other.Pass, other.Flags);
	}
};

std::vector<Key> s_keys;
bool s_keysBuilt = false;
size_t s_compileIndex = 0;
std::map<Key, std::unique_ptr<RenderProgram>> s_programs;

// The compile sequence: ProgramType x material shader index x EPassType. Array types are a single entry.
void BuildKeys()
{
	if (s_keysBuilt)
		return;

	s_keys.clear();
	int materialCount = UzdoomContract::MaterialShaderCount();
	for (const ProgramTypeInfo& info : ProgramTypes)
	{
		if (!info.MaterialAware)
		{
			s_keys.push_back({ info.Type, MaterialProgramOptions::NoMaterial, NORMAL_PASS, 0 });
			continue;
		}

		for (int material = 0; material < materialCount; material++)
		{
			for (int pass = 0; pass < MAX_PASS_TYPES; pass++)
				s_keys.push_back({ info.Type, material, (EPassType)pass, 0 });
		}
	}
	s_keysBuilt = true;
}

Key Normalize(ProgramType type, int materialShaderIndex, EPassType pass, int flags)
{
	if (!ProgramTypes[(int)type].MaterialAware)
		return { type, MaterialProgramOptions::NoMaterial, NORMAL_PASS, flags };

	return { type, materialShaderIndex, pass, flags };
}

}

int HelionProgramRegistry::Count()
{
	BuildKeys();
	return (int)s_keys.size();
}

bool HelionProgramRegistry::Compiled()
{
	return s_keysBuilt && s_compileIndex >= s_keys.size();
}

bool HelionProgramRegistry::CompileNext()
{
	BuildKeys();
	if (s_compileIndex >= s_keys.size())
		return true;

	if (s_compileIndex == 0)
	{
		Renderers::Legacy::World::Shader::ShaderVars::Initialize();
		// The light -> cluster assignment (plan M6.1); not a program x material x pass combination.
		Renderers::Legacy::World::Lights::ClusterAssignProgram::CreateShared();
	}

	const Key& key = s_keys[s_compileIndex];
	const ProgramTypeInfo& info = ProgramTypes[(int)key.Type];

	MaterialProgramOptions material;
	material.MaterialShaderIndex = key.MaterialShaderIndex;
	material.Pass = key.Pass;
	s_programs[key] = info.Create(material);

	s_compileIndex++;
	return s_compileIndex >= s_keys.size();
}

RenderProgram* HelionProgramRegistry::Get(ProgramType type, int materialShaderIndex, EPassType pass, int flags)
{
	Key key = Normalize(type, materialShaderIndex, pass, flags);
	auto it = s_programs.find(key);
	if (it == s_programs.end())
	{
		I_Error("Render program %s (material shader %d, pass %d, flags %d) was not compiled at startup", ProgramTypes[(int)type].Name, key.MaterialShaderIndex, (int)key.Pass, key.Flags);
		return nullptr;
	}

	return it->second.get();
}

void HelionProgramRegistry::Reset()
{
	s_programs.clear();
	Renderers::Legacy::World::Lights::ClusterAssignProgram::ReleaseShared();
	s_keys.clear();
	s_keysBuilt = false;
	s_compileIndex = 0;
}

}

void HelionProgramRegistry_Register()
{
	DFrameBuffer::ExtraShaderCount = &Helion::Render::OpenGL::Shader::HelionProgramRegistry::Count;
	DFrameBuffer::CompileNextExtraShader = &Helion::Render::OpenGL::Shader::HelionProgramRegistry::CompileNext;
}
