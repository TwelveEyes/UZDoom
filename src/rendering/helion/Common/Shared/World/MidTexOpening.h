/*
** MidTexOpening.h
**
** Port of Helion.Render.Common.Shared.World.MidTexOpening.
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

namespace Helion::Render::Common::Shared::World
{

struct MidTexOpening
{
	double BottomZ = 0;
	double TopZ = 0;
	double MinBottomZ = 0;
	double MaxTopZ = 0;
};

}
