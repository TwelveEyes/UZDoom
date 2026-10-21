/*
** LegacyWorldRenderer.BspHeuristics.cpp
**
** Port of the LegacyWorldRenderer.BspHeuristics partial.
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** Config.Render.Mode / Adaptive.* are gl_rendermode / gl_rendermode_* (HelionCVars.h); Stopwatch.GetTimestamp is
** std::chrono::steady_clock. UZDoom-only: BspHeuristics (the AutomapMarker counterpart) walks the main view
** synchronously once per game tic in PerformRender (MarkAutomap; the worker thread of Helion is not ported,
** plan D15). SampleWindow is a median over the last N samples.
*/

#include "OpenGL/Renderers/Legacy/World/LegacyWorldRenderer.h"
#include "OpenGL/Renderers/Legacy/World/BspBox.h"
#include "Common/Shared/Frustum.h"
#include "HelionCVars.h"
#include "World/WorldStatic.h"
#include "g_levellocals.h"
#include "actor.h"
#include "r_utility.h"
#include <algorithm>
#include <chrono>
#include "Renderer.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World
{

bool LegacyWorldRenderer::UseBspBasedOnHeuristic(FLevelLocals* world, Shared::RenderInfo& renderInfo)
{
	// 0 = static, 1 = BSP, 2 = adaptive
	if (gl_rendermode == 1)
	{
		if (m_bspHeuristics != nullptr)
			m_bspHeuristics->Info().UseBsp = true;
		return true;
	}

	if (gl_rendermode == 0)
	{
		if (m_bspHeuristics != nullptr)
			m_bspHeuristics->Info().UseBsp = false;
		return false;
	}

	if (m_bspHeuristics == nullptr)
		return false;

	if (!m_bspHeuristics->Valid())
	{
		m_bspHeuristics->Info().UseBsp = false;
		return false;
	}

	if (m_lastProcessedId == m_bspHeuristics->LastProcessedId())
		return m_bspHeuristics->Info().UseBsp;

	m_lastProcessedId = m_bspHeuristics->LastProcessedId();
	m_smoothedBspTimeUs = AddBspTimeSample(m_bspHeuristics->Microseconds());

	int threshold = std::max((int)gl_rendermode_timethreshold, 1);
	if (m_smoothedBspTimeUs < threshold)
	{
		m_belowThresholdCount++;
		m_aboveThresholdCount = 0;
	}
	else
	{
		m_aboveThresholdCount++;
		m_belowThresholdCount = 0;
	}

	int thresholdCount = AdaptiveSwitchCount;
	bool shouldUseBsp = m_smoothedBspTimeUs < threshold;
	if (!shouldUseBsp && m_belowThresholdCount >= thresholdCount)
		shouldUseBsp = true;
	if (shouldUseBsp && m_aboveThresholdCount >= thresholdCount)
		shouldUseBsp = false;

	// Don't let the BSP heuristics flip-flop too quickly. If the smoothed time is within 10% of the threshold, don't switch.
	if (shouldUseBsp != m_bspHeuristics->Info().UseBsp)
	{
		double percentRange = std::clamp((double)gl_rendermode_hysteresis, 0.01, 0.5);
		double highRange = threshold * (1 + percentRange);
		double lowRange = threshold * (1 - percentRange);
		if (m_smoothedBspTimeUs >= lowRange && m_smoothedBspTimeUs <= highRange)
			shouldUseBsp = m_bspHeuristics->Info().UseBsp;
	}

	// Don't let fast CPUs switch to BSP when it's likely not beneficial.
	if (m_bspHeuristics->SegCount() > std::max((int)gl_rendermode_segthreshold, 1))
		shouldUseBsp = false;

	BspHeuristicInfo& info = m_bspHeuristics->Info();
	info.GameTick = world->maptime;
	info.SmoothTime = (int)m_smoothedBspTimeUs;
	info.AboveThresholdCount = m_aboveThresholdCount;
	info.BelowThresholdCount = m_belowThresholdCount;
	info.SegCount = m_bspHeuristics->SegCount();
	info.UseBsp = shouldUseBsp;
	return info.UseBsp;
}

double LegacyWorldRenderer::AddBspTimeSample(double time)
{
	// SampleWindow.SetWindowSize / AddSampleAndCalcMedian
	m_bspTimeWindowSize = std::clamp((int)gl_rendermode_timewindow, 4, 32);
	while ((int)m_bspTimeWindow.size() >= m_bspTimeWindowSize)
		m_bspTimeWindow.erase(m_bspTimeWindow.begin());
	m_bspTimeWindow.push_back(time);

	std::vector<double> sorted = m_bspTimeWindow;
	std::sort(sorted.begin(), sorted.end());
	size_t mid = sorted.size() / 2;
	if (sorted.size() % 2 == 1)
		return sorted[mid];
	return (sorted[mid - 1] + sorted[mid]) / 2;
}

}
