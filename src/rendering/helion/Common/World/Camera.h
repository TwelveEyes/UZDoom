/*
** Camera.h
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

#pragma once

#include "Util/Geometry.h"
#include "Util/MathHelper.h"

struct FRenderViewpoint;

namespace Helion::Render::Common::World
{

/// <summary>
/// A camera in a world.
/// </summary>
class Camera
{
public:
	static inline const Geometry::Vec3F Up = Geometry::Vec3F(0, 0, 1);

	static constexpr double MaxPitch = Util::MathHelper::HalfPi - 0.05;

	Geometry::Vec3F PositionInterpolated;
	Geometry::Vec3F Position;

	Camera(const Geometry::Vec3F& positionInterpolated, float yawRadians, float pitchRadians);

	// UZDoom: builds the camera from the viewpoint of the frame being rendered (see OldCamera::FromViewpoint).
	static Camera FromViewpoint(const FRenderViewpoint& viewpoint);

	void Set(const Geometry::Vec3F& positionInterpolated, const Geometry::Vec3F& position, float yawRadians, float pitchRadians);

	const Geometry::Vec3F& Direction() const { return m_direction; }
	float YawRadians() const { return m_yawRadians; }
	float PitchRadians() const { return m_pitchRadians; }
	void SetYawRadians(float yaw) { SetYaw(yaw); }
	void SetPitchRadians(float pitch) { SetPitch(pitch); }

	Geometry::mat4 ViewMatrix(float frac, float pixelStretch = 1.0f) const;

	/// <summary>
	/// Takes a desired X field of view in radians and converts it to the Y
	/// field of view with the provided aspect ratio.
	/// </summary>
	/// <remarks>
	/// Intended primarily for libraries that take a Y field of view when
	/// creating a perspective matrix.
	/// </remarks>
	/// <param name="fovX">The field of view in radians.</param>
	/// <param name="aspectRatio">The aspect ratio of the viewport.</param>
	/// <returns>The Y field of view.</returns>
	static float FieldOfViewXToY(float fovX, float aspectRatio);

protected:
	static float ClampYaw(double yawRadians);
	static float ClampPitch(double pitchRadians);

private:
	void SetYaw(float yaw);
	void SetPitch(float pitch);

	Geometry::Vec3F m_direction;
	float m_yawRadians = 0;
	float m_pitchRadians = 0;
};

}
