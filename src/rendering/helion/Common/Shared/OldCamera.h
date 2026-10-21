/*
** OldCamera.h
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

#pragma once

#include "Util/Geometry.h"

struct FRenderViewpoint;

namespace Helion::Render::OpenGL::Shared
{

/// <summary>
/// Represents a camera as a point in some world, while also supporting
/// interpolation between a previous and current state.
/// </summary>
class OldCamera
{
public:
	/// <summary>
	/// The up direction for renderers to orient themselves with.
	/// </summary>
	/// <remarks>
	/// Because Doom uses the Z axis as the vertical axis, we have to
	/// reflect that. It's much easier for us to go with the convention
	/// used by the game rather than have Y be the vertical axis, or else
	/// it's easy to forget and mix up the Y and Z axes.
	/// </remarks>
	static inline const Geometry::Vec3F Up = Geometry::Vec3F(0, 0, 1);

	/// <summary>
	/// The current interpolated camera position.
	/// </summary>
	Geometry::Vec3F PositionInterpolated;

	Geometry::Vec3F Position;

	/// <summary>
	/// The directional vector we're facing. This is made up from the yaw
	/// and pitch.
	/// </summary>
	Geometry::Vec3F Direction;

	/// <summary>
	/// The horizontal viewing angle, where zero radians is equal to facing
	/// east, and proceeds counter clockwise.
	/// </summary>
	float YawRadians = 0;

	/// <summary>
	/// The vertical viewing pitch, where zero radians is at a horizontal.
	/// </summary>
	float PitchRadians = 0;

	OldCamera() { Set(Geometry::Vec3F(0, 0, 0), Geometry::Vec3F(0, 0, 0), 0, 0); }

	/// <summary>
	/// Creates a camera at the location with the angles provided.
	/// </summary>
	/// <param name="position">The location of the camera.</param>
	/// <param name="yawRadians">The horizontal looking angle in radians.
	/// </param>
	/// <param name="pitchRadians">The vertical looking angle in radians.
	/// This should be between [-pi/2, pi/2], or else it will be clamped
	/// to that range.</param>
	OldCamera(const Geometry::Vec3F& positionInterpolated, const Geometry::Vec3F& position, float yawRadians, float pitchRadians);

	// UZDoom: builds the camera from the viewpoint of the frame being rendered. r_viewpoint.Pos is
	// already the interpolated eye position (R_SetupFrame), so it is used for both positions unless
	// a separate non-interpolated position is supplied. UZDoom pitch is positive when looking down,
	// Helion's is positive when looking up.
	static OldCamera FromViewpoint(const FRenderViewpoint& viewpoint, const Geometry::Vec3F* position = nullptr);

	void Set(const Geometry::Vec3F& positionInterpolated, const Geometry::Vec3F& position, float yawRadians, float pitchRadians);

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

	/// <summary>
	/// Creates a view matrix for a camera information object.
	/// </summary>
	/// <returns>The view matrix for the camera information.</returns>
	// pixelStretch: UZDoom scales the height axis of the world (Level->info->pixelstretch, hw_drawinfo SetViewMatrix);
	// Helion's 0.825 projection height factor is that stretch. The eye height is scaled with the world, the view
	// direction (pitch) is applied in the stretched space.
	Geometry::mat4 CalculateViewMatrix(bool onlyXY = false, float pixelStretch = 1.0f) const;

private:
	/// <summary>
	/// Calculates the direction from the yaw/pitch.
	/// </summary>
	/// <param name="yawRadians">The yaw in radians.</param>
	/// <param name="pitchRadians">The pitch in radians. This will be
	/// clamped to the range of [-pi/2, pi/2] if not in range.</param>
	/// <returns>The direction from the yaw/pitch combination.</returns>
	static Geometry::Vec3F DirectionFrom(float yawRadians, float pitchRadians);
	static float ClampYaw(double yawRadians);
	static float ClampPitch(double pitchRadians);
};

}
