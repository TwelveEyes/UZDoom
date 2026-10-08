/*
** d_abcapture.h
**
** A/B capture mode for levelmesh verification (ticket 01): run the engine
** on a synthetic clock -- exactly one sim tick per rendered frame, all
** time sources derived from the per-map frame count -- and dump the final
** composed framebuffer as PNGs for the ticks listed in `r_ab_capture`,
** reusing M_ScreenShot's PNG writer.
**
** r_ab_capture T1,T2,...:outdir
**   e.g.  r_ab_capture 25,50,75,100:/tmp/abcap/MAP01
**
** PNG files are named <map>_<tick>.png under outdir. stdout/console
** markers are "ABCAPTURE <path>" per capture and "ABCAPTURE done" after
** the last one. The engine exits 0 after the last listed tick is captured
** and exits 1 if a level ends with listed ticks unmet.
**
** The synthetic clock is only in effect between AbCapture_BeginRenderPass
** and AbCapture_EndRenderPass. While it is, the I_* time queries in
** i_time.cpp return values derived from AbCaptureTic (the number of
** rendered frames of the current map): I_GetTime -> AbCaptureTic,
** I_GetTimeFrac -> 0.5, I_nsTime -> (AbCaptureTic + 0.5) ticks after the
** start of the current map. Everything in between (interpolation, FOV,
** sky animation, TexAnim, post-process TimeDelta) is a pure function of
** (map, frame), so two runs of the same path compose bit-identical
** frames.
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

#ifndef D_ABCAPTURE_H
#define D_ABCAPTURE_H

// True while a render pass is in flight under r_ab_capture; read by
// i_time.cpp to switch the time sources to the synthetic clock.
extern bool AbCaptureActive;

// True for the entire capture session (set when r_ab_capture is parsed);
// used to lock all inputs so the camera is stable.
extern bool AbCaptureEnabled;

// Number of rendered frames of the current map (0 outside a level).
extern int AbCaptureTic;

void AbCapture_BeginRenderPass ();	// called in D_DoomLoop before D_ProcessEvents/D_Display
void AbCapture_EndRenderPass ();		// called in D_DoomLoop after D_Display
void AbCapture_FrameComposed ();		// called in End2DAndUpdate after twod->End()

#endif // D_ABCAPTURE_H
