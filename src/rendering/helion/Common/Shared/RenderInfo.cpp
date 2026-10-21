/*
** RenderInfo.cpp
**
** Port of Helion.Render.OpenGL.Shared.RenderInfo.
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

#include <cassert>
#include <cmath>
#include "Common/Shared/RenderInfo.h"
#include "v_video.h"

namespace Helion::Render::OpenGL::Shared
{

using namespace Helion::Geometry;

RenderInfo::RenderInfo()
{
	// Set must be called on new allocation
	Camera = nullptr;
	ViewerEntity = nullptr;
	ViewSector = nullptr;

	Uniforms.GammaCorrection = 1.0f;
}

void RenderInfo::Set(OldCamera* camera, float tickFraction, const IntRect& viewport, AActor* viewerEntity, bool drawAutomap,
	const Vec2I& automapOffset, double automapScale, sector_t* viewSector,
	::Helion::World::Geometry::Sectors::TransferHeightView transferHeightView,
	bool brightMaps, bool sectorColor, bool sectorFog)
{
	assert(tickFraction >= 0.0 && tickFraction <= 1.0 && "Tick fraction should be in the unit range");

	if (camera != nullptr)
		Camera = camera;
	TickFraction = tickFraction;
	Viewport = viewport;
	ViewerEntity = viewerEntity;
	DrawAutomap = drawAutomap;
	AutomapOffset = automapOffset;
	AutomapScale = automapScale;
	ViewSector = viewSector;
	TransferHeightView = transferHeightView;
	BrightMaps = brightMaps;
	SectorColor = sectorColor;
	SectorFog = sectorFog;

	if (!DrawAutomap)
		LastAutomapOffset = Vec2F(0, 0);
}

static float DegToRad(float deg) { return deg * 3.14159265358979323846f / 180.0f; }
static float RadToDeg(float rad) { return rad * 180.0f / 3.14159265358979323846f; }

// The clip space flip of the mirror portal views (diag(MirrorX ? -1 : 1, MirrorY ? -1 : 1, 1, 1) * projection).
static mat4 ApplyMirror(mat4 projection, bool mirrorX, bool mirrorY)
{
	if (!mirrorX && !mirrorY)
		return projection;
	float* m = projection.mMatrix;
	for (int col = 0; col < 4; col++)
	{
		if (mirrorX)
			m[col * 4 + 0] = -m[col * 4 + 0];
		if (mirrorY)
			m[col * 4 + 1] = -m[col * 4 + 1];
	}
	return projection;
}

mat4 RenderInfo::UnmirroredProjectionMatrix() const
{
	if (HasProjectionOverride)
		return ProjectionOverride;

	mat4 projection;
	float fovy = (float)(2 * RadToDeg(std::atan(std::tan(DegToRad(FieldOfView) / 2) / FovRatio)));
	projection.perspective(fovy, AspectRatio, screen->GetZNear(), screen->GetZFar());
	return projection;
}

mat4 RenderInfo::ProjectionMatrix() const
{
	return ApplyMirror(UnmirroredProjectionMatrix(), MirrorX, MirrorY);
}

mat4 RenderInfo::ProjectionMatrix(float zNear, float zFar) const
{
	mat4 projection = UnmirroredProjectionMatrix();
	const float* m = projection.get();
	// Orthographic projections have m[15] == 1.
	if (m[15] == 1.0f)
		return ApplyMirror(projection, MirrorX, MirrorY);

	mat4 result;
	result.perspective(RadToDeg(FovY()), ProjectionAspect(), zNear, zFar);
	return ApplyMirror(result, MirrorX, MirrorY);
}

float RenderInfo::FovY() const
{
	return 2 * std::atan(1.0f / std::fabs(UnmirroredProjectionMatrix().get()[5]));
}

float RenderInfo::ProjectionAspect() const
{
	const mat4 projection = UnmirroredProjectionMatrix();
	return std::fabs(projection.get()[5] / projection.get()[0]);
}

}
