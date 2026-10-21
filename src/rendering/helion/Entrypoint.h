/*
** Entrypoint.h
**
** Entry point of the Helion-style renderer (UZDoom-only, plan M2.4).
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

#include "c_cvars.h"
#include "intrect.h"

struct FRenderViewpoint;
struct sector_t;
class AActor;
struct FLevelLocals;
class player_t;
namespace FileSys { class FileWriter; }
using FileSys::FileWriter;

// Renders the camera textures, the canvases and the main view of the frame. Returns the view sector.
sector_t* RenderView(player_t* player);

// Renders the view of the player into the picture of a savegame.
void WriteSavePic(player_t* player, FileWriter* file, int width, int height);

// Renders one viewpoint through the Helion pipeline: viewport rects, scene render target, view setup, scene, HUD,
// PostProcessScene with the 2D callback. R_SetupFrame has been done by the caller. Returns the view sector.
sector_t* Helion_RenderViewpoint(FRenderViewpoint& mainvp, AActor* camera, IntRect* bounds, float fov, float ratio, float fovratio, bool mainview, bool toscreen);

// Destroys the renderer (video shutdown / level change).
void Helion_ShutdownRenderer();
void Helion_LevelDestroyed(FLevelLocals* level);
