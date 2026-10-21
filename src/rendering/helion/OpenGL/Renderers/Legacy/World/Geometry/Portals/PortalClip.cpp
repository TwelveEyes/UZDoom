/*
** PortalClip.cpp
**
** UZDoom-only (plan M5.2), see PortalClip.h.
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

#include "PortalClip.h"
#include <algorithm>
#include <cmath>

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Geometry::Portals
{

namespace
{

constexpr uint64_t FullCircleBam = 0x100000000ull;
constexpr uint64_t QuarterCircleBam = 0x40000000ull;

// The diamond angle (ViewClipper::ToDiamondAngle) of the map direction of a BAM angle.
uint32_t BamToDiamond(uint64_t bam)
{
	if (bam >= FullCircleBam)
		return UINT32_MAX;
	double radians = (double)bam * (2.0 * 3.14159265358979323846 / 4294967296.0);
	return Shared::World::ViewClipping::ViewClipper::ToDiamondAngle(0, 0, std::cos(radians), std::sin(radians));
}

}

void PortalClip::AddBlockedBam(uint64_t fromBam, uint64_t toBam)
{
	if (toBam > FullCircleBam)
		toBam = FullCircleBam;
	if (fromBam >= toBam)
		return;

	while (fromBam < toBam)
	{
		uint64_t end = std::min(toBam, fromBam + QuarterCircleBam);
		uint32_t first = BamToDiamond(fromBam);
		uint32_t second = BamToDiamond(end);
		// The conversion is monotonic; an end point at the full circle maps to the largest diamond angle and a chunk that ends on
		// a quadrant border may convert to the first angle of the next quadrant, never below its own start.
		if (second < first)
			second = first;
		if (second > first)
			BlockedAngles.emplace_back(first, second);
		fromBam = end;
	}
}

}
