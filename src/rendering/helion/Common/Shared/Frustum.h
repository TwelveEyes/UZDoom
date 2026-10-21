/*
** Frustum.h
**
** Port of Helion.Render.Common.Shared.Frustum.
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

#include "Common/Shared/FrustumPlane.h"

namespace Helion::Render::Common::Shared
{

class Frustum
{
public:
	// mvp is indexed linearly (column-major), exactly like GlmSharp's mat4 indexer.
	static void SetFrustumPlanes(const Geometry::mat4& mvp, FrustumPlanes& planes);
};

}
