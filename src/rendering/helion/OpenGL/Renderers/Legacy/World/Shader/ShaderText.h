/*
** ShaderText.h
**
** UZDoom-only. String helpers for the generated GLSL text (C# string.Replace / interpolation).
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

// C# string.Replace(what, with): returns the text with every occurrence replaced.
inline FString Replace(FString text, const char* what, const FString& with)
{
	text.Substitute(what, with.GetChars());
	return text;
}

inline FString Replace(FString text, const char* what, const char* with)
{
	text.Substitute(what, with);
	return text;
}

}
