/*
** IBspHeuristics.h
**
** Port of Helion.Render.OpenGL.Renderers.Legacy.World.IBspHeuristics.
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

#include <cstdint>

namespace Helion::Render::OpenGL::Renderers::Legacy::World
{

struct BspHeuristicInfo
{
	bool UseBsp = false;
	int GameTick = 0;
	int SmoothTime = 0;
	int AboveThresholdCount = 0;
	int BelowThresholdCount = 0;
	int SegCount = 0;
};

class IBspHeuristics
{
public:
	virtual ~IBspHeuristics() = default;

	virtual bool Valid() const = 0;
	virtual int SubsectorCount() const = 0;
	virtual int SegCount() const = 0;
	virtual int LineCount() const = 0;
	virtual int LastProcessedId() const = 0;
	virtual int Microseconds() const = 0;
	virtual int64_t LastProcessedTimeStamp() const = 0;
	virtual BspHeuristicInfo& Info() = 0;
};

}
