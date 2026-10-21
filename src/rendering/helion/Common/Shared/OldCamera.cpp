/*
** OldCamera.cpp
**
** Port of Helion.Render.OpenGL.Shared.OldCamera.
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

#include <cmath>
#include "Common/Shared/OldCamera.h"
#include "Util/MathHelper.h"
#include "r_utility.h"

namespace Helion::Render::OpenGL::Shared
{

using namespace Helion::Geometry;
using Helion::Util::MathHelper;

OldCamera::OldCamera(const Vec3F& positionInterpolated, const Vec3F& position, float yawRadians, float pitchRadians)
{
	Set(positionInterpolated, position, yawRadians, pitchRadians);
}

OldCamera OldCamera::FromViewpoint(const FRenderViewpoint& viewpoint, const Vec3F* position)
{
	Vec3F interpolated((float)viewpoint.Pos.X, (float)viewpoint.Pos.Y, (float)viewpoint.Pos.Z);
	// UZDoom: HWAngles.Pitch, the pitch corrected for the pixelstretch of the view matrix (see Camera::FromViewpoint).
	return OldCamera(interpolated, position ? *position : interpolated,
		(float)viewpoint.Angles.Yaw.Radians(), (float)-viewpoint.HWAngles.Pitch.Radians());
}

void OldCamera::Set(const Vec3F& positionInterpolated, const Vec3F& position, float yawRadians, float pitchRadians)
{
	PositionInterpolated = positionInterpolated;
	Position = position;
	YawRadians = ClampYaw(yawRadians);
	PitchRadians = ClampPitch(pitchRadians);
	Direction = DirectionFrom(yawRadians, pitchRadians);
}

float OldCamera::FieldOfViewXToY(float fovX, float aspectRatio)
{
	return 2 * (float)std::atan((float)std::tan(fovX / 2) / aspectRatio);
}

mat4 OldCamera::CalculateViewMatrix(bool onlyXY, float pixelStretch) const
{
	Vec3F pos = PositionInterpolated;
	pos.Z *= pixelStretch;
	Vec3F eye = pos + Vec3F(Direction.X, Direction.Y, onlyXY ? 0 : Direction.Z);
	mat4 view;
	view.loadIdentity();
	view.lookAt(pos.X, pos.Y, pos.Z, eye.X, eye.Y, eye.Z, Up.X, Up.Y, Up.Z);
	view.scale(1.0f, 1.0f, pixelStretch);
	return view;
}

Vec3F OldCamera::DirectionFrom(float yawRadians, float pitchRadians)
{
	float x = (float)(std::cos(yawRadians) * std::cos(pitchRadians));
	float y = (float)(std::sin(yawRadians) * std::cos(pitchRadians));
	float z = (float)std::sin(pitchRadians);
	return Vec3F(x, y, z).Unit();
}

float OldCamera::ClampYaw(double yawRadians)
{
	double clampedYaw = std::fmod(yawRadians, MathHelper::TwoPi);
	if (clampedYaw < 0)
		clampedYaw += MathHelper::TwoPi;
	return (float)clampedYaw;
}

float OldCamera::ClampPitch(double pitchRadians)
{
	return (float)MathHelper::Clamp(pitchRadians, -MathHelper::HalfPi, MathHelper::HalfPi);
}

}
