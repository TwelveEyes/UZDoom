/*
** LumpText.cpp
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

#include "LumpText.h"
#include "filesystem.h"
#include "cmdlib.h"
#include "engineerrors.h"
#include "stb_include.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Shader
{

FString LumpText::Get(const char* fullName, bool coreOnly, bool* found)
{
	int lump = coreOnly ? fileSystem.CheckNumForFullName(fullName, 0) : fileSystem.CheckNumForFullName(fullName);
	if (found != nullptr)
		*found = lump != -1;
	if (lump == -1)
		return FString();

	return GetStringFromLump(lump);
}

FString LumpText::Require(const char* fullName, bool coreOnly)
{
	bool found;
	FString text = Get(fullName, coreOnly, &found);
	if (!found)
		I_Error("Unable to load '%s'", fullName);
	return text;
}

FString LumpText::Include(const FString& text, const char* lumpName, TArray<FString>& filenamesForError)
{
	FString error;
	FString result = stb_include_string(text, lumpName, filenamesForError, error);
	if (!error.IsEmpty())
		I_Error("Unable to load '%s': %s", lumpName, error.GetChars());
	return result;
}

}
