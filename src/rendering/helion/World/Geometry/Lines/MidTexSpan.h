/*
** MidTexSpan.h
**
** Port of Helion.World.Geometry.Lines.MidTexSpan.
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

namespace Helion::World::Geometry::Lines
{

struct MidTexSpan
{
	double BottomZ;
	double TopZ;
	double PrevBottomZ;
	double PrevTopZ;

	MidTexSpan(double bottom, double top, double prevBottom, double prevTop) :
		BottomZ(bottom),
		TopZ(top),
		PrevBottomZ(prevBottom),
		PrevTopZ(prevTop)
	{
	}
};

}
