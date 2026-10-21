/*
** LumpText.h
**
** UZDoom-only. Access to the GLSL lumps of the core resource file.
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

#include "zstring.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Shader
{

class LumpText
{
public:
	// The text of a shader lump (zero terminated like GetStringFromLump), or an empty string with Found == false.
	// coreOnly: ignore resource overrides by user mods (what FShader::Load does for the engine's own lumps).
	static FString Get(const char* fullName, bool coreOnly, bool* found = nullptr);

	// As Get but fails with I_Error ("Unable to load '%s'") when the lump does not exist.
	static FString Require(const char* fullName, bool coreOnly = true);

	// Resolves #include like the table shaders do for mod shaders (stb_include_string).
	static FString Include(const FString& text, const char* lumpName, TArray<FString>& filenamesForError);
};

}
