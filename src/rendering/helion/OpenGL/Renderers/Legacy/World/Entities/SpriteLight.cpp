/*
** SpriteLight.cpp
**
** UZDoom-only (plan M4.1): see SpriteLight.h. The light equations are hw_spritelight.cpp's.
**
**---------------------------------------------------------------------------
**
** Copyright 2002-2016 Christoph Oelckers
** Copyright 2017-2025 GZDoom Maintainers and Contributors
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
*/

#include <cmath>
#include "OpenGL/Renderers/Legacy/World/Entities/SpriteLight.h"
#include "a_dynlight.h"
#include "p_local.h"
#include "p_effect.h"
#include "g_levellocals.h"
#include "d_player.h"
#include "actorinlines.h"
#include "hw_shadowmap.h"
#include "hw_cvars.h"
#include "v_video.h"
#include "r_utility.h"
#include "HelionCVars.h"

EXTERN_CVAR(Bool, r_ab_fullbright)
EXTERN_CVAR(Int, gl_enhanced_nv_stealth)

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Entities
{

FullbrightState GetFullbrightState(player_t* player)
{
	FullbrightState state;

	if (r_ab_fullbright)
	{
		state.Fullbright = true;
		return state;
	}

	if (player == nullptr)
		player = r_viewpoint.player;

	player_t* cplayer = player ? player->camera->player : nullptr;
	if (cplayer == nullptr)
		return state;

	if (cplayer->extralight == INT_MIN)
	{
		// This does never set stealth vision.
		state.Fullbright = true;
	}
	else if (cplayer->fixedcolormap == INVERSECOLORMAP && gl_altinvulnoverlay)
	{
		// Helion AlternativeInvulnerabilityOverlay: invulnerability does not make the view fullbright.
	}
	else if (cplayer->fixedcolormap != NOFIXEDCOLORMAP)
	{
		state.Fullbright = true;
		state.StealthVision = gl_enhanced_nv_stealth > 2;
	}
	else if (cplayer->fixedlightlevel != -1 || cplayer->bForceFullbright)
	{
		EFullbrightMode fbmode = cplayer->GetFullbrightMode();
		if (fbmode != FBMODE_NONE)
		{
			state.Fullbright = true;
			if (fbmode == FBMODE_TORCH)
			{
				state.StealthVision = gl_enhanced_nv_stealth > 1;
			}
			else
			{
				state.Nightvision = fbmode == FBMODE_NIGHTVISION;
				state.StealthVision = gl_enhanced_nv_stealth > 0;
			}
		}
	}

	return state;
}

template<class T>
static T smoothstep(const T edge0, const T edge1, const T x)
{
	auto t = clamp((x - edge0) / (edge1 - edge0), 0.0, 1.0);
	return t * t * (3.0 - 2.0 * t);
}

LightProbe* FindLightProbe(FLevelLocals* level, float x, float y, float z)
{
	LightProbe* foundprobe = nullptr;
	if (level->LightProbes.Size() > 0)
	{
		double rcpCellSize = 1.0 / level->LPCellSize;
		int gridCenterX = (int)std::floor(x * rcpCellSize) - level->LPMinX;
		int gridCenterY = (int)std::floor(y * rcpCellSize) - level->LPMinY;
		int gridWidth = level->LPWidth;
		int gridHeight = level->LPHeight;
		float lastdist = 0.0f;
		for (int gridY = gridCenterY - 1; gridY <= gridCenterY + 1; gridY++)
		{
			for (int gridX = gridCenterX - 1; gridX <= gridCenterX + 1; gridX++)
			{
				if (gridX >= 0 && gridY >= 0 && gridX < gridWidth && gridY < gridHeight)
				{
					const LightProbeCell& cell = level->LPCells[gridX + (size_t)gridY * gridWidth];
					for (int i = 0; i < cell.NumProbes; i++)
					{
						LightProbe* probe = cell.FirstProbe + i;
						float dx = probe->X - x;
						float dy = probe->Y - y;
						float dz = probe->Z - z;
						float dist = dx * dx + dy * dy + dz * dz;
						if (!foundprobe || dist < lastdist)
						{
							foundprobe = probe;
							lastdist = dist;
						}
					}
				}
			}
		}
	}
	return foundprobe;
}

void GetDynSpriteLight(FLevelLocals* level, AActor* self, float x, float y, float z, FSection* sec, int portalgroup, float* out)
{
	float frac, lr, lg, lb;
	float radius;

	out[0] = out[1] = out[2] = 0.f;

	LightProbe* probe = Entities::FindLightProbe(level, x, y, z);
	if (probe)
	{
		out[0] = probe->Red;
		out[1] = probe->Green;
		out[2] = probe->Blue;
	}

	for (FDynamicLight* light : sec->dlist)
	{
		if (light->ShouldLightActor(self))
		{
			float dist;
			FVector3 L;

			// This is a performance critical section of code where we cannot afford to let the compiler decide whether to inline the function or not.
			// This will do the calculations explicitly rather than calling one of AActor's utility functions.
			if (level->Displacements.size > 0)
			{
				int fromgroup = light->Sector->PortalGroup;
				int togroup = portalgroup;
				if (fromgroup == togroup || fromgroup == 0 || togroup == 0)
				{
					L = FVector3(x - (float)light->X(), y - (float)light->Y(), z - (float)light->Z());
				}
				else
				{
					DVector2 offset = level->Displacements.getOffset(fromgroup, togroup);
					L = FVector3(x - (float)(light->X() + offset.X), y - (float)(light->Y() + offset.Y), z - (float)light->Z());
				}
			}
			else
			{
				L = FVector3(x - (float)light->X(), y - (float)light->Y(), z - (float)light->Z());
			}

			dist = (float)L.LengthSquared();
			radius = light->GetRadius();

			if (dist < radius * radius)
			{
				dist = sqrtf(dist);	// only calculate the square root if we really need it.

				frac = 1.0f - (dist / radius);

				if (light->IsSpot())
				{
					L *= -1.0f / dist;
					DAngle negPitch = -light->Pitch;
					DAngle Angle = light->Yaw;
					double xyLen = negPitch.Cos();
					double spotDirX = -Angle.Cos() * xyLen;
					double spotDirY = -Angle.Sin() * xyLen;
					double spotDirZ = -negPitch.Sin();
					double cosDir = L.X * spotDirX + L.Y * spotDirY + L.Z * spotDirZ;
					frac *= (float)smoothstep(light->pSpotOuterAngle->Cos(), light->pSpotInnerAngle->Cos(), cosDir);
				}

				if (frac > 0 && (!light->IsShadowMapped() || (light->GetRadius() > 0 && screen->mShadowMap.ShadowTest(light->Pos, { x, y, z }))))
				{
					lr = light->GetRed() / 255.0f;
					lg = light->GetGreen() / 255.0f;
					lb = light->GetBlue() / 255.0f;

					if (light->target && (light->target->renderflags2 & RF2_LIGHTMULTALPHA))
					{
						float alpha = (float)light->target->Alpha;
						lr *= alpha;
						lg *= alpha;
						lb *= alpha;
					}

					// Get GLDEFS intensity
					lr *= light->GetLightDefIntensity();
					lg *= light->GetLightDefIntensity();
					lb *= light->GetLightDefIntensity();

					if (light->IsSubtractive())
					{
						float bright = (float)FVector3(lr, lg, lb).Length();
						lr = (bright - lr) * -1;
						lg = (bright - lg) * -1;
						lb = (bright - lb) * -1;
					}

					out[0] += lr * frac;
					out[1] += lg * frac;
					out[2] += lb * frac;
				}
			}
		}
	}
}

void GetDynSpriteLight(FLevelLocals* level, AActor* thing, particle_t* particle, float* out)
{
	out[0] = out[1] = out[2] = 0.f;
	if (thing && !(thing->renderflags2 & RF2_NODYNAMICLIGHTING))
	{
		GetDynSpriteLight(level, thing, (float)thing->X(), (float)thing->Y(), (float)thing->Center(), thing->section, thing->Sector->PortalGroup, out);
	}
	else if (particle && !(particle->flags & SPF_NODYNAMICLIGHTING))
	{
		GetDynSpriteLight(level, nullptr, (float)particle->Pos.X, (float)particle->Pos.Y, (float)particle->Pos.Z, particle->subsector->section, particle->subsector->sector->PortalGroup, out);
	}
}

}
