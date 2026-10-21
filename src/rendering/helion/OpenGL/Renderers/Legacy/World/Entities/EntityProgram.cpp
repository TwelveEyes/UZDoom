/*
** EntityProgram.cpp
**
** Port of Helion.Render.OpenGL.Renderers.Legacy.World.Entities.EntityProgram
** (+ EntityTransparentProgram, EntityCompositeProgram, EntityFuzzRefractionProgram).
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** The vertex shader expands one instance (EntityVertex) into a quad. The corner math is
** the one of HWSprite::CalculateVertices / HandleSpriteOffsets (hw_sprites.cpp) evaluated
** in the GL frame of UZDoom (x, height, y) with the matrix helpers of Matrix3x4, then
** converted back to the Helion world frame (x, y, height) the mvp of the Helion programs
** expects. The fragment shader is the Helion entity fragment shader (light level,
** colormaps, brightmaps, sector colormap, fog, OIT) plus the UZDoom sprite features
** (object / add colour, texture modes, fuzz variants, light probe, procedural particles).
**
*/

#include "EntityProgram.h"
#include "EntityVertex.h"
#include "OpenGL/Renderers/Legacy/World/Shader/ShaderVars.h"
#include "OpenGL/Renderers/Legacy/World/Shader/ShaderText.h"
#include "OpenGL/Renderers/Legacy/World/Shader/LightLevel.h"
#include "OpenGL/Renderers/Legacy/World/Shader/SectorColorMap.h"
#include "OpenGL/Vertex/Attributes.h"
#include "engineerrors.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Entities
{

using namespace Shader;
using ::Helion::Render::OpenGL::Shader::ProgramUniforms;

namespace
{

FString LabelOf(const FString& name, const MaterialProgramOptions& material)
{
	FString label;
	label << "Entity - " << name;
	if (material.IsMaterial())
		label.AppendFormat(" - material %s%s", UzdoomContract::MaterialShaderName(material.MaterialShaderIndex).GetChars(), material.Pass == GBUFFER_PASS ? " (gbuffer)" : "");
	return label;
}

// The EntityOptions / EntityOrientation constants as GLSL constants (both stages).
FString OptionDefines()
{
	FString text;
	text.AppendFormat("const int OptOrientationMask = %d;\n", EntityOptions::OrientationMask);
	text.AppendFormat("const int OptYFlip = %d;\n", EntityOptions::YFlip);
	text.AppendFormat("const int OptRollCenter = %d;\n", EntityOptions::RollCenter);
	text.AppendFormat("const int OptStretchPixels = %d;\n", EntityOptions::StretchPixels);
	text.AppendFormat("const int OptOneSided = %d;\n", EntityOptions::OneSided);
	text.AppendFormat("const int OptRoll = %d;\n", EntityOptions::Roll);
	text.AppendFormat("const int OptAngledRoll = %d;\n", EntityOptions::AngledRoll);
	text.AppendFormat("const int OptFuzzShift = %d;\n", EntityOptions::FuzzShift);
	text.AppendFormat("const int OptFuzzMask = %d;\n", EntityOptions::FuzzMask);
	text.AppendFormat("const int OptParticleShapeShift = %d;\n", EntityOptions::ParticleShapeShift);
	text.AppendFormat("const int OptParticleShapeMask = %d;\n", EntityOptions::ParticleShapeMask);
	text.AppendFormat("const int OptProcedural = %d;\n", EntityOptions::Procedural);
	text.AppendFormat("const int OptNoAlphaTest = %d;\n", EntityOptions::NoAlphaTest);
	text.AppendFormat("const int OptShadow = %d;\n", EntityOptions::Shadow);
	text.AppendFormat("const int OptStencil = %d;\n", EntityOptions::Stencil);
	text.AppendFormat("const int OptAlphaTexture = %d;\n", EntityOptions::AlphaTexture);
	text.AppendFormat("const int OptInvertSource = %d;\n", EntityOptions::InvertSource);
	text.AppendFormat("const int OptFadeToBlack = %d;\n", EntityOptions::FadeToBlack);
	text.AppendFormat("const int OptNoColoredLight = %d;\n", EntityOptions::NoColoredLight);
	text.AppendFormat("const int OptFullbrightLight = %d;\n", EntityOptions::FullbrightLight);
	text.AppendFormat("const int OptIsometricPitch = %d;\n", EntityOptions::IsometricPitch);
	text.AppendFormat("const int OrientXYBillboard = %d;\n", (int)EntityOrientation::XYBillboard);
	text.AppendFormat("const int OrientYBillboard = %d;\n", (int)EntityOrientation::YBillboard);
	text.AppendFormat("const int OrientFaceCamera = %d;\n", (int)EntityOrientation::FaceCamera);
	text.AppendFormat("const int OrientFlat = %d;\n", (int)EntityOrientation::Flat);
	text.AppendFormat("const int OrientWall = %d;\n", (int)EntityOrientation::Wall);
	text.AppendFormat("const int OrientHud = %d;\n", (int)EntityOrientation::Hud);
	text.AppendFormat("const int OrientXYFaceCamera = %d;\n", (int)EntityOrientation::XYFaceCamera);
	return text;
}

}

EntityProgram::EntityProgram(const FString& name, const MaterialProgramOptions& material) : RenderProgramBase(LabelOf(name, material)), m_material(material)
{
}

const TArray<Vertex::VaoAttribute>* EntityProgram::VertexLayout() const
{
	return &Vertex::Attributes::ReadStructAttributes<EntityVertex>();
}

void EntityProgram::ProgramCreated()
{
	RenderProgramBase::ProgramCreated();

	m_fuzzFracLocation = Uniforms.GetLocation("fuzzFrac");
	m_viewRightNormalLocation = Uniforms.GetLocation("viewRightNormal");
	m_fuzzDivLocation = Uniforms.GetLocation("fuzzDiv");
	m_maxDistanceLocation = Uniforms.GetLocation("maxDistanceSquared");
	m_fadeDistanceLocation = Uniforms.GetLocation("fadeDistance");
	m_viewPosLocation = Uniforms.GetLocation("viewPos");
	m_renderFuzzLocation = Uniforms.GetLocation("renderFuzz");
	m_renderFuzzRefractionColorLocation = Uniforms.GetLocation("renderFuzzRefractionColor");
	m_mapDataTextureLocation = Uniforms.GetLocation("mapDataTexture");
	m_lineHeightsTextureLocation = Uniforms.GetLocation("lineHeightsTexture");
	m_fuzzTextureLocation = Uniforms.GetLocation("fuzzTexture");
	m_opaqueTextureLocation = Uniforms.GetLocation("opaqueTexture");
	m_colorClampLocation = Uniforms.GetLocation("colorClamp");
	m_viewAnglesLocation = Uniforms.GetLocation("viewAngles");
	m_pixelStretchLocation = Uniforms.GetLocation("pixelStretch");
	m_orthoViewLocation = Uniforms.GetLocation("orthoView");
	m_useLightProbeLocation = Uniforms.GetLocation("useLightProbe");
	m_enhancedNightvisionLocation = Uniforms.GetLocation("enhancedNightvision");
	m_maskThresholdLocation = Uniforms.GetLocation("maskThreshold");
	m_cullColorLocation = Uniforms.GetLocation("cullColor");
	m_fuzzTimerLocation = Uniforms.GetLocation("fuzzTimer");
}

void EntityProgram::FuzzTexture(TextureUnit unit) { ProgramUniforms::Set(unit, m_fuzzTextureLocation); }
void EntityProgram::OpaqueTexture(TextureUnit unit) { ProgramUniforms::Set(unit, m_opaqueTextureLocation); }
void EntityProgram::MapDataTexture(TextureUnit unit) { ProgramUniforms::Set(unit, m_mapDataTextureLocation); }
void EntityProgram::LineHeightsTexture(TextureUnit unit) { ProgramUniforms::Set(unit, m_lineHeightsTextureLocation); }
void EntityProgram::FuzzFrac(float frac) { ProgramUniforms::Set(frac, m_fuzzFracLocation); }
void EntityProgram::ViewRightNormal(const ::Helion::Geometry::Vec2F& viewRightNormal) { ProgramUniforms::Set(viewRightNormal, m_viewRightNormalLocation); }
void EntityProgram::FuzzDiv(float div) { ProgramUniforms::Set(div, m_fuzzDivLocation); }
void EntityProgram::MaxDistanceSquared(float value) { ProgramUniforms::Set(value, m_maxDistanceLocation); }
void EntityProgram::FadeDistance(float value) { ProgramUniforms::Set(value, m_fadeDistanceLocation); }
void EntityProgram::ViewPos(const ::Helion::Geometry::Vec3F& pos) { ProgramUniforms::Set(pos, m_viewPosLocation); }
void EntityProgram::RenderFuzz(bool value) { ProgramUniforms::Set(value, m_renderFuzzLocation); }
void EntityProgram::RenderFuzzRefractionColor(bool value) { ProgramUniforms::Set(value, m_renderFuzzRefractionColorLocation); }
void EntityProgram::ColorClamp(float value) { ProgramUniforms::Set(value, m_colorClampLocation); }
void EntityProgram::ViewAngles(const ::Helion::Geometry::Vec3F& yawPitchRoll) { ProgramUniforms::Set(yawPitchRoll, m_viewAnglesLocation); }
void EntityProgram::OrthoView(bool value) { ProgramUniforms::Set(value, m_orthoViewLocation); }
void EntityProgram::PixelStretch(float pixelStretch) { ProgramUniforms::Set(pixelStretch, m_pixelStretchLocation); }
void EntityProgram::UseLightProbe(bool value) { ProgramUniforms::Set(value, m_useLightProbeLocation); }
void EntityProgram::EnhancedNightvision(bool value) { ProgramUniforms::Set(value, m_enhancedNightvisionLocation); }
void EntityProgram::MaskThreshold(float value) { ProgramUniforms::Set(value, m_maskThresholdLocation); }
void EntityProgram::CullColor(const FVector4& color) { ProgramUniforms::Set(color, m_cullColorLocation); }
void EntityProgram::FuzzTimer(float seconds) { ProgramUniforms::Set(seconds, m_fuzzTimerLocation); }

OitOptions EntityProgram::GetOitOptions() const
{
	if (dynamic_cast<const EntityTransparentProgram*>(this) != nullptr)
		return OitOptions::OitTransparentPass;
	if (dynamic_cast<const EntityCompositeProgram*>(this) != nullptr)
		return OitOptions::OitCompositePass;
	if (dynamic_cast<const EntityFuzzRefractionProgram*>(this) != nullptr)
		return OitOptions::OitFuzzRefractionPass;
	return OitOptions::None;
}

//==========================================================================
//
// Vertex shader
//
//==========================================================================

FString EntityProgram::VertexShader()
{
	bool material = m_material.IsMaterial();

	FString text = R"GLSL(
        #version 330
        ${OptionDefines}

        layout(location = 0) in vec3 pos;
        layout(location = 1) in float surfaceOptions;
        layout(location = 2) in vec3 prevPos;
        layout(location = 3) in float offsetXYZ;
        layout(location = 4) in float renderOptions;
        layout(location = 5) in float textureInfo;
        layout(location = 6) in vec3 angles;
        layout(location = 7) in vec2 spriteScale;
        layout(location = 8) in vec2 spriteOffset;
        layout(location = 9) in vec4 lightProbe;
        layout(location = 10) in float translationIndex;
        layout(location = 11) in float spriteOptions;
        layout(location = 12) in float alpha;
        layout(location = 13) in float objectColor;
        layout(location = 14) in float addColor;

        flat out float lightLevelFrag;
        flat out float alphaFrag;
        flat out float fuzzFrag;
        flat out float colorMapTranslationFrag;
        flat out float zPosFrag;
        flat out float textureWidthFrag;
        flat out float boundTextureIndex;
        flat out vec3 centerPosFrag;
        ${MinMaxPosVariables}
        flat out vec3 sectorColorMapIndexFrag;
        flat out vec4 sectorFogColorFrag;
        flat out float fuzzDist;
        flat out float renderDistSquared;
        flat out float spriteOptionsFrag;
        flat out float objectColorFrag;
        flat out float addColorFrag;
        flat out vec4 lightProbeFrag;
        flat out vec4 uzLightFrag;
        flat out vec2 uvMaxFrag;
        flat out vec2 textureDimFrag;

        out vec2 uvFrag;
        out float dist2D;
        out float dist3D;
        out vec3 worldPosFrag;

        uniform mat4 mvp;
        uniform vec4 helionClipPlane;
        uniform mat4 mvpNoPitch;
        uniform vec2 viewRightNormal;
        uniform vec3 viewPos;
        uniform vec3 viewAngles;
        uniform float pixelStretch;
        uniform int orthoView;
        uniform float timeFrac;
        uniform int useSectorColor;
        uniform int useSectorFog;
        uniform int useLightProbe;
        ${VertexTextureUniform}
        uniform samplerBuffer sectorColormapTexture;
        uniform samplerBuffer sectorFogTexture;

        float distSquared(vec2 v1, vec2 v2) {
            vec2 length = v1.xy - v2.xy;
            return (length.x * length.x) + (length.y * length.y);
        }

        // Matrix3x4 of hw_sprites.cpp (column vector convention, post-multiplied like glTranslate / glScale / glRotate).
        mat4 mTranslate(vec3 t) {
            mat4 m = mat4(1.0);
            m[3] = vec4(t, 1.0);
            return m;
        }

        mat4 mScale(vec3 s) {
            return mat4(vec4(s.x, 0.0, 0.0, 0.0), vec4(0.0, s.y, 0.0, 0.0), vec4(0.0, 0.0, s.z, 0.0), vec4(0.0, 0.0, 0.0, 1.0));
        }

        mat4 mRotate(vec3 axis, float degrees) {
            axis = normalize(axis);
            float c = cos(radians(degrees));
            float s = sin(radians(degrees));
            float t = 1.0 - c;
            float r00 = t * axis.x * axis.x + c;
            float r01 = t * axis.x * axis.y - s * axis.z;
            float r02 = t * axis.x * axis.z + s * axis.y;
            float r10 = t * axis.x * axis.y + s * axis.z;
            float r11 = t * axis.y * axis.y + c;
            float r12 = t * axis.y * axis.z - s * axis.x;
            float r20 = t * axis.x * axis.z - s * axis.y;
            float r21 = t * axis.y * axis.z + s * axis.x;
            float r22 = t * axis.z * axis.z + c;
            return mat4(vec4(r00, r10, r20, 0.0), vec4(r01, r11, r21, 0.0), vec4(r02, r12, r22, 0.0), vec4(0.0, 0.0, 0.0, 1.0));
        }

        // The UZDoom GL frame is (x, height, y), the Helion world frame is (x, y, height).
        vec3 toGl(vec3 v) {
            return vec3(v.x, v.z, v.y);
        }

        // HandleSpriteOffsets: the ZScript SpriteOffset in view space, as a GL frame translation.
        vec3 spriteOffsetTranslation(bool xyBillboard) {
            float yaw = radians(270.0 - viewAngles.x);
            float pitch = xyBillboard ? radians(viewAngles.y) : 0.0;
            vec3 sideVec = vec3(-sin(yaw), cos(yaw), 0.0);
            vec3 upVec = vec3(sin(pitch) * cos(yaw), sin(pitch) * sin(yaw), cos(pitch));
            vec3 res = sideVec * spriteOffset.x + upVec * spriteOffset.y;
            return vec3(res.x, res.z, res.y);
        }

        // The sprite quad: corners in the doom frame as x1/x2/y1/y2/z1/z2 of HWSprite and the matrix of CalculateVertices.
        mat4 gMat;
        vec3 gPos;
        float gX1, gX2, gY1, gY2, gZ1, gZ2;

        vec3 spriteCornerGl(float xs, float ys, bool flatSprite) {
            vec3 v;
            if (flatSprite)
                v = vec3(mix(gX2, gX1, xs), gPos.z, mix(gY2, gY1, ys));
            else
                v = vec3(mix(gX1, gX2, xs), mix(gZ1, gZ2, ys), mix(gY1, gY2, xs));
            return (gMat * vec4(v, 1.0)).xyz;
        }

        void main()
        {
            int intOptions = floatBitsToInt(surfaceOptions);
            alphaFrag = alpha;
            fuzzFrag = float((intOptions >> 8) & 1);
            int flipU = (intOptions >> 9) & 1;
            lightLevelFrag = float((intOptions >> 10) & 0xFF);
            colorMapTranslationFrag = translationIndex;

            int opts = floatBitsToInt(spriteOptions);
            int orientation = opts & OptOrientationMask;
            bool procedural = (opts & OptProcedural) != 0;
            spriteOptionsFrag = spriteOptions;
            objectColorFrag = objectColor;
            addColorFrag = addColor;
            lightProbeFrag = (useLightProbe != 0 && lightProbe.w > 0.0 && (opts & OptFullbrightLight) == 0) ? lightProbe : vec4(0.0);

            intOptions = floatBitsToInt(renderOptions);
            int lightIndexInt = (intOptions >> 12) & 0xFFFFF;
            int renderIndex = intOptions & 0xFFF;

            intOptions = floatBitsToInt(offsetXYZ);
            float offsetXYOption = float((intOptions >> 16) & 0x3FFF);
            float offsetZ = float(intOptions & 0x3FFF);
            float offsetXYSign = float(((intOptions >> 31) & 1) > 0);
            float offsetZSign = float(((intOptions >> 30) & 1) > 0);
            offsetXYOption = mix(offsetXYOption, -offsetXYOption, offsetXYSign);
            offsetZ = mix(offsetZ, -offsetZ, offsetZSign);

            intOptions = floatBitsToInt(textureInfo);
            boundTextureIndex = float((intOptions >> 20) & 0xFFF);
            ivec2 textureDim = ivec2((intOptions >> 10) & 0x3FF, intOptions & 0x3FF);
            textureWidthFrag = float(textureDim.x);
            textureDimFrag = vec2(textureDim);

            ${SectorColorMapVertexFunction}

            if ((opts & OptFadeToBlack) != 0)
                sectorFogColorFrag.rgb = vec3(0.0);

            // b < 0: the r channel is a palette colormap index, there is no colour to modify.
            if ((opts & OptFullbrightLight) != 0)
            {
                lightLevelFrag = 255.0;
                if (sectorColorMapIndexFrag.b >= 0.0)
                    sectorColorMapIndexFrag = vec3(1.0);
            }
            else if ((opts & OptNoColoredLight) != 0 && sectorColorMapIndexFrag.b >= 0.0)
            {
                // PalEntry::Decolorize
                float gray = (3.0 + 2.0 * (sectorColorMapIndexFrag.r + sectorColorMapIndexFrag.g + sectorColorMapIndexFrag.b)) / 9.0;
                sectorColorMapIndexFrag = vec3(gray);
            }

            // The inputs of the truecolor light stage (LightLevel::UzdoomLightVariables): sprites have no relative light and
            // their colour slot is a plane slot, so the wall glow never applies (HWSprite has no glow).
            uzLightFrag = vec4(lightLevelFrag, 0.0, float(lightIndexInt), texelFetch(sectorColormapTexture, lightIndexInt).a);

            ${UvMax}
            uvMaxFrag = uvMax;

            vec3 interpolatedPos = mix(prevPos, pos, timeFrac);
            gPos = interpolatedPos;
            float xs = float(gl_VertexID & 1);     // 0,1,0,1
            float ys = float(gl_VertexID >> 1);    // 0,0,1,1

            vec3 world;
            bool backFace = false;
            bool hud = orientation == OrientHud;
            float depthBias = float(renderIndex) * 5e-4;

            if (hud)
            {
                // Pos.xy, PrevPos.xy, Angles.xy and SpriteOffset are the four corners (left top, left bottom, right top, right bottom) in HUD pixels.
                int id = gl_VertexID;
                vec2 corner = id == 0 ? pos.xy : (id == 1 ? prevPos.xy : (id == 2 ? angles.xy : spriteOffset));
                float xSel = float(id >> 1);
                float ySel = float(id & 1);
                float u = flipU == 0 ? xSel : 1.0 - xSel;
                float v = (opts & OptYFlip) == 0 ? ySel : 1.0 - ySel;
                uvFrag = vec2(u, v) * uvMax;
                world = vec3(corner, pos.z);
                depthBias = 0.0;
                zPosFrag = pos.z;
                centerPosFrag = pos;
                ${MinMaxPosHud}
            }
            else
            {
                float ps = pixelStretch;
                float sx = spriteScale.x;
                float sy = spriteScale.y;

                // The sprite rectangle (r of HWSprite::Process) in world units.
                float spriteWidth;
                float spriteHeight;
                float rectLeft;
                float bottomOffset;
                if (procedural)
                {
                    // Pos is the centre of the particle, Scale its half extent.
                    spriteWidth = sx * 2.0;
                    spriteHeight = sy * 2.0;
                    rectLeft = -sx;
                    bottomOffset = -sy;
                }
                else
                {
                    spriteWidth = float(textureDim.x) * sx;
                    spriteHeight = float(textureDim.y) * sy;
                    rectLeft = -offsetXYOption * sx;
                    bottomOffset = offsetZ * sy;
                }

                if ((opts & OptStretchPixels) != 0)
                    spriteHeight *= ps;

                float rightfac = -rectLeft;
                float leftfac = rightfac - spriteWidth;
                float z2 = interpolatedPos.z + bottomOffset;
                float z1 = z2 + spriteHeight;
                float x = interpolatedPos.x;
                float y = interpolatedPos.y;
                float z = interpolatedPos.z;

                bool isFlat = orientation == OrientFlat;
                bool isWall = orientation == OrientWall;
                if (isFlat)
                {
                    float bottomfac = z1 - z;
                    float topfac = z2 - z;
                    gX1 = x + leftfac;
                    gX2 = x + rightfac;
                    gY1 = y - topfac;
                    gY2 = y - bottomfac;
                }
                else if (isWall)
                {
                    float viewvecX = cos(radians(angles.x));
                    float viewvecY = sin(radians(angles.x));
                    gX1 = x + viewvecY * leftfac;
                    gX2 = x + viewvecY * rightfac;
                    gY1 = y - viewvecX * leftfac;
                    gY2 = y - viewvecX * rightfac;
                }
                else
                {
                    // The left vector of the view (-viewRightNormal).
                    vec2 leftVec = -viewRightNormal;
                    gX1 = x + leftVec.x * leftfac;
                    gX2 = x + leftVec.x * rightfac;
                    gY1 = y + leftVec.y * leftfac;
                    gY2 = y + leftVec.y * rightfac;
                }
                gZ1 = z1;
                gZ2 = z2;

                vec3 center = vec3((gX1 + gX2) * 0.5, (gY1 + gY2) * 0.5, (z1 + z2) * 0.5);
                float xx = x - center.x;
                float yy = y - center.y;
                float zz = z - center.z;

                bool useOffsets = (opts & OptRollCenter) == 0;
                bool doRoll = (opts & OptRoll) != 0;
                bool angledRoll = (opts & OptAngledRoll) != 0;
                bool hasOffset = spriteOffset.x != 0.0 || spriteOffset.y != 0.0;
                bool faceCamera = orientation == OrientFaceCamera || orientation == OrientXYFaceCamera;
                bool drawWithXYBillboard = (orientation == OrientXYBillboard || orientation == OrientXYFaceCamera) && !angledRoll;
                float angleRad = radians(270.0 - viewAngles.x);

                if (isFlat)
                {
                    // [MC] Rotate around the center or offsets given to the sprites.
                    gMat = mTranslate(vec3(x, z, y));
                    gMat = gMat * mRotate(vec3(0.0, 1.0, 0.0), 270.0 - angles.x);
                    gMat = gMat * mRotate(vec3(1.0, 0.0, 0.0), -angles.y);

                    if ((opts & OptRollCenter) != 0)
                    {
                        gMat = gMat * mTranslate(vec3(center.x - x, 0.0, center.y - y));
                        gMat = gMat * mRotate(vec3(0.0, 1.0, 0.0), -angles.z);
                        gMat = gMat * mTranslate(vec3(-center.x, -z, -center.y));
                    }
                    else
                    {
                        gMat = gMat * mRotate(vec3(0.0, 1.0, 0.0), -angles.z);
                        gMat = gMat * mTranslate(vec3(-x, -z, -y));
                    }
                }
                else if (drawWithXYBillboard || faceCamera || isWall)
                {
                    gMat = mTranslate(toGl(center));
                    gMat = gMat * mScale(vec3(1.0, 1.0 / ps, 1.0));

                    if (hasOffset)
                        gMat = gMat * mTranslate(spriteOffsetTranslation(true));

                    if (faceCamera && !isWall)
                    {
                        float xrel = center.x - viewPos.x;
                        float yrel = center.y - viewPos.y;
                        float absAngleDeg = degrees(atan(-yrel, xrel));
                        float counterRotationDeg = 270.0 - viewAngles.x;
                        gMat = gMat * mRotate(vec3(0.0, 1.0, 0.0), counterRotationDeg + absAngleDeg);
                    }

                    float rollDegrees = doRoll ? angles.z : 0.0;
                    if (isWall)
                    {
                        if (doRoll)
                        {
                            float yawvecX = cos(radians(angles.x));
                            float yawvecY = sin(radians(angles.x));
                            if (useOffsets) gMat = gMat * mTranslate(vec3(xx, zz, yy));
                            gMat = gMat * mRotate(vec3(yawvecX, 0.0, yawvecY), rollDegrees);
                            if (useOffsets) gMat = gMat * mTranslate(vec3(-xx, -zz, -yy));
                        }
                    }
                    else if (doRoll)
                    {
                        if (useOffsets) gMat = gMat * mTranslate(vec3(xx, zz, yy));
                        if (drawWithXYBillboard)
                            gMat = gMat * mRotate(vec3(-sin(angleRad), 0.0, cos(angleRad)), -viewAngles.y);
                        gMat = gMat * mRotate(vec3(cos(angleRad), 0.0, sin(angleRad)), rollDegrees);
                        if (useOffsets) gMat = gMat * mTranslate(vec3(-xx, -zz, -yy));
                    }
                    else if (drawWithXYBillboard)
                    {
                        // Rotate the sprite about the vector starting at the center of the sprite
                        // triangle strip and with direction orthogonal to where the player is looking
                        // in the x/y plane.
                        gMat = gMat * mRotate(vec3(-sin(angleRad), 0.0, cos(angleRad)), -viewAngles.y);
                    }

                    gMat = gMat * mScale(vec3(1.0, ps, 1.0));
                    gMat = gMat * mTranslate(-toGl(center));
                }
                else
                {
                    // traditional "Y" billboard mode
                    gMat = mat4(1.0);

                    if (hasOffset)
                        gMat = gMat * mTranslate(spriteOffsetTranslation(false));

                    if (doRoll)
                    {
                        float rollDegrees = angles.z;
                        float pitchDegrees = 0.0;

                        if (angledRoll)
                        {
                            // angles.x is the actor yaw plus AngledRollOffset
                            rollDegrees = rollDegrees - 360.0 * trunc(rollDegrees / 360.0);
                            angleRad = radians((270.0 - viewAngles.x) + angles.x - 90.0);
                            vec3 relPos = center - viewPos;
                            if (useOffsets) relPos += vec3(xx, yy, zz);

                            float tiltAngle = radians(angles.x);
                            vec3 rolled = (mRotate(vec3(cos(tiltAngle), sin(tiltAngle), 0.0), -rollDegrees) * vec4(relPos, 1.0)).xyz;
                            pitchDegrees = 270.0 - degrees(atan(rolled.y, rolled.x));
                        }

                        gMat = gMat * mTranslate(toGl(center));
                        if (useOffsets) gMat = gMat * mTranslate(vec3(xx, zz, yy));
                        gMat = gMat * mScale(vec3(1.0, 1.0 / ps, 1.0));
                        if (angledRoll) gMat = gMat * mRotate(vec3(0.0, 1.0, 0.0), -viewAngles.x); // Cancel regular Y-billboarding
                        gMat = gMat * mRotate(vec3(cos(angleRad), 0.0, sin(angleRad)), rollDegrees);
                        if (angledRoll) gMat = gMat * mRotate(vec3(0.0, 1.0, 0.0), pitchDegrees); // New Y-billboarding about rolled z-axis
                        gMat = gMat * mScale(vec3(1.0, ps, 1.0));
                        if (useOffsets) gMat = gMat * mTranslate(vec3(-xx, -zz, -yy));
                        gMat = gMat * mTranslate(-toGl(center));
                    }

                    if ((opts & OptIsometricPitch) != 0 && orthoView != 0)
                    {
                        // RF2_ISOMETRICSPRITES in an orthographic view: tilt about the bottom edge by isotheta (Angles.pitch).
                        float isoAngleRad = radians(270.0 - viewAngles.x);
                        gMat = gMat * mTranslate(toGl(center));
                        gMat = gMat * mTranslate(vec3(0.0, gZ2 - center.z, 0.0));
                        gMat = gMat * mRotate(vec3(-sin(isoAngleRad), 0.0, cos(isoAngleRad)), -angles.y);
                        gMat = gMat * mTranslate(vec3(0.0, center.z - gZ2, 0.0));
                        gMat = gMat * mTranslate(-toGl(center));
                    }
                }

                if ((opts & OptOneSided) != 0 && (isFlat || isWall))
                {
                    // RF_ONESIDED: visible from the front only. Walls face along Angles.yaw, flat sprites face up.
                    vec3 normalGl = isFlat ? vec3(0.0, 1.0, 0.0) : vec3(cos(radians(angles.x)), 0.0, sin(radians(angles.x)));
                    vec3 normal = (gMat * vec4(normalGl, 0.0)).xyz;
                    vec3 reference = spriteCornerGl(0.0, 0.0, isFlat);
                    backFace = dot(normal, toGl(viewPos) - reference) < 0.0;
                }

                vec3 gl = spriteCornerGl(xs, ys, isFlat);
                world = vec3(gl.x, gl.z, gl.y);

                float u = flipU == 0 ? 1.0 - xs : xs;
                float v = (opts & OptYFlip) == 0 ? ys : 1.0 - ys;
                uvFrag = vec2(u, v) * uvMax;

                zPosFrag = interpolatedPos.z;
                centerPosFrag = interpolatedPos;
                ${MinMaxPosSet}
            }

            vec4 clip = mvp * vec4(world, 1.0);
            // Push depth biased by the base times the renderIndex to prevent z-fighting
            clip.z -= (depthBias * (1.0 - (clip.z / clip.w * 0.5 + 0.5))) * clip.w;

            gl_Position = clip;
            gl_ClipDistance[0] = hud ? 1.0 : dot(helionClipPlane, vec4(world, 1.0));
            dist3D = gl_Position.z;
            dist2D = hud ? 1.0 : ${Dist2D};
            // HUD sprites: pixelpos of main.vp is the vertex (screen x, screen y, 0) in the HUD frame; the light stage measures its
            // distance to the camera like the table shaders do.
            worldPosFrag = hud ? vec3(world.xy, 0.0) : vec3(world.x, world.z, world.y);
            fuzzDist = dist3D;
            renderDistSquared = hud ? 0.0 : distSquared(viewPos.xy, interpolatedPos.xy);

            if (backFace)
                gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
            #ifdef VULKAN_COORDINATE_SYSTEM
            gl_Position.z = (gl_Position.z + gl_Position.w) / 2.0;
            #endif
        }
    )GLSL";

	bool swEmulation = ShaderVars::SoftwareSpriteEmulation;
	text = Replace(text, "${OptionDefines}", OptionDefines());
	// The truecolor light stage takes the view space depth (LightLevel::VertexDist); material programs are always truecolor.
	text = Replace(text, "${Dist2D}", (material || !ShaderVars::PaletteColorMode) ? "clip.w" : "(mvpNoPitch * vec4(world, 1.0)).z");
	text = Replace(text, "${SectorColorMapVertexFunction}", SectorColorMap::VertexFunction("lightIndexInt", "sectorColorMapIndexFrag", "sectorFogColorFrag"));
	text = Replace(text, "${MinMaxPosVariables}", swEmulation ? "flat out vec3 minPosFrag;\n        flat out vec3 maxPosFrag;" : "");
	text = Replace(text, "${MinMaxPosHud}", swEmulation ? "minPosFrag = pos;\n                maxPosFrag = pos;" : "");
	text = Replace(text, "${MinMaxPosSet}", swEmulation ?
		"vec3 minGl = spriteCornerGl(0.0, 1.0, isFlat);\n                vec3 maxGl = spriteCornerGl(1.0, 1.0, isFlat);\n"
		"                minPosFrag = vec3(minGl.x, minGl.z, minGl.y);\n                maxPosFrag = vec3(maxGl.x, maxGl.z, maxGl.y);" : "");

	// UV: the sprite image is the part [0, size / layer size] of the array layer; materials and procedural quads cover the whole image.
	if (!material)
	{
		text = Replace(text, "${VertexTextureUniform}", "uniform sampler2DArray boundTexture;");
		text = Replace(text, "${UvMax}",
			"vec2 uvMax = procedural ? vec2(1.0) : vec2(textureDim) / vec2(textureSize(boundTexture, 0).xy);");
	}
	else if (m_material.ArrayTexture)
	{
		text = Replace(text, "${VertexTextureUniform}", "uniform sampler2DArray tex;");
		text = Replace(text, "${UvMax}",
			"vec2 uvMax = procedural ? vec2(1.0) : vec2(textureDim) / vec2(textureSize(tex, 0).xy);");
	}
	else
	{
		text = Replace(text, "${VertexTextureUniform}", "");
		text = Replace(text, "${UvMax}", "vec2 uvMax = vec2(1.0);");
	}

	return text;
}

//==========================================================================
//
// Fragment shader
//
//==========================================================================

static FString SoftwareSpriteEmulationFunctions()
{
	if (!ShaderVars::SoftwareSpriteEmulation)
		return "";

	return R"GLSL(
        bool lineIntersection(vec2 startA, vec2 endA, vec2 startB, vec2 endB) {
            // use epsilon for approximate checks to deal with sprites that are exactly on lines / points
            const float MinEpsilon = 0.001;
            const float MaxEpsilon = 0.999;
            vec2 deltaA = endA - startA;
            vec2 deltaB = endB - startB;
            float d = deltaA.x * -deltaB.y + deltaA.y * deltaB.x;
            float t = ((startB.x - startA.x) * (startB.y - endB.y) - (startB.y - startA.y) * (startB.x - endB.x)) / d;
            float u = ((startB.x - startA.x) * (startA.y - endA.y) - (startB.y - startA.y) * (startA.x - endA.x)) / d;
            return t > MinEpsilon && t < MaxEpsilon && u > MinEpsilon && u < MaxEpsilon;
        }

        vec2 closestPoint(vec2 point, vec2 lineStart, vec2 lineDelta) {
            vec2 pointDelta = point - lineStart;
            float t = clamp(dot(pointDelta, lineDelta) / dot(lineDelta, lineDelta), 0.0, 1.0);
            return lineStart + t * lineDelta;
        }

        float distSquared(vec2 v1, vec2 v2) {
            vec2 length = v1.xy - v2.xy;
            return (length.x * length.x) + (length.y * length.y);
        }

        bool discardPlaneClip() {
            ivec2 sampleCoords = ivec2(clamp(gl_FragCoord.xy / downScaleAmount, vec2(0.0), screenBounds / downScaleAmount));
            vec4 wallClip = texelFetch(wallClipTexture, sampleCoords, 0).rgba;
            vec3 planeClip = texelFetch(planeClipTexture, sampleCoords, 0).rgb;

            // Floor
            if (planeClip.b == 1 && planeClip.g < dist3D && planeClip.r > zPosFrag)
                return true;

            // Ceiling
            if (planeClip.b == 2 && planeClip.g < dist3D && zPosFrag >= planeClip.r)
                return true;

            if (wallClip.r >= 0) {
                int byte0 = int(wallClip.r);
                int byte1 = int(wallClip.g);
                int packedByte = int(wallClip.b);
                int byte2 = packedByte >> 2;
                int upperLowerFlag = packedByte & 0x3;

                int lineId = byte0 | (byte1 << 8) | (byte2 << 16);

                vec4 linePoints = texelFetch(mapDataTexture, lineId);
                vec3 floorHeights = texelFetch(lineHeightsTexture, lineId).rgb;
                float renderBlock = floorHeights.b;
                float floorHeight = mix(floorHeights.r, floorHeights.g, timeFrac);
                vec2 lineStart = linePoints.rg;
                vec2 lineEnd = linePoints.ba;
                vec2 lineDelta = lineEnd - lineStart;

                float viewDotProduct = (lineDelta.x * (viewPos.y - lineStart.y)) - (lineDelta.y * (viewPos.x - lineStart.x));
                float entityDotProduct = (lineDelta.x * (centerPosFrag.y - lineStart.y)) - (lineDelta.y * (centerPosFrag.x - lineStart.x));
                float distanceToWallSquared = distSquared(centerPosFrag.xy, closestPoint(centerPosFrag.xy, lineStart, lineDelta)) + 0.01;

                bool viewFront = viewDotProduct < 0;
                bool entityFront = entityDotProduct < 0;

                // lower wall
                // renderBlock: 1 = front side, 2 = back side, 3 = both. Doom will clip if there is midtex.
                float blockSide = mix(2, 1, float(viewFront));
                if (upperLowerFlag == 1 && renderBlock != 3 && renderBlock != blockSide &&
                    distanceToWallSquared <= max(40*40, textureWidthFrag*textureWidthFrag) &&
                    viewPos.z > floorHeight && floorHeight <= zPosFrag) {
                        return false;
                }

                if (wallClip.a < dist3D) {
                    // Discard if the sprite isn't on the same side of the line as the camera or when the sprite line doesn't intersect the line
                    return viewFront != entityFront || !lineIntersection(lineStart, lineEnd, minPosFrag.xy, maxPosFrag.xy);
                }
                else {
                    // Discard if the sprite is behind the line and intersects
                    return viewFront != entityFront && lineIntersection(lineStart, lineEnd, minPosFrag.xy, maxPosFrag.xy);
                }
            }

            return false;
        })GLSL";
}

static FString CheckPlaneClipText()
{
	if (!ShaderVars::SoftwareSpriteEmulation)
		return "";

	return R"GLSL(
            if (checkPlaneClip == 1 && discardPlaneClip())
                discard;)GLSL";
}

// The sprite texel: texture (or procedural particle shape) with the UZDoom texture modes, object / add colour and fuzz variants.
// The texel stage of UZDoom's getTexel + the ProcessTexel of the fuzz_*.fp shaders.
static FString EntityTexelFunctions(bool paletteMode)
{
	FString text = R"GLSL(
        int entityOptions() {
            return floatBitsToInt(spriteOptionsFrag);
        }

        // PalEntry colours are packed r | g << 8 | b << 16 | a << 24 (unpackUnorm4x8).
        vec4 entityUnpackColor(float packedColor) {
            uint u = floatBitsToUint(packedColor);
            return vec4(float(u & 0xFFu), float((u >> 8) & 0xFFu), float((u >> 16) & 0xFFu), float(u >> 24)) / 255.0;
        }

        float entityGrayscale(vec4 color) {
            return dot(color.rgb, vec3(0.2126, 0.7152, 0.0722));
        }

        // glpart.png (smooth) and glpart2.png (round) as shapes; square particles fill the quad.
        vec4 entityProceduralTexel(vec2 uv) {
            int shape = (entityOptions() >> OptParticleShapeShift) & OptParticleShapeMask;
            float r = length(uv * 2.0 - 1.0);
            float a = 1.0;
            if (shape == 1)
                a = clamp((1.0 - r) * 16.0, 0.0, 1.0);
            else if (shape == 2) {
                float s = clamp(1.0 - r, 0.0, 1.0);
                a = mix(s, s * s * (3.0 - 2.0 * s), 0.5);
            }
            return vec4(1.0, 1.0, 1.0, a);
        }

        vec4 entityBaseTexel(vec2 uv) {
            int options = entityOptions();
            vec4 texel;
            if ((options & OptProcedural) != 0)
                texel = entityProceduralTexel(uv);
            else
                texel = texture(boundTexture, vec3(uv, boundTextureIndex));
            ${TextureModes}
            return texel;
        }
)GLSL";

	if (!paletteMode)
	{
		text = Replace(text, "${TextureModes}", R"GLSL(
            if ((options & OptStencil) != 0)
                texel.rgb = vec3(1.0);
            else if ((options & OptAlphaTexture) != 0)
                texel = vec4(1.0, 1.0, 1.0, entityGrayscale(texel) * texel.a);
            else if ((options & OptInvertSource) != 0)
                texel = vec4(1.0 - texel.r, 1.0 - texel.b, 1.0 - texel.g, texel.a);

            // The Doom64 style material colours of getTexel.
            texel.rgb += entityUnpackColor(addColorFrag).rgb;
            texel *= entityUnpackColor(objectColorFrag);
            if ((options & OptShadow) != 0)
                texel.rgb = vec3(0.0);)GLSL");
	}
	else
	{
		// The colormap lookup happens on the index texture later, colours are modulated after the lookup. A fill needs the
		// alpha flag (which requests the colormap fetch) cleared.
		text = Replace(text, "${TextureModes}", R"GLSL(
            if ((options & (OptStencil | OptAlphaTexture | OptProcedural)) != 0) {
                texel.rgb = vec3(1.0);
                texel.a = mix(texel.a, 1.0, float(texel.a == 0.9960784313725490196078431372549));
            }
            if ((options & OptShadow) != 0)
                texel.rgb = vec3(0.0);)GLSL");
	}

	// gl_fuzztype 1..8: the ProcessTexel of fuzz_standard / smooth / swirly / smoothtranslucent / jagged / noise / smoothnoise / software.
	text << R"GLSL(
        #define FUZZTABLE 50
        #define FUZZ_RANDOM_X_SIZE 100
        #define FRACBITS 16

        const int fuzz_random_x_offset[FUZZ_RANDOM_X_SIZE] = int[]
        (
            75, 76, 21, 91, 56, 33, 62, 99, 61, 79,
            95, 54, 41, 18, 69, 43, 49, 59, 10, 84,
            94, 17, 57, 46,  9, 39, 55, 34,100, 81,
            73, 88, 92,  3, 63, 36,  7, 28, 13, 80,
            16, 96, 78, 29, 71, 58, 89, 24,  1, 35,
            52, 82,  4, 14, 22, 53, 38, 66, 12, 72,
            90, 44, 77, 83,  6, 27, 48, 30, 42, 32,
            65, 15, 97, 20, 67, 74, 98, 85, 60, 68,
            19, 26,  8, 87, 86, 64, 11, 37, 31, 47,
            25,  5, 50, 51, 23,  2, 93, 70, 40, 45
        );

        const int fuzzoffset[FUZZTABLE] = int[]
        (
             6, 11,  6, 11,  6,  6, 11,  6,  6, 11,
             6,  6,  6, 11,  6,  6,  6, 11, 15, 18,
            21,  6, 11, 15,  6,  6,  6,  6, 11, 6,
            11,  6,  6, 11, 15,  6,  6, 11, 15, 18,
            21,  6,  6,  6,  6, 11,  6,  6, 11, 6
        );

        vec4 entityFuzzTexel(vec2 uv, int type, vec4 basicColor) {
            const float pi = 3.14159265358979323846;
            float timer = fuzzTimer;
            vec2 texCoord = uv / uvMaxFrag;

            if (type == 8) {
                // fuzz_software.fp
                int next_random = int(abs(mod(timer * 35.0, float(FUZZ_RANDOM_X_SIZE))));
                int fuzzpos = (fuzz_random_x_offset[next_random] * FUZZTABLE / 100) % FUZZTABLE;
                int x = int(gl_FragCoord.x);
                int y = int(gl_FragCoord.y);
                int fuzzscale = (200 << FRACBITS) / (screenBounds.y + 1);
                int scaled_x = (x * fuzzscale) >> FRACBITS;
                int fuzz_x = fuzz_random_x_offset[scaled_x % FUZZ_RANDOM_X_SIZE] + fuzzpos;
                int fuzzcount = FUZZTABLE << FRACBITS;
                int fuzz = ((fuzz_x << FRACBITS) + y * fuzzscale) % fuzzcount;
                float a = float(fuzzoffset[fuzz >> FRACBITS]) / 32.0;
                return vec4(0.0, 0.0, 0.0, basicColor.a * a);
            }

            if (type == 5) {
                // fuzz_jagged.fp
                vec2 texSplat;
                texSplat.x = texCoord.x + mod(sin(pi * 2.0 * (texCoord.y + timer * 2.0)), 0.1) * 0.1;
                texSplat.y = texCoord.y + mod(cos(pi * 2.0 * (texCoord.x + timer * 2.0)), 0.1) * 0.1;
                basicColor = entityBaseTexel(texSplat * uvMaxFrag);
                float texX = sin(texCoord.x * 100.0 + timer * 5.0);
                float texY = cos(texCoord.x * 100.0 + timer * 5.0);
                float vX = (texX / texY) * 21.0;
                float vY = (texY / texX) * 13.0;
                float test = mod(timer * 2.0 + (vX + vY), 0.5);
                basicColor.a = basicColor.a * test;
                return basicColor;
            }

            // fuzz_standard / noise pixelate the coordinates to the texture
            if (type == 1 || type == 6) {
                texCoord.x = float(int(texCoord.x * textureDimFrag.x)) / textureDimFrag.x;
                texCoord.y = float(int(texCoord.y * textureDimFrag.y)) / textureDimFrag.y;
            }

            float texX;
            float texY;
            if (type == 1 || type == 2) {
                // fuzz_standard / fuzz_smooth
                texX = texCoord.x / 3.0 + 0.66;
                texY = 0.34 - texCoord.y / 3.0;
            }
            else if (type == 6 || type == 7) {
                // fuzz_noise / fuzz_smoothnoise
                texX = sin(mod(texCoord.x * 100.0 + timer * 5.0, 3.489)) + texCoord.x / 4.0;
                texY = cos(mod(texCoord.y * 100.0 + timer * 5.0, 3.489)) + texCoord.y / 4.0;
            }
            else {
                // fuzz_swirly / fuzz_smoothtranslucent
                texX = sin(texCoord.x * 100.0 + timer * 5.0);
                texY = cos(texCoord.x * 100.0 + timer * 5.0);
            }

            float vX = (texX / texY) * 21.0;
            float vY = (texY / texX) * 13.0;
            float test = mod(timer * 2.0 + (vX + vY), 0.5);
            basicColor.a = basicColor.a * test;
            // fuzz_smoothtranslucent keeps the colour, the others are black
            if (type != 4)
                basicColor.rgb = vec3(0.0);
            return basicColor;
        }

        vec4 entityTexel(vec2 uv) {
            vec4 texel = entityBaseTexel(uv);
            int type = (entityOptions() >> OptFuzzShift) & OptFuzzMask;
            if (type > 0 && enhancedNightvision == 0)
                texel = entityFuzzTexel(uv, type, texel);
            return texel;
        }
)GLSL";

	return text;
}

static FString PostProcessText(OitOptions oit, bool material, bool paletteMode)
{
	FString text;

	// The pixels of fuzz types 1..8 (and of the enhanced vision translucent drawing) are not refracted.
	if (oit == OitOptions::OitFuzzRefractionPass && !material)
	{
		text << R"GLSL(
        if (((entityOptions() >> OptFuzzShift) & OptFuzzMask) > 0 || enhancedNightvision != 0)
            discard;
        )GLSL";
	}

	if (!material && paletteMode)
	{
		// Object / add colour after the colormap lookup.
		text << R"GLSL(
        fragColor.rgb = (fragColor.rgb + entityUnpackColor(addColorFrag).rgb) * entityUnpackColor(objectColorFrag).rgb;
        )GLSL";
	}

	if (oit == OitOptions::None && !material)
	{
		// gl_mask_sprite_threshold alpha test (STYLEHW_NoAlphaTest sprites and translucent styles are not tested; the renderer sets the
		// threshold to 0 for styles the original tests with AlphaFunc off). gl_sprite_blend only changes the blend state, not the shader.
		text << R"GLSL(
        if ((entityOptions() & OptNoAlphaTest) == 0 && fragColor.a < maskThreshold)
            discard;
        if (fragColor.a <= 0)
            discard;
        )GLSL";
	}

	// Distance fade of Helion; r_distance_cull_type > 1 (thick fog) fades to gl_cullcolor instead of making the sprite transparent.
	text << R"GLSL(
        if (renderDistSquared > maxDistanceSquared - fadeDistance) {
            float fade = (maxDistanceSquared - renderDistSquared) / fadeDistance;
            if (cullColor.a > 0.0)
                fragColor.rgb = mix(fragColor.rgb, cullColor.rgb, clamp(1.0 - fade, 0.0, 1.0));
            else
                fragColor.a = fragColor.a * fade;
        }
        )GLSL";
	return text;
}

std::optional<FString> EntityProgram::FragmentShader()
{
	bool material = m_material.IsMaterial();
	OitOptions oit = GetOitOptions();

	// Material programs are always truecolor.
	PaletteColorModeScope colorMode(material ? false : ShaderVars::PaletteColorMode);
	bool paletteMode = ShaderVars::PaletteColorMode;
	bool uzdoomLightMode = !paletteMode && !material;

	FString text = R"GLSL(
        #version 330

        ${OptionDefines}

        in vec2 uvFrag;
        in float dist2D;
        in float dist3D;
        in vec3 worldPosFrag;
        flat in float fuzzDist;
        flat in float renderDistSquared;
        flat in float lightLevelFrag;
        flat in float alphaFrag;
        flat in float fuzzFrag;
        flat in float colorMapTranslationFrag;
        flat in float zPosFrag;
        flat in float textureWidthFrag;
        flat in float boundTextureIndex;
        flat in vec3 centerPosFrag;
        ${MinMaxPosVariables}
        flat in float spriteOptionsFrag;
        flat in float objectColorFrag;
        flat in float addColorFrag;
        flat in vec4 lightProbeFrag;
        flat in vec2 uvMaxFrag;
        flat in vec2 textureDimFrag;

        ${SectorColorMapFragVariables}

        ${OutFragColor}

        uniform int hasInvulnerability;
        uniform float fuzzFrac;
        ${ArrayTextureUniforms}
        uniform vec3 colorMix;
        uniform float fuzzDiv;
        uniform int paletteIndex;
        uniform int colormapIndex;
        uniform float maxDistanceSquared;
        uniform float fadeDistance;
        uniform int renderFuzz;
        uniform int renderFuzzRefractionColor;
        uniform ivec2 screenBounds;
        uniform int checkPlaneClip;
        uniform int useBrightmaps;
        uniform vec3 viewPos;
        uniform float timeFrac;
        uniform float downScaleAmount;
        uniform float colorClamp;
        uniform int useSectorFog;
        uniform float fuzzTimer;
        uniform int enhancedNightvision;
        uniform float maskThreshold;
        uniform vec4 cullColor;

        ${ClipUniforms}

        ${LightLevelFragVariables}
        ${OitVariables}
        ${FuzzFunction}
        ${SoftwareSpriteEmulationFunctions}
        ${UzdoomPrelude}
        ${EntityTexelFunctions}

        void main()
        {
            ${CheckPlaneClip}
            ${EntityDynLight}
            ${LightLevelFragFunction}
            ${SectorColorMapFragFunction}
            ${FragColorFunction}
            ${UzdoomMainEnd}
        }
    )GLSL";

	FragColorFunctionOptions options;
	if (material)
		options = FragColorFunctionOptions::Alpha | FragColorFunctionOptions::UzdoomMaterial;
	else
	{
		options = FragColorFunctionOptions::Fuzz | FragColorFunctionOptions::Alpha | FragColorFunctionOptions::Colormap | FragColorFunctionOptions::Brightmaps;
		if (uzdoomLightMode)
			options = options | FragColorFunctionOptions::UzdoomLightMode;
	}

	FString fragColorFunction = FragFunction::FragColorFunction(options, ColorMapFetchContext::Entity, oit, PostProcessText(oit, material, paletteMode));

	if (!material)
	{
		// The sprite texel replaces the plain texture fetch.
		const char* sampler = "texture(boundTexture, vec3(texUV, boundTextureIndex))";
		if (fragColorFunction.IndexOf(sampler) < 0)
			I_FatalError("EntityProgram: the texture fetch of FragColorFunction changed");
		fragColorFunction = Replace(fragColorFunction, sampler, "entityTexel(texUV)");

		if (uzdoomLightMode)
		{
			// Sprites have no surface normal (vWorldNormal is not set on sprites in main.fp).
			const char* normal = "vWorldNormal = vec4(faceNormal, 1.0);";
			if (fragColorFunction.IndexOf(normal) < 0)
				I_FatalError("EntityProgram: the light level of FragColorFunction changed");
			fragColorFunction = Replace(fragColorFunction, normal, "vWorldNormal = vec4(0.0);");

			// material_nolight.fp: Base * clamp(light + desaturate(dynlight), 0, 1.4), the dynamic light being the sprite's light probe.
			const char* uzdoomLight = "fragColor.rgb * uzColor)";
			if (fragColorFunction.IndexOf(uzdoomLight) < 0)
				I_FatalError("EntityProgram: the light multiplication of FragColorFunction changed");
			fragColorFunction = Replace(fragColorFunction, uzdoomLight, "fragColor.rgb * clamp(uzColor + HelionDesaturate(entityDynLight, uzLightFrag.w), 0.0, 1.4))");
		}
		else if (!paletteMode)
		{
			// func_defaultlight: Base * clamp(light + dynlight, 0, 1.4), the dynamic light being the sprite's light probe.
			const char* brightmapLight = "fragColor.rgb *= min(vec3(1.0), texture(brightmapTexture, vec3(texUV, boundTextureIndex)).rgb + vec3(lightLevel));";
			const char* plainLight = "fragColor.rgb *= lightLevel;";
			if (fragColorFunction.IndexOf(brightmapLight) < 0 || fragColorFunction.IndexOf(plainLight) < 0)
				I_FatalError("EntityProgram: the light multiplication of FragColorFunction changed");
			fragColorFunction = Replace(fragColorFunction, brightmapLight,
				"fragColor.rgb *= clamp(min(vec3(1.0), texture(brightmapTexture, vec3(texUV, boundTextureIndex)).rgb + vec3(lightLevel)) + entityDynLight, 0.0, 1.4);");
			fragColorFunction = Replace(fragColorFunction, plainLight, "fragColor.rgb *= clamp(vec3(lightLevel) + entityDynLight, 0.0, 1.4);");
		}
	}

	// Fullbright sprites (HWSprite::fullbright) are fullbright in the truecolor light stage too.
	const char* spriteFullbright = "float((floatBitsToInt(spriteOptionsFrag) & OptFullbrightLight) != 0)";
	if (material)
	{
		const char* fullbright = "float uzFullbright = float(lightLevelMix >= 1.0);";
		if (fragColorFunction.IndexOf(fullbright) < 0)
			I_FatalError("EntityProgram: the fullbright test of FragColorFunction changed");
		fragColorFunction = Replace(fragColorFunction, fullbright, FStringf("float uzFullbright = max(%s, float(lightLevelMix >= 1.0));", spriteFullbright));
	}
	else if (uzdoomLightMode)
	{
		const char* fullbright = "float uzFullbright = max(fullBrightFlag, float(lightLevelMix >= 1.0));";
		if (fragColorFunction.IndexOf(fullbright) < 0)
			I_FatalError("EntityProgram: the fullbright test of FragColorFunction changed");
		fragColorFunction = Replace(fragColorFunction, fullbright, FStringf("float uzFullbright = max(max(fullBrightFlag, %s), float(lightLevelMix >= 1.0));", spriteFullbright));
	}

	// HUD sprites (classic DrawPSprite in DrawEndScene2D): SetColor(weapon) with the light level of the weapon renderer
	// (GetWeaponLighting: CalcLightColor = light colour * light / 255), without fog and in the fallback light mode.
	if (material || uzdoomLightMode)
	{
		const char* uzColor = material ? "vec3 uzColor = uzLight.color + HelionWallGlow(uzLightFrag.z, uzLightFrag.w) * (1.0 - uzFullbright);"
			: "vec3 uzColor = min(uzLight.color + HelionWallGlow(uzLightFrag.z, uzLightFrag.w) * (1.0 - uzFullbright), vec3(1.0));";
		if (fragColorFunction.IndexOf(uzColor) < 0)
			I_FatalError("EntityProgram: the light colour of FragColorFunction changed");
		FString hudColor = uzColor;
		hudColor << R"GLSL(
            if ((floatBitsToInt(spriteOptionsFrag) & OptOrientationMask) == OrientHud)
            {
                if (uzFullbright < 0.5)
                    uzColor = floor(sectorColorMapIndexFrag * lightLevelFrag) / 255.0;
                uzLight.fogEnabled = 0;
            }
)GLSL";
		fragColorFunction = Replace(fragColorFunction, uzColor, hudColor);
	}

	FString prelude;
	if (material)
	{
		// sprites have no lightmap (vLightmap.z < 0: none)
		prelude = "const vec3 lightmapFrag = vec3(0.0, 0.0, -1.0);\n";
		prelude << UzdoomContract::MaterialPrelude(m_material);
	}
	else if (uzdoomLightMode)
		prelude = UzdoomContract::LightModePrelude();

	FString outFragColor;
	if (oit != OitOptions::OitTransparentPass)
	{
		outFragColor = "out vec4 fragColor;";
		if (material)
			outFragColor << UzdoomContract::PassOutputs(m_material.Pass);
	}

	text = Replace(text, "${OptionDefines}", OptionDefines());
	text = Replace(text, "${MinMaxPosVariables}", ShaderVars::SoftwareSpriteEmulation ? "flat in vec3 minPosFrag;\n        flat in vec3 maxPosFrag;" : "");
	// Material variants are limited by the binding count: sprite clipping needs the clip textures, so only the main variants have it.
	text = Replace(text, "${ClipUniforms}", material ? "" :
		"uniform sampler2D planeClipTexture;\n        uniform sampler2D wallClipTexture;\n        uniform samplerBuffer mapDataTexture;\n        uniform samplerBuffer lineHeightsTexture;");
	text = Replace(text, "${SoftwareSpriteEmulationFunctions}", material ? FString() : SoftwareSpriteEmulationFunctions());
	text = Replace(text, "${CheckPlaneClip}", material ? FString() : CheckPlaneClipText());
	text = Replace(text, "${EntityDynLight}", material ? "" : "vec3 entityDynLight = lightProbeFrag.rgb;");
	text = Replace(text, "${LightLevelFragVariables}", LightLevel::FragVariables(LightLevelOptions::Default));
	text = Replace(text, "${LightLevelFragFunction}", LightLevel::FragFunction());
	text = Replace(text, "${FuzzFunction}", FragFunction::FuzzFunction());
	text = Replace(text, "${FragColorFunction}", fragColorFunction);
	text = Replace(text, "${SectorColorMapFragVariables}", SectorColorMap::FragVariables());
	text = Replace(text, "${SectorColorMapFragFunction}", SectorColorMap::FragFunction());
	text = Replace(text, "${OitVariables}", FragFunction::OitFragVariables(oit));
	text = Replace(text, "${OutFragColor}", outFragColor);
	text = Replace(text, "${ArrayTextureUniforms}", material ? "" : "uniform sampler2DArray boundTexture;\n        uniform sampler2DArray brightmapTexture;");
	text = Replace(text, "${UzdoomPrelude}", prelude);
	text = Replace(text, "${EntityTexelFunctions}", material ? FString() : EntityTexelFunctions(paletteMode));
	text = Replace(text, "${UzdoomMainEnd}", material ? UzdoomContract::PassMainEnd(m_material.Pass) : FString());
	return text;
}

}
