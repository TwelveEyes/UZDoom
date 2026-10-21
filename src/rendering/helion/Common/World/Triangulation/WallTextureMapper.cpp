/*
** WallTextureMapper.cpp
**
** Port of Helion.Render.Common.World.Triangulation.WallTextureMapper.
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** side.Offset is the middle texture offset of the side_t
** (side_t::GetTextureXOffset/YOffset(side_t::mid)); UZDoom scrollers move
** those offsets directly so Helion's ScrollData branch has no counterpart.
*/

#include "Common/World/Triangulation/WallTextureMapper.h"
#include "g_levellocals.h"

namespace Helion::Render::Common::World::Triangulation
{

using namespace Helion::Geometry;

Box2F WallTextureMapper::OneSidedWallUV(line_t* line, side_t* side, float length, float spanZ,
	const Textures::IRenderableTextureHandle& textureHandle)
{
	Box2F uv = textureHandle.UV();
	Vec2F sides = uv.Max - uv.Min;
	Vec2F uvInverse(1.0f / sides.X, 1.0f / sides.Y);
	Vec2F offsetUV((float)side->GetTextureXOffset(side_t::mid) * uvInverse.X,
		(float)side->GetTextureYOffset(side_t::mid) * uvInverse.Y);

	// side.ScrollData != null: TODO in Helion
	// offsetUV += GetScrollOffset(side.ScrollData, SideScrollData.MiddlePosition, textureUVInverse);

	float wallSpanU = length * uvInverse.X;
	float spanV = spanZ * uvInverse.Y;

	float leftU = offsetUV.X;
	float rightU = offsetUV.X + wallSpanU;
	float topV;
	float bottomV;

	if (line->flags & ML_DONTPEGBOTTOM)
	{
		bottomV = 1.0f + offsetUV.Y;
		topV = bottomV - spanV;
	}
	else
	{
		topV = offsetUV.Y;
		bottomV = offsetUV.Y + spanV;
	}

	return Box2F(Vec2F(leftU, topV), Vec2F(rightU, bottomV));
}

}
