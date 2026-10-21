/*
** Camera.cpp
**
** Port of Helion.Render.Common.World.Camera.
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
#include "Common/World/Camera.h"
#include "r_utility.h"

namespace Helion::Render::Common::World
{

using namespace Helion::Geometry;
using Helion::Util::MathHelper;

Camera::Camera(const Vec3F& positionInterpolated, float yawRadians, float pitchRadians)
{
	m_pitchRadians = ClampPitch(pitchRadians);
	PositionInterpolated = positionInterpolated;
	m_direction = UnitSphere(yawRadians, m_pitchRadians);
	m_yawRadians = ClampYaw(yawRadians);
	Position = positionInterpolated;
}

Camera Camera::FromViewpoint(const FRenderViewpoint& viewpoint)
{
	// UZDoom: HWAngles.Pitch (the pitch corrected for Level->info->pixelstretch, r_utility.cpp), which is what
	// classic SetViewMatrix rotates by after the view matrix stretched the height axis.
	return Camera(Vec3F((float)viewpoint.Pos.X, (float)viewpoint.Pos.Y, (float)viewpoint.Pos.Z),
		(float)viewpoint.Angles.Yaw.Radians(), (float)-viewpoint.HWAngles.Pitch.Radians());
}

void Camera::Set(const Vec3F& positionInterpolated, const Vec3F& position, float yawRadians, float pitchRadians)
{
	m_pitchRadians = ClampPitch(pitchRadians);
	PositionInterpolated = positionInterpolated;
	m_direction = UnitSphere(yawRadians, m_pitchRadians);
	m_yawRadians = ClampYaw(yawRadians);
	Position = position;
}

void Camera::SetYaw(float yaw)
{
	m_yawRadians = ClampYaw(yaw);
	m_direction = UnitSphere(m_yawRadians, m_pitchRadians);
}

void Camera::SetPitch(float pitch)
{
	m_pitchRadians = ClampPitch(pitch);
	m_direction = UnitSphere(m_yawRadians, m_pitchRadians);
}

mat4 Camera::ViewMatrix(float frac, float pixelStretch) const
{
	Vec3F pos = PositionInterpolated;
	pos.Z *= pixelStretch;
	Vec3F eye = pos + m_direction;
	mat4 view;
	view.loadIdentity();
	view.lookAt(pos.X, pos.Y, pos.Z, eye.X, eye.Y, eye.Z, Up.X, Up.Y, Up.Z);
	view.scale(1.0f, 1.0f, pixelStretch);
	return view;
}

float Camera::FieldOfViewXToY(float fovX, float aspectRatio)
{
	return 2 * (float)std::atan((float)std::tan(fovX / 2) / aspectRatio);
}

float Camera::ClampYaw(double yawRadians)
{
	double clampedYaw = std::fmod(yawRadians, MathHelper::TwoPi);
	if (clampedYaw < 0)
		clampedYaw += MathHelper::TwoPi;
	return (float)clampedYaw;
}

float Camera::ClampPitch(double pitchRadians)
{
	return (float)MathHelper::Clamp(pitchRadians, -MaxPitch, MaxPitch);
}

}
