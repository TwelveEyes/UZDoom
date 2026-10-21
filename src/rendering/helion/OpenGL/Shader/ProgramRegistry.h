/*
** ProgramRegistry.h
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

#pragma once

#include <functional>
#include <memory>
#include "RenderProgram.h"
#include "OpenGL/Renderers/Legacy/World/Shader/UzdoomContract.h"

namespace Helion::Render::OpenGL::Shader
{

// Every program class the renderer draws with. The registry creates one instance of each program x material
// shader x pass combination at startup through DFrameBuffer's extra shader hook (so a broken mod shader fails with
// the same startup error and the same progress bar as the table shaders). Add a type here and to the table in
// ProgramRegistry.cpp when a renderer file gets a new RenderProgram class.
enum class ProgramType
{
	// Helion's array texture programs (one per class; material index and pass are not part of the key).
	StaticArray,
	StaticWallClip,
	StaticWallClipAlpha,
	StaticPlaneClip,
	StaticPlaneClipAlpha,
	StaticPlaneClipMrt,
	StaticTransparent,
	StaticComposite,
	InterpolationArray,
	InterpolationWallClip,
	InterpolationWallClipAlpha,
	InterpolationPlaneClip,
	InterpolationPlaneClipAlpha,
	InterpolationPlaneClipMrt,
	InterpolationTransparent,
	InterpolationComposite,

	// Portal and sky programs.
	FloodFill,
	FloodFillWallClip,
	PortalStencil,
	SkySphere,
	SkySphereForeground,
	SkySphereFog,
	SkySphereGeometry,
	SkyBox,

	// Entity, decal, corona, primitive and framebuffer programs.
	EntityMain,
	EntityTransparent,
	EntityComposite,
	EntityFuzzRefraction,
	Decal,
	Corona,
	Primitive,
	Framebuffer,

	// Per material shader programs (mod shader contract): one per entry of the material shader table x pass.
	StaticMaterial,
	InterpolationMaterial,
	EntityMaterial,
	Model,
	Count
};

class HelionProgramRegistry
{
public:
	// Number of compile steps (DFrameBuffer::ExtraShaderCount).
	static int Count();

	// Compiles the next program (DFrameBuffer::CompileNextExtraShader). Returns true when everything is compiled.
	static bool CompileNext();

	// The program of the key. materialShaderIndex / pass are only part of the key of the material types.
	// Fails with I_Error when the combination was not compiled at startup (there is no lazy compilation).
	static RenderProgram* Get(ProgramType type, int materialShaderIndex = Renderers::Legacy::World::Shader::MaterialProgramOptions::NoMaterial,
		EPassType pass = NORMAL_PASS, int flags = 0);

	template<typename T>
	static T* Get(ProgramType type, int materialShaderIndex = Renderers::Legacy::World::Shader::MaterialProgramOptions::NoMaterial,
		EPassType pass = NORMAL_PASS, int flags = 0)
	{
		return static_cast<T*>(Get(type, materialShaderIndex, pass, flags));
	}

	// Destroys every program and restarts the compile sequence (color mode change, renderer shutdown).
	static void Reset();

	static bool Compiled();
};

}

// Sets DFrameBuffer::ExtraShaderCount / CompileNextExtraShader. Call once at startup, before the video backend is created.
void HelionProgramRegistry_Register();
