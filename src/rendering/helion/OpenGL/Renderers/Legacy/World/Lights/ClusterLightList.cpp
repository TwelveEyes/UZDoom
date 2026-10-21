/*
** ClusterLightList.cpp
**
** UZDoom-only (plan M6.1/M6.2): see ClusterLightList.h. Encode is AddLightToList of hw_dynlightdata.cpp.
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
#include "OpenGL/Renderers/Legacy/World/Lights/ClusterLightList.h"
#include "actorinlines.h"
#include "a_dynlight.h"
#include "g_levellocals.h"
#include "g_cvars.h"
#include "hw_shadowmap.h"
#include "v_video.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Lights
{

void ClusterLightList::Encode(FDynamicLight* light, int group, bool forceAttenuate, ClusterLight& out)
{
	int i = ClusterBucketModulated;

	DVector3 pos = light->PosRelative(group);
	float radius = light->GetRadius();

	float cs;
	if (light->IsAdditive())
	{
		cs = 0.2f;
		i = ClusterBucketAdditive;
	}
	else
	{
		cs = 1.0f;
	}

	if (light->target && (light->target->renderflags2 & RF2_LIGHTMULTALPHA))
		cs *= (float)light->target->Alpha;

	// Multiply intensity from GLDEFS
	cs *= (float)light->GetLightDefIntensity();

	float r = light->GetRed() / 255.0f * cs;
	float g = light->GetGreen() / 255.0f * cs;
	float b = light->GetBlue() / 255.0f * cs;

	if (light->IsSubtractive())
	{
		DVector3 v(r, g, b);
		float length = (float)v.Length();

		r = length - r;
		g = length - g;
		b = length - b;
		i = ClusterBucketSubtractive;
	}

	float shadowIndex;
	if (screen->mShadowMap.Enabled()) // note: with shadowmaps switched off, we cannot rely on properly set indices anymore.
	{
		shadowIndex = light->mShadowmapIndex + 1.0f;
	}
	else shadowIndex = 1025.f;
	// Store attenuate flag in the sign bit of the float.
	if (light->IsAttenuated() || forceAttenuate) shadowIndex = -shadowIndex;

	float lightType = 0.0f;
	float spotInnerAngle = 0.0f;
	float spotOuterAngle = 0.0f;
	float spotDirX = 0.0f;
	float spotDirY = 0.0f;
	float spotDirZ = 0.0f;
	if (light->IsSpot())
	{
		lightType = 1.0f;
		spotInnerAngle = (float)light->pSpotInnerAngle->FastCos();
		spotOuterAngle = (float)light->pSpotOuterAngle->FastCos();

		DAngle negPitch = -light->Pitch;
		DAngle Angle = light->Yaw;
		double xzLen = negPitch.FastCos();
		spotDirX = float(-Angle.FastCos() * xzLen);
		spotDirY = float(-negPitch.FastSin());
		spotDirZ = float(-Angle.FastSin() * xzLen);
	}

	float* data = out.f;
	data[0] = float(pos.X);
	data[1] = float(pos.Z);
	data[2] = float(pos.Y);
	data[3] = radius;
	data[4] = r;
	data[5] = g;
	data[6] = b;
	data[7] = shadowIndex;
	data[8] = spotDirX;
	data[9] = spotDirY;
	data[10] = spotDirZ;
	data[11] = lightType;
	data[12] = spotInnerAngle;
	data[13] = spotOuterAngle;
	data[14] = (float)i;	// AddLightToList: unused. Here the bucket the cluster assignment sorts the light into.
	data[15] = 0.0f;
}

void ClusterLightList::Build(FLevelLocals* level, int group, std::vector<ClusterLight>& lights)
{
	lights.clear();
	if (!r_dynlights || !level->HasDynamicLights)
		return;

	for (FDynamicLight* light = level->lights; light != nullptr; light = light->next)
	{
		if (!light->IsActive() || light->DontLightMap())
			continue;
		if (!(light->GetRadius() > 0.0f))
			continue;

		ClusterLight record;
		Encode(light, group, false, record);
		lights.push_back(record);
	}
}

}
