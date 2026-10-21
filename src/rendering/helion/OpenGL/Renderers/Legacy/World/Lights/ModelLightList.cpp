/*
** ModelLightList.cpp
**
** UZDoom-only (plan M6.2): see ModelLightList.h. The light gathering is hw_GetDynModelLight (hw_spritelight.cpp), the
** records are ClusterLightList::Encode's (AddLightToList with forceAttenuate).
**
**---------------------------------------------------------------------------
**
** Copyright 2017-2025 GZDoom Maintainers and Contributors
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
*/

#include <algorithm>
#include "OpenGL/Renderers/Legacy/World/Lights/ModelLightList.h"
#include "OpenGL/Renderers/Legacy/World/Lights/ClusterLightList.h"
#include "actor.h"
#include "a_dynlight.h"
#include "g_levellocals.h"
#include "hw_cvars.h"
#include "hw_dynlightdata.h"
#include "hw_lightbuffer.h"
#include "models.h"
#include "r_utility.h"
#include "v_video.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Lights
{

namespace
{

FDynLightData s_lightData;
TArray<FDynamicLight*> s_addedLights;

void AddLight(int group, FDynamicLight* light)
{
	ClusterLight record;
	ClusterLightList::Encode(light, group, true, record);
	int bucket = (int)record.f[14];
	record.f[14] = 0.0f;
	TArray<float>& list = s_lightData.arrays[bucket];
	std::copy(record.f, record.f + 16, &list[list.Reserve(16)]);
}

}

int ModelLightList::Upload(AActor* self)
{
	s_lightData.Clear();

	if (self == nullptr || self->Sector == nullptr)
		return -1;

	float actorradius = (float)self->RenderRadius();

	if (actorradius <= gl_model_light_optimization_radius)
	{
		int group = self->Sector->PortalGroup;
		for (FDynamicLight* light : self->section->dlist)
		{
			if (light->ShouldLightActor(self))
				AddLight(group, light);
		}
	}
	else
	{
		s_addedLights.Clear();

		float x = (float)self->X();
		float y = (float)self->Y();
		float z = (float)self->Center();
		float radiusSquared = actorradius * actorradius;
		dl_validcount++;

		BSPWalkCircle(self->Level, x, y, radiusSquared, [&](subsector_t* subsector) // Iterate through all subsectors potentially touched by actor
		{
			FSection* section = subsector->section;
			if (section->validcount == dl_validcount)
				return;	// already done from a previous subsector.
			section->validcount = dl_validcount;

			for (FDynamicLight* light : section->dlist)
			{
				if (!light->ShouldLightActor(self))
					continue;

				int group = subsector->sector->PortalGroup;
				DVector3 pos = light->PosRelative(group);
				float radius = (float)(light->GetRadius() + actorradius);
				double dx = pos.X - x;
				double dy = pos.Y - y;
				double dz = pos.Z - z;
				double distSquared = dx * dx + dy * dy + dz * dz;
				if (distSquared < radius * radius) // Light and actor touches
				{
					if (std::find(s_addedLights.begin(), s_addedLights.end(), light) == s_addedLights.end()) // Check if we already added this light from a different subsector
					{
						AddLight(group, light);
						s_addedLights.Push(light);
					}
				}
			}
		});
	}

	return screen->mLights->UploadLights(s_lightData);
}

}
