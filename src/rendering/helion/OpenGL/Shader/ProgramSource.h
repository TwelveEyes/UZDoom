/*
** ProgramSource.h
**
** UZDoom-only. Lifts the interface declarations out of a Helion-style GLSL
** stage source.
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** Helion's programs are written as complete GLSL 3.30 sources with loose
** `uniform`, `in` and `out` declarations that the GL driver introspects. The
** render program API (hw_renderprogram.h) declares the same information
** explicitly (RenderProgramDesc) and generates the declarations itself, so
** RenderProgram runs every stage source through this parser: top-level
** `uniform` / `in` / `out` / `buffer` declarations and `#version` lines are
** removed from the source and returned as ProgramDecl records, everything
** else (functions, structs, constants, preprocessor lines) is kept verbatim.
** Preprocessor conditionals around lifted declarations are not evaluated, so
** program text must not put those declarations inside #if blocks.
**
*/

#pragma once

#include "tarray.h"
#include "zstring.h"

namespace Helion::Render::OpenGL::Shader
{

struct ProgramDecl
{
	enum class Kind
	{
		In,
		Out,
		Uniform,		// loose uniform (non-sampler)
		Sampler,
		UniformBlock,
		StorageBuffer,
	};

	static constexpr int UnsizedArray = -1;
	static constexpr int UnknownArraySize = -2;

	Kind kind = Kind::Uniform;
	FString type;			// GLSL type; the block type name for blocks
	FString name;			// variable name; the instance name for blocks (may be empty)
	FString interpolation;	// "flat", "noperspective" or empty
	FString qualifiers;		// "readonly", "writeonly", ...
	FString layout;			// text inside layout(...)
	int location = -1;
	int arrayCount = 0;		// 0 = not an array, > 0 = size, UnsizedArray, UnknownArraySize
	FString blockBody;		// "{ ... }" including the braces
};

struct ParsedSource
{
	FString source;			// the stage source without lifted declarations and #version
	TArray<ProgramDecl> decls;
};

ParsedSource ParseProgramSource(const FString& source);

// std140 size/alignment of a GLSL type (scalar, vector, matrix); false for anything else (structs, samplers).
bool Std140TypeLayout(const FString& type, int& alignment, int& size);

// One member of a uniform block body.
struct BlockMember
{
	FString type;
	FString name;
	int arrayCount = 0;
};

// Splits "{ type a; type b[4]; ... }" into members. False if the body contains anything else.
bool ParseBlockMembers(const FString& body, TArray<BlockMember>& members);

}
