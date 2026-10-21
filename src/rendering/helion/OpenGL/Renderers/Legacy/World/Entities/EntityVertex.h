/*
** EntityVertex.h
**
** Port of Helion.Render.OpenGL.Renderers.Legacy.World.Entities.EntityVertex,
** extended with the UZDoom sprite fields (plan M4.1).
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

#include <cstdint>
#include <bit>
#include <span>
#include "OpenGL/Vertex/VertexAttributes.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Entities
{

// How the vertex shader expands the instance into a quad. UZDoom's sprite types (hw_sprites.cpp CalculateVertices).
enum class EntityOrientation : int
{
	XYBillboard = 0,	// faces the view: yaw and pitch (gl_billboard_mode 1, particles with gl_billboard_particles)
	YBillboard = 1,		// faces the view: yaw only (Helion's viewRightNormal sprite)
	XYFaceCamera = 6,	// FaceCamera plane plus the view pitch tilt (gl_billboard_mode 1 together with gl_billboard_faces_camera)
	FaceCamera = 2,		// the quad plane is perpendicular to the direction from the viewer to the sprite (gl_billboard_faces_camera)
	Flat = 3,			// RF_FLATSPRITE: oriented by Angles (yaw, pitch, roll)
	Wall = 4,			// RF_WALLSPRITE: a vertical quad along Angles.yaw
	Hud = 5,			// weapon / HUD sprite: Pos.xy = lower left corner, Scale = size, in the projection of the HUD (mvp)
};

// Bits of EntityVertex::SpriteOptions (see EntityVertex::PackSpriteOptions).
struct EntityOptions
{
	static constexpr int OrientationMask = 0x7;			// bits 0..2: EntityOrientation
	static constexpr int XFlip = 1 << 3;					// mirror the quad (additional to the flipU of SurfaceOptions: sprite level flip of the quad geometry)
	static constexpr int YFlip = 1 << 4;
	static constexpr int RollCenter = 1 << 5;				// RF_ROLLCENTER / SPF_ROLLCENTER: roll around the sprite center instead of the offsets
	static constexpr int StretchPixels = 1 << 6;			// scale the height by Level->pixelstretch
	static constexpr int OneSided = 1 << 7;				// RF_ONESIDED: discard back faces (wall / flat sprites)
	static constexpr int Roll = 1 << 8;					// RF_ROLLSPRITE / SPF_ROLL: apply Angles.roll
	static constexpr int AngledRoll = 1 << 9;				// RF2_ANGLEDROLL: the roll is relative to the view angle (disables the XY billboard)
	static constexpr int FuzzShift = 10;					// bits 10..13: gl_fuzztype 0..8 (only meaningful when SurfaceOptions fuzz = 1)
	static constexpr int FuzzMask = 0xF;
	static constexpr int ParticleShapeShift = 14;			// bits 14..15: 0 = textured quad, 1 = round, 2 = smooth (gl_particles_style procedural shapes)
	static constexpr int ParticleShapeMask = 0x3;
	static constexpr int Procedural = 1 << 16;			// particle without texture: colour is ObjectColor, shape is the particle shape, size is Scale (world units)
	static constexpr int NoAlphaTest = 1 << 17;			// hw_styleflags == STYLEHW_NoAlphaTest
	static constexpr int SubtractiveStyle = 1 << 18;		// STYLEOP_Sub / RevSub drawn through the sorted bucket
	static constexpr int Shadow = 1 << 19;				// r_actorspriteshadow blob: flat black translucent quad on the floor (Pos.z = floor, Angles.pitch = 90)
	static constexpr int Stencil = 1 << 20;				// TM_STENCIL: rgb = ObjectColor fill, a = texture alpha (STYLE_Stencil / TranslucentStencil / AddStencil, sprite shadows)
	static constexpr int AlphaTexture = 1 << 21;			// TM_ALPHATEXTURE: rgb = ObjectColor, a = texture red (STYLE_Shaded / AddShaded)
	static constexpr int InvertSource = 1 << 22;			// STYLEF_InvertSource / TM_INVERSE
	static constexpr int FadeToBlack = 1 << 23;			// STYLEF_FadeToBlack: fog fades to black, additive fog
	static constexpr int NoColoredLight = 1 << 24;			// LEVEL3_NOCOLOREDSPRITELIGHTING: decolorize the sector light colour, keep fade / desaturation
	static constexpr int FullbrightLight = 1 << 25;			// fullbright sprite: white light colour, light level 255, no light probe; fade / desaturation kept
	static constexpr int IsometricPitch = 1 << 26;			// RF2_ISOMETRICSPRITES: the XY billboard tilts by Angles.pitch (isotheta) instead of the view pitch
};

// C# StructLayout(LayoutKind.Sequential, Pack = 1)
#pragma pack(push, 1)
struct EntityVertex
{
	// Per instance attributes (divisor 1). The quad corners come from gl_VertexID.
	static constexpr bool IsInstanced = true;

	// [VertexAttribute(divisor: 1)]
	float PosX, PosY, PosZ;

	// [VertexAttribute(divisor: 1)]
	// Helion: alpha (8 bits) | fuzz << 8 | flipU << 9 | light level << 10 (8 bits) | colormap << 18
	float SurfaceOptions;

	// [VertexAttribute(divisor: 1)]
	float PrevPosX, PrevPosY, PrevPosZ;

	// [VertexAttribute(divisor: 1)]
	// Helion: texture offset X / clipped offset Z packed (VertexOptions::EntityPackXYZ)
	float OffsetXYZ;

	// [VertexAttribute(divisor: 1)]
	// Helion: light buffer index << 12 | render index (12 bits)
	float RenderOptions;

	// [VertexAttribute(divisor: 1)]
	// Helion: array index << 20 | padded width << 10 | padded height (VertexOptions::EntityPackTextureInfo)
	float TextureInfo;

	// ---- UZDoom extensions ----

	// [VertexAttribute(divisor: 1)] yaw, pitch, roll in degrees (the Angles of HWSprite: flat sprites, wall sprites, roll)
	float AngleYaw, AnglePitch, AngleRoll;

	// [VertexAttribute(divisor: 1)] actor scale (xscale, yscale); Hud: size of the quad; Procedural particle: size
	float ScaleX, ScaleY;

	// [VertexAttribute(divisor: 1)] ZScript SpriteOffset (offx, offy) in view space, HandleSpriteOffsets
	float SpriteOffsetX, SpriteOffsetY;

	// [VertexAttribute(divisor: 1)] SetDynLight colour of the sprite probe (GetDynSpriteLight / light probe); w = 1 when set
	float LightProbeR, LightProbeG, LightProbeB, LightProbeW;

	// [VertexAttribute(divisor: 1)] palette translation index (translation id of the sprite; selects the colormap in palette mode)
	float TranslationIndex;

	// [VertexAttribute(divisor: 1)] EntityOptions bits, bit-cast int
	float SpriteOptions;

	// [VertexAttribute(divisor: 1)] full precision alpha of the render style (SurfaceOptions only has 8 bits)
	float Alpha;

	// [VertexAttribute(divisor: 1)] ThingColor / particle colour modulated with the sector sprite colour, RGBA8 bit-cast
	float ObjectColor;

	// [VertexAttribute(divisor: 1)] sector sprite AdditiveColor (alpha ff), RGBA8 bit-cast
	float AddColor;

	static float PackBits(uint32_t bits) { return std::bit_cast<float>(bits); }

	static float PackSpriteOptions(EntityOrientation orientation, int options, int fuzzType = 0, int particleShape = 0)
	{
		uint32_t packed = ((uint32_t)orientation & EntityOptions::OrientationMask) | (uint32_t)options
			| (((uint32_t)fuzzType & EntityOptions::FuzzMask) << EntityOptions::FuzzShift)
			| (((uint32_t)particleShape & EntityOptions::ParticleShapeMask) << EntityOptions::ParticleShapeShift);
		return std::bit_cast<float>(packed);
	}

	// Helion reflects over the [VertexAttribute] fields; C++ lists them here, in field order.
	static std::span<const Vertex::VertexAttributeAttribute> VertexAttributes()
	{
		using A = Vertex::VertexAttributeAttribute;
		static constexpr A attributes[] =
		{
			{ "pos", A::InferFromField, 3, Vertex::VertexAttribPointerType::Float, false, false, true, 1 },
			{ "surfaceOptions", A::InferFromField, 1, Vertex::VertexAttribPointerType::Float, false, false, true, 1 },
			{ "prevPos", A::InferFromField, 3, Vertex::VertexAttribPointerType::Float, false, false, true, 1 },
			{ "offsetXYZ", A::InferFromField, 1, Vertex::VertexAttribPointerType::Float, false, false, true, 1 },
			{ "renderOptions", A::InferFromField, 1, Vertex::VertexAttribPointerType::Float, false, false, true, 1 },
			{ "textureInfo", A::InferFromField, 1, Vertex::VertexAttribPointerType::Float, false, false, true, 1 },
			{ "angles", A::InferFromField, 3, Vertex::VertexAttribPointerType::Float, false, false, true, 1 },
			{ "spriteScale", A::InferFromField, 2, Vertex::VertexAttribPointerType::Float, false, false, true, 1 },
			{ "spriteOffset", A::InferFromField, 2, Vertex::VertexAttribPointerType::Float, false, false, true, 1 },
			{ "lightProbe", A::InferFromField, 4, Vertex::VertexAttribPointerType::Float, false, false, true, 1 },
			{ "translationIndex", A::InferFromField, 1, Vertex::VertexAttribPointerType::Float, false, false, true, 1 },
			{ "spriteOptions", A::InferFromField, 1, Vertex::VertexAttribPointerType::Float, false, false, true, 1 },
			{ "alpha", A::InferFromField, 1, Vertex::VertexAttribPointerType::Float, false, false, true, 1 },
			{ "objectColor", A::InferFromField, 1, Vertex::VertexAttribPointerType::Float, false, false, true, 1 },
			{ "addColor", A::InferFromField, 1, Vertex::VertexAttribPointerType::Float, false, false, true, 1 },
		};
		return attributes;
	}
};
#pragma pack(pop)

static_assert(sizeof(EntityVertex) == 26 * sizeof(float), "EntityVertex has to stay tightly packed");

}
