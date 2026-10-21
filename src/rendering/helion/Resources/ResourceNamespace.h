/*
** ResourceNamespace.h
**
** Port of Helion.Resources.ResourceNamespace.
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** UZDoom-only note: Helion resolves images by (name, namespace). The renderer
** texture manager keeps the enum for its name-based lookups and for the
** namespace dependent clamp rules; FTextureID based lookups never need it.
**
*/

#pragma once

namespace Helion::Resources
{

/// <summary>
/// The different namespace locations for a resource.
/// </summary>
enum class ResourceNamespace
{
	Undefined,
	Global,
	ACS,
	Flats,
	Fonts,
	Graphics,
	Music,
	Sounds,
	Sprites,
	Textures,
	Colormaps,
	Brightmaps
};

}
