/*
** hw_clock.cpp
**
** Hardware render profiling info
**
**---------------------------------------------------------------------------
**
** Copyright 2007-2018 Christoph Oelckers
** Copyright 2017-2025 GZDoom Maintainers and Contributors
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** Code written prior to 2026 is also licensed under:
**
** SPDX-License-Identifier: BSD-3-Clause
**
**---------------------------------------------------------------------------
**
*/


#include "c_console.h"
#include "c_dispatch.h"
#include "v_video.h"
#include "hw_clock.h"
#include "i_time.h"
#include "i_interface.h"
#include "printf.h"
#include "doomstat.h"
#include "g_levellocals.h"

glcycle_t RenderWall,SetupWall,ClipWall;
glcycle_t RenderFlat,SetupFlat;
glcycle_t RenderSprite,SetupSprite;
glcycle_t All, Finish, PortalAll, Bsp;
glcycle_t ProcessAll, PostProcess;
glcycle_t RenderAll;
glcycle_t Dirty;
glcycle_t drawcalls;
glcycle_t twoD, Flush3D;
glcycle_t MTWait, WTTotal;
int vertexcount, flatvertices, flatprimitives;

int rendered_lines,rendered_flats,rendered_sprites,render_vertexsplit,render_texsplit,rendered_decals, rendered_portals, rendered_commandbuffers;
int iter_dlightf, iter_dlight, draw_dlight, draw_dlightf;

void ResetProfilingData()
{
	All.Reset();
	All.Clock();
	Bsp.Reset();
	PortalAll.Reset();
	RenderAll.Reset();
	ProcessAll.Reset();
	PostProcess.Reset();
	RenderWall.Reset();
	SetupWall.Reset();
	ClipWall.Reset();
	RenderFlat.Reset();
	SetupFlat.Reset();
	RenderSprite.Reset();
	SetupSprite.Reset();
	drawcalls.Reset();
	MTWait.Reset();
	WTTotal.Reset();

	flatvertices=flatprimitives=vertexcount=0;
	render_texsplit=render_vertexsplit=rendered_lines=rendered_flats=rendered_sprites=rendered_decals=rendered_portals = 0;
}

//-----------------------------------------------------------------------------
//
// Rendering statistics
//
//-----------------------------------------------------------------------------

static void AppendRenderTimes(FString &str)
{
	double setupwall = SetupWall.TimeMS();
	double clipwall = ClipWall.TimeMS();
	double bsp = Bsp.TimeMS() - ClipWall.TimeMS();

	str.AppendFormat("BSP = %2.3f, Clip=%2.3f\n"
		"W: Render=%2.3f, Setup=%2.3f\n"
		"F: Render=%2.3f, Setup=%2.3f\n"
		"S: Render=%2.3f, Setup=%2.3f\n"
		"2D: %2.3f Finish3D: %2.3f\n"
		"Main thread total=%2.3f, Main thread waiting=%2.3f Worker thread total=%2.3f, Worker thread waiting=%2.3f\n"
		"All=%2.3f, Render=%2.3f, Setup=%2.3f, Portal=%2.3f, Drawcalls=%2.3f, Postprocess=%2.3f, Finish=%2.3f\n",
		bsp, clipwall,
		RenderWall.TimeMS(), setupwall,
		RenderFlat.TimeMS(), SetupFlat.TimeMS(),
		RenderSprite.TimeMS(), SetupSprite.TimeMS(),
		twoD.TimeMS(), Flush3D.TimeMS() - twoD.TimeMS(),
		MTWait.TimeMS() + Bsp.TimeMS(), MTWait.TimeMS(), WTTotal.TimeMS(), WTTotal.TimeMS() - setupwall - SetupFlat.TimeMS() - SetupSprite.TimeMS(),
		All.TimeMS() + Finish.TimeMS(), RenderAll.TimeMS(),	ProcessAll.TimeMS(), PortalAll.TimeMS(), drawcalls.TimeMS(), PostProcess.TimeMS(), Finish.TimeMS());
}

static void AppendRenderStats(FString &out)
{
	out.AppendFormat("Walls: %d (%d splits, %d t-splits, %d vertices)\n"
		"Flats: %d (%d primitives, %d vertices)\n"
		"Sprites: %d, Decals=%d, Portals: %d, Command buffers: %d\n",
		rendered_lines, render_vertexsplit, render_texsplit, vertexcount, rendered_flats, flatprimitives, flatvertices, rendered_sprites,rendered_decals, rendered_portals, rendered_commandbuffers );
}

static void AppendLightStats(FString &out)
{
	out.AppendFormat("DLight - Walls: %d processed, %d rendered - Flats: %d processed, %d rendered\n",
		iter_dlight, draw_dlight, iter_dlightf, draw_dlightf );
}

ADD_STAT_ONOFF(rendertimes)
{
	static FString buff;
	static int64_t lasttime=0;
	int64_t t=I_msTime();
	if (t-lasttime>1000)
	{
		buff.Truncate(0);
		AppendRenderTimes(buff);
		lasttime=t;
	}
	return buff;
}

STAT_ON(rendertimes)
{
	doBench++;
}

STAT_OFF(rendertimes)
{
	doBench--;
}

ADD_STAT(renderstats)
{
	FString out;
	AppendRenderStats(out);
	return out;
}

ADD_STAT(lightstats)
{
	FString out;
	AppendLightStats(out);
	return out;
}

static int printstats;
static bool switchfps;
static uint64_t waitstart;
EXTERN_CVAR(Bool, vid_fps)

void CheckBench()
{
	if (printstats && ConsoleState == c_up)
	{
		// if we started the FPS counter ourselves or ran from the console
		// we need to wait for it to stabilize before using it.
		if (waitstart > 0 && I_msTime() - waitstart < 5000) return;

		FString compose;

		if (sysCallbacks.GetLocationDescription) compose = sysCallbacks.GetLocationDescription();

		AppendRenderStats(compose);
		AppendRenderTimes(compose);
		AppendLightStats(compose);
		compose << "\n\n\n";

		FILE *f = fopen("benchmarks.txt", "at");
		if (f != NULL)
		{
			fputs(compose.GetChars(), f);
			fclose(f);
		}
		Printf("Benchmark info saved\n");
		if (switchfps) vid_fps = false;
		printstats = false;
	}
}

CCMD(bench)
{
	printstats = true;
	if (vid_fps == 0)
	{
		vid_fps = 1;
		waitstart = I_msTime();
		switchfps = true;
	}
	else
	{
		if (ConsoleState == c_up) waitstart = I_msTime();
		switchfps = false;
	}
	C_HideConsole ();
}

bool glcycle_t::active = false;

int doBench = 0;

void  checkBenchActive()
{
	glcycle_t::active = (doBench || printstats);
}

//==========================================================================
//
// Per-frame performance log (r_perflog)
//
// Exposes the per-frame profiling timers above to a file: one line per
// frame plus one summary line per loaded level. Used by the levelmesh
// verification tooling (tools/abtest) to collect the classic baseline and
// the later levelmesh comparison runs. Enable with: r_perflog <path>
// Disable by setting it back to "". The file is flushed per frame so the
// data survives the I_FatalError exit that ends a timedemo.
//
//==========================================================================

CVAR(String, r_perflog, "", CVAR_NOSAVE)

extern cycle_t FrameCycles;

static FILE *PerfLogFile = nullptr;
static FString PerfLogOpenPath;
static FLevelLocals *PerfLogLevel = nullptr;
static uint64_t PerfLogFrame = 0;

static void PerfLogHeader(FILE *f)
{
	fprintf(f, "# r_perflog v1\n");
	fprintf(f, "# F <frame> tic=<gametic> total=<D_Display ms> r=<RenderView window ms> "
		"bsp=<traversal ms, incl clip> clip=<wall clip ms> wr=<wall render ms> ws=<wall setup ms> "
		"fr=<flat render ms> fs=<flat setup ms> sr=<sprite render ms> ss=<sprite setup ms> "
		"2d=<2D ms> f3d=<3D flush ms> fin=<finish/present ms> pg=<portal ms> pr=<job processing ms> "
		"dcms=<drawcall submission ms> wl=<wall count> wsp=<wall vertex splits> wv=<wall vertices> "
		"fl=<flat count> fp=<flat primitives> fv=<flat vertices> sp=<sprite count> "
		"dec=<decal count> portals=<portal draw count> cbuf=<command buffer count>\n");
	fprintf(f, "# L map=<name> sectors=<n> lines=<n> subsectors=<n> sprites=<n> polyobjs=<n> "
		"lineportals=<n> portalgroups=<n> ffloors=<n>\n");
}

static void PerfLogClose()
{
	if (PerfLogFile != nullptr)
	{
		fclose(PerfLogFile);
		PerfLogFile = nullptr;
		doBench--;
	}
}

static void PerfLogWriteLevel(FLevelLocals *Level)
{
	int ffloors = 0;
	for (auto &sec : Level->sectors)
	{
		ffloors += sec.e->XFloor.ffloors.Size();
	}
	int sprites = 0;
	for (auto &sub : Level->subsectors)
	{
		sprites += sub.sprites.Size();
	}
	fprintf(PerfLogFile, "L map=%s sectors=%d lines=%d subsectors=%d sprites=%d polyobjs=%d "
		"lineportals=%d portalgroups=%d ffloors=%d\n",
		Level->MapName.GetChars(), Level->sectors.Size(), Level->lines.Size(),
		Level->subsectors.Size(), sprites, Level->Polyobjects.Size(),
		Level->linePortals.Size(), Level->portalGroups.Size(), ffloors);
}

void PerfLogUpdate()
{
	// Sync the open file with the cvar (set / changed / cleared).
	const char *want = r_perflog;
	if (PerfLogFile == nullptr)
	{
		if (want[0] == 0)
		{
			return;
		}
		PerfLogFile = fopen(want, "wt");
		if (PerfLogFile == nullptr)
		{
			Printf("r_perflog: cannot open '%s' for writing\n", want);
			r_perflog = "";
			return;
		}
		PerfLogOpenPath = FString(want);
		PerfLogLevel = nullptr;
		PerfLogFrame = 0;
		doBench++;
		PerfLogHeader(PerfLogFile);
	}
	else if (strcmp(want, PerfLogOpenPath.GetChars()) != 0)
	{
		PerfLogClose();
		PerfLogUpdate();
		return;
	}

	FLevelLocals *Level = primaryLevel;
	if (Level == nullptr)
	{
		return;
	}
	if (Level != PerfLogLevel)
	{
		PerfLogLevel = Level;
		PerfLogWriteLevel(Level);
	}
	fprintf(PerfLogFile, "F %llu tic=%d total=%.3f r=%.3f bsp=%.3f clip=%.3f wr=%.3f ws=%.3f "
		"fr=%.3f fs=%.3f sr=%.3f ss=%.3f 2d=%.3f f3d=%.3f fin=%.3f pg=%.3f pr=%.3f dcms=%.3f "
		"wl=%d wsp=%d wv=%d fl=%d fp=%d fv=%d sp=%d dec=%d portals=%d cbuf=%d\n",
		(unsigned long long)PerfLogFrame++, gametic, FrameCycles.TimeMS(), All.TimeMS(),
		Bsp.TimeMS(), ClipWall.TimeMS(), RenderWall.TimeMS(), SetupWall.TimeMS(),
		RenderFlat.TimeMS(), SetupFlat.TimeMS(), RenderSprite.TimeMS(), SetupSprite.TimeMS(),
		twoD.TimeMS(), Flush3D.TimeMS(), Finish.TimeMS(), PortalAll.TimeMS(),
		ProcessAll.TimeMS(), drawcalls.TimeMS(), rendered_lines, render_vertexsplit,
		vertexcount, rendered_flats, flatprimitives, flatvertices, rendered_sprites,
		rendered_decals, rendered_portals, rendered_commandbuffers);
	fflush(PerfLogFile);
}
