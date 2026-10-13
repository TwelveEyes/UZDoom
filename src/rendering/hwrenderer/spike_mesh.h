/*
** spike_mesh.h
**
** Ticket 05 spike (throwaway, never merged).
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

struct FLevelLocals;

// Called once per main view. Starts r_spike_movers, builds/uploads the spike
// mesh when needed, refreshes moving clusters. Returns the effective SPIKE_* mode.
int Spike_Frame(FLevelLocals *Level);

// Playsim hook (P_Ticker): starts r_spike_movers inside the sim tick, so the
// thinkers are part of the game state (thinkers spawned from the renderer are
// thrown away again).
void Spike_TickMovers(FLevelLocals *Level);
