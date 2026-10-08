/*
** d_abcapture.cpp
**
** A/B capture mode for levelmesh verification (ticket 01). See
** d_abcapture.h for the mode contract: synthetic clock (one sim tick per
** rendered frame, tick-derived time sources) plus PNG dumps of the
** composed frame for the ticks listed in r_ab_capture.
**
**---------------------------------------------------------------------------
**
** Copyright 2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
*/

#include "d_abcapture.h"

#include "c_cvars.h"
#include "doomstat.h"
#include "engineerrors.h"
#include "g_levellocals.h"
#include "m_misc.h"
#include "menu.h"
#include "printf.h"
#include "r_defs.h"
#include "zstring.h"

// With r_ab_capture active the sim is paced at exactly one tick per
// rendered frame: singletics caps the per-pass tic count (d_net.cpp) and
// is also what forces the wait in TryRunTics, so a frame is composed for
// every tick.
bool AbCaptureActive = false;
int AbCaptureTic = 0;

bool AbCaptureEnabled = false;

namespace
{
	// A/B capture diagnostics must not touch the on-screen console or
	// notification buffers: they would differ run-to-run and pollute the
	// pixel diff. (OR of two flags is an int; Printf wants a PrintFlag.)
	constexpr PrintFlag AbCapturePrint =
		static_cast<PrintFlag>(PRINT_NOCONSOLE | PRINT_NONOTIFY);

	struct CaptureTick
	{
		int tick = 0;
		bool done = false;
	};

	FString OutDir;
	FString MapName;
	TArray<CaptureTick> Ticks;
	FLevelLocals *CapturedLevel = nullptr;
	bool WasInLevel = false;

	void ParseValue(const char *value)
	{
		AbCaptureEnabled = false;
		AbCaptureActive = false;
		AbCaptureTic = 0;
		M_EnableMenu(true);
		Ticks.Clear();
		OutDir = "";
		MapName = "";
		CapturedLevel = nullptr;
		WasInLevel = false;

		if (value[0] == 0)
		{
			singletics = false;
			return;
		}

		ptrdiff_t colon = FString(value).IndexOf(':');
		if (colon == (ptrdiff_t)-1)
		{
			Printf(AbCapturePrint, "ABCAPTURE: r_ab_capture needs \"T1,T2,...:outdir\", got \"%s\"\n", value);
			singletics = false;
			return;
		}

		FString full(value);
		FString tickpart = full.Mid(0, (size_t)colon);
		FString dir = full.Mid((size_t)colon + 1);
		if (dir.IsEmpty())
		{
			Printf(AbCapturePrint, "ABCAPTURE: r_ab_capture needs \"T1,T2,...:outdir\", got \"%s\"\n", value);
			singletics = false;
			return;
		}

		TArray<FString> parts = tickpart.Split(",");
		for (auto &part : parts)
		{
			part.StripLeft();
			part.StripRight();
			if (part.IsEmpty())
				continue;
			int t = atoi(part.GetChars());
			if (t <= 0)
			{
				Printf(AbCapturePrint, "ABCAPTURE: bad tick \"%s\" in \"%s\"\n", part.GetChars(), value);
				singletics = false;
				return;
			}
			bool dup = false;
			for (const auto &c : Ticks)
			{
				if (c.tick == t)
					dup = true;
			}
			if (dup)
				continue;
			CaptureTick ct;
			ct.tick = t;
			Ticks.push_back(ct);
		}
		if (Ticks.Size() == 0)
		{
			Printf(AbCapturePrint, "ABCAPTURE: r_ab_capture has no ticks in \"%s\"\n", value);
			singletics = false;
			return;
		}

		std::sort(Ticks.begin(), Ticks.end(), [](const CaptureTick &a, const CaptureTick &b) { return a.tick < b.tick; });
		OutDir = dir;
		AbCaptureEnabled = true;
		singletics = true;
		M_EnableMenu(false);

		Printf(AbCapturePrint, "ABCAPTURE: %u tick(s):", (unsigned)Ticks.Size());
		for (const auto &t : Ticks)
			Printf(AbCapturePrint, " %d", t.tick);
		Printf(AbCapturePrint, " -> %s\n", OutDir.GetChars());
	}
}

CUSTOM_CVAR(String, r_ab_capture, "", CVAR_NOSAVE)
{
	ParseValue(*self);
}

//==========================================================================
//
// AbCapture_BeginRenderPass
//
// One render pass = one D_Display = one composed frame. Bumps the
// per-map frame counter (which doubles as the synthetic tick time for
// the pass) and detects a run that left the level without meeting the
// tick list.
//
//==========================================================================

void AbCapture_BeginRenderPass ()
{
	if (!AbCaptureEnabled)
	{
		AbCaptureTic = 0;
		AbCaptureActive = false;
		return;
	}

	FLevelLocals *L = (gamestate == GS_LEVEL) ? primaryLevel : nullptr;

	if (L != CapturedLevel)
	{
		// New map (or out of a level): the tick list applies per map, so
		// restart the per-map frame counter and the done flags.
		CapturedLevel = L;
		AbCaptureTic = 0;
		MapName = (L != nullptr) ? L->MapName : FString();
		for (auto &t : Ticks)
			t.done = false;
		if (L != nullptr)
			WasInLevel = true;
	}

	if (L == nullptr)
	{
		AbCaptureActive = false;
		if (WasInLevel)
		{
			int pending = 0;
			for (const auto &t : Ticks)
				if (!t.done)
					pending++;
			Printf(AbCapturePrint, "ABCAPTURE incomplete: level ended with %d tick(s) unmet\n", pending);
			throw CExitEvent(1);
		}
		return;
	}

	AbCaptureTic++;
	AbCaptureActive = true;
}

//==========================================================================
//
// AbCapture_EndRenderPass
//
//==========================================================================

void AbCapture_EndRenderPass ()
{
	AbCaptureActive = false;
}

//==========================================================================
//
// AbCapture_FrameComposed
//
// The frame for this pass is fully composed (scene + 2D + post-process)
// but not yet presented. Dump it if its tick is listed.
//
//==========================================================================

void AbCapture_FrameComposed ()
{
	if (!AbCaptureActive || gamestate != GS_LEVEL)
		return;

	bool hit = false;
	for (auto &t : Ticks)
	{
		// !t.done guards against Update() firing more than once in a pass
		// (e.g. the wipe path) re-dumping the same tick.
		if (t.tick == AbCaptureTic && !t.done)
		{
			t.done = true;
			hit = true;
			break;
		}
	}
	if (!hit)
		return;

	FString path;
	path.Format("%s/%s_%d.png", OutDir.GetChars(), MapName.GetChars(), AbCaptureTic);
	M_ScreenShot(path.GetChars());
	Printf(AbCapturePrint, "ABCAPTURE %s\n", path.GetChars());

	bool all = true;
	for (const auto &t : Ticks)
	{
		if (!t.done)
			all = false;
	}
	if (all)
	{
		Printf(AbCapturePrint, "ABCAPTURE done\n");
		throw CExitEvent(0);
	}
}
