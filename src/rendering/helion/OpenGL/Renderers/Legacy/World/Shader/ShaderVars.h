/*
** ShaderVars.h
**
** Port of Helion.Render.OpenGL.Renderers.Legacy.World.Shader.ShaderVars.
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

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Shader
{

// Shader generation switches. Like in Helion they are read while the program sources are generated, so
// Initialize() has to run before any program is created (and a change needs the programs to be recreated).
// ReversedZ and Depth are not ported (plan D11): depth is always gl_Position.z (conventional projection).
class ShaderVars
{
public:
	inline static bool PaletteColorMode = true;
	inline static bool SoftwareSpriteEmulation = false;
	inline static bool EmulateInvulnerabilityColorMap = false;

	// Reads vid_colormode, gl_vanillarender and gl_emulateinvulncolormap
	// (Helion Renderer.SetShaderVars).
	static void Initialize();

	// UZDoom-only: true when one of those CVars no longer matches the values the programs were generated with
	// (vid_colormode switched at runtime): the renderer and the programs have to be rebuilt.
	static bool Changed();
};

// UZDoom-only. Per material shader programs are always truecolor (an index texture cannot go through
// a custom ProcessTexel), so their sources are generated with PaletteColorMode forced off.
class PaletteColorModeScope
{
public:
	explicit PaletteColorModeScope(bool value) : m_previous(ShaderVars::PaletteColorMode) { ShaderVars::PaletteColorMode = value; }
	~PaletteColorModeScope() { ShaderVars::PaletteColorMode = m_previous; }

	PaletteColorModeScope(const PaletteColorModeScope&) = delete;
	PaletteColorModeScope& operator=(const PaletteColorModeScope&) = delete;

private:
	bool m_previous;
};

}
