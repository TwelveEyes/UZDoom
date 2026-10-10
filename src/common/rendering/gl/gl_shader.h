/*
** gl_shader.h
**
**
**
**---------------------------------------------------------------------------
**
** Copyright 2004-2016 Christoph Oelckers
** Copyright 2017-2025 GZDoom Maintainers and Contributors
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
*/

#ifndef __GL_SHADERS_H__
#define __GL_SHADERS_H__

#include "gl_renderstate.h"
#include "name.h"
#include "shaderuniforms.h"

extern bool gl_shaderactive;

struct HWViewpointUniforms;

namespace OpenGLRenderer
{
	class FShaderCollection;

//==========================================================================
//
//
//==========================================================================

class FUniform1i
{
	int mIndex;

public:
	void Init(GLuint hShader, const GLchar *name)
	{
		mIndex = glGetUniformLocation(hShader, name);
	}

	void Set(int newvalue)
	{
		glUniform1i(mIndex, newvalue);
	}
};

class FBufferedUniform1i
{
	int mBuffer;
	int mIndex;

public:
	void Init(GLuint hShader, const GLchar *name)
	{
		mIndex = glGetUniformLocation(hShader, name);
		mBuffer = 0;
	}

	void Set(int newvalue)
	{
		if (newvalue != mBuffer)
		{
			mBuffer = newvalue;
			glUniform1i(mIndex, newvalue);
		}
	}
};

class FBufferedUniform4i
{
	int mBuffer[4];
	int mIndex;

public:
	void Init(GLuint hShader, const GLchar *name)
	{
		mIndex = glGetUniformLocation(hShader, name);
		memset(mBuffer, 0, sizeof(mBuffer));
	}

	void Set(const int *newvalue)
	{
		if (memcmp(newvalue, mBuffer, sizeof(mBuffer)))
		{
			memcpy(mBuffer, newvalue, sizeof(mBuffer));
			glUniform4iv(mIndex, 1, newvalue);
		}
	}
};

class FBufferedUniform1f
{
	float mBuffer;
	int mIndex;

public:
	void Init(GLuint hShader, const GLchar *name)
	{
		mIndex = glGetUniformLocation(hShader, name);
		mBuffer = 0;
	}

	void Set(float newvalue)
	{
		if (newvalue != mBuffer)
		{
			mBuffer = newvalue;
			glUniform1f(mIndex, newvalue);
		}
	}
};

class FBufferedUniform2f
{
	float mBuffer[2];
	int mIndex;

public:
	void Init(GLuint hShader, const GLchar *name)
	{
		mIndex = glGetUniformLocation(hShader, name);
		memset(mBuffer, 0, sizeof(mBuffer));
	}

	void Set(const float *newvalue)
	{
		if (memcmp(newvalue, mBuffer, sizeof(mBuffer)))
		{
			memcpy(mBuffer, newvalue, sizeof(mBuffer));
			glUniform2fv(mIndex, 1, newvalue);
		}
	}

	void Set(float f1, float f2)
	{
		if (mBuffer[0] != f1 || mBuffer[1] != f2)
		{
			mBuffer[0] = f1;
			mBuffer[1] = f2;
			glUniform2fv(mIndex, 1, mBuffer);
		}
	}

};

class FBufferedUniform4f
{
	float mBuffer[4];
	int mIndex;

public:
	void Init(GLuint hShader, const GLchar *name)
	{
		mIndex = glGetUniformLocation(hShader, name);
		memset(mBuffer, 0, sizeof(mBuffer));
	}

	void Set(const float *newvalue)
	{
		if (memcmp(newvalue, mBuffer, sizeof(mBuffer)))
		{
			memcpy(mBuffer, newvalue, sizeof(mBuffer));
			glUniform4fv(mIndex, 1, newvalue);
		}
	}
};

class FUniform4f
{
	int mIndex;

public:
	void Init(GLuint hShader, const GLchar *name)
	{
		mIndex = glGetUniformLocation(hShader, name);
	}

	void Set(const float *newvalue)
	{
		glUniform4fv(mIndex, 1, newvalue);
	}

	void Set(float a, float b, float c, float d)
	{
		glUniform4f(mIndex, a, b, c, d);
	}

	void Set(PalEntry newvalue)
	{
		glUniform4f(mIndex, newvalue.r / 255.f, newvalue.g / 255.f, newvalue.b / 255.f, newvalue.a / 255.f);
	}

};

class FBufferedUniformPE
{
	FVector4PalEntry mBuffer;
	int mIndex;

public:
	void Init(GLuint hShader, const GLchar *name)
	{
		mIndex = glGetUniformLocation(hShader, name);
		mBuffer = 0;
	}

	void Set(const FVector4PalEntry &newvalue)
	{
		if (newvalue != mBuffer)
		{
			mBuffer = newvalue;
			glUniform4f(mIndex, newvalue.r, newvalue.g, newvalue.b, newvalue.a);
		}
	}
};

class FShader
{
	friend class FShaderCollection;
	friend class FGLRenderState;

	unsigned int hShader;
	unsigned int hVertProg;
	unsigned int hFragProg;
	FName mName;

	FBufferedUniform1f muDesaturation;
	FBufferedUniform1i muFogEnabled;
	FBufferedUniform1i muTextureMode;
	FBufferedUniform4f muLightParms;
	FBufferedUniform2f muClipSplit;
	FBufferedUniform1i muLightIndex;
	FBufferedUniform1i muBoneIndexBase;
	FBufferedUniformPE muFogColor;
	FBufferedUniform4f muDynLightColor;
	FBufferedUniformPE muObjectColor;
	FBufferedUniformPE muObjectColor2;
	FBufferedUniformPE muAddColor;
	FBufferedUniformPE muTextureBlendColor;
	FBufferedUniformPE muTextureModulateColor;
	FBufferedUniformPE muTextureAddColor;
	FUniform4f muGlowBottomColor;
	FUniform4f muGlowTopColor;
	FUniform4f muGlowBottomPlane;
	FUniform4f muGlowTopPlane;
	FUniform4f muGradientBottomPlane;
	FUniform4f muGradientTopPlane;
	FUniform4f muSplitBottomPlane;
	FUniform4f muSplitTopPlane;
	FUniform4f muDetailParms;
	FBufferedUniform1f muInterpolationFactor;
	FBufferedUniform1f muAlphaThreshold;
	FBufferedUniform2f muSpecularMaterial;
	FBufferedUniform1f muTimer;
#ifdef NPOT_EMULATION
	FBufferedUniform2f muNpotEmulation;
#endif

	// Ticket 03 (level-mesh): the levelmesh.vp-specific uniforms. Raw
	// locations: -1 on every non-levelmesh shader, where all Set* calls are
	// no-ops, so FGLRenderState::ApplyShader pushes the params through the
	// same code path unconditionally.
	struct LevelMeshUniforms
	{
		int sectorStateSlot = -1;	// uSectorStateSlot (int)
		int surfaceCount = -1;		// uSurfaceCount (int)
		int sectorCount = -1;		// uSectorCount (int)
		int levelSkyPos = -1;		// uLevelSkyPos (vec3)
		int lightMode = -1;		// uLightMode (int)
		int levelFogDensity = -1;	// uLevelFogDensity (float)
		int outsideFogDensity = -1;// uOutsideFogDensity (float)
		int outsideFog = -1;		// uOutsideFog (uint)
		int levelFlags3 = -1;		// uLevelFlags3 (uint)
		int levelFlags2 = -1;		// uLevelFlags2 (uint)
		int levelFlags = -1;		// uLevelFlags (uint)
		int skyfog = -1;		// uSkyfog (int)
		int cullDist = -1;		// uCullDist (float)
		int glCullColor = -1;		// uGlCullColor (vec4, 0-1)
		int rDistanceCullType = -1;// uRDistanceCullType (int)
		int fogMode = -1;		// uGlFogMode (int)
		int rVisibility = -1;		// uRVisibility (float)
		int rExtralight = -1;		// uRExtralight (int)
		int weaponLight = -1;		// uWeaponLight (int)
		int rFakeContrast = -1;	// uRFakeContrast (int)
		int wallHorizLight = -1;	// uWallHorizLight (int)
		int wallVertLight = -1;	// uWallVertLight (int)
		int insybox = -1;		// uInsybox (bool -> uniform1i)
		int uSurface = -1;		// samplerBuffer record pools, fixed units 12-14
		int uSectorState = -1;
		int uLightState = -1;

		void Init(GLuint hShader)
		{
			sectorStateSlot = glGetUniformLocation(hShader, "uSectorStateSlot");
			surfaceCount = glGetUniformLocation(hShader, "uSurfaceCount");
			sectorCount = glGetUniformLocation(hShader, "uSectorCount");
			levelSkyPos = glGetUniformLocation(hShader, "uLevelSkyPos");
			lightMode = glGetUniformLocation(hShader, "uLightMode");
			levelFogDensity = glGetUniformLocation(hShader, "uLevelFogDensity");
			outsideFogDensity = glGetUniformLocation(hShader, "uOutsideFogDensity");
			outsideFog = glGetUniformLocation(hShader, "uOutsideFog");
			levelFlags3 = glGetUniformLocation(hShader, "uLevelFlags3");
			levelFlags2 = glGetUniformLocation(hShader, "uLevelFlags2");
			levelFlags = glGetUniformLocation(hShader, "uLevelFlags");
			skyfog = glGetUniformLocation(hShader, "uSkyfog");
			cullDist = glGetUniformLocation(hShader, "uCullDist");
			glCullColor = glGetUniformLocation(hShader, "uGlCullColor");
			rDistanceCullType = glGetUniformLocation(hShader, "uRDistanceCullType");
			fogMode = glGetUniformLocation(hShader, "uGlFogMode");
			rVisibility = glGetUniformLocation(hShader, "uRVisibility");
			rExtralight = glGetUniformLocation(hShader, "uRExtralight");
			weaponLight = glGetUniformLocation(hShader, "uWeaponLight");
			rFakeContrast = glGetUniformLocation(hShader, "uRFakeContrast");
			wallHorizLight = glGetUniformLocation(hShader, "uWallHorizLight");
			wallVertLight = glGetUniformLocation(hShader, "uWallVertLight");
			insybox = glGetUniformLocation(hShader, "uInsybox");
			uSurface = glGetUniformLocation(hShader, "uSurface");
			uSectorState = glGetUniformLocation(hShader, "uSectorState");
			uLightState = glGetUniformLocation(hShader, "uLightState");
		}

		void Set(const LevelMeshDrawParams &p)
		{
			glUniform1i(sectorStateSlot, p.sectorStateSlot);
			glUniform1i(surfaceCount, p.surfaceCount);
			glUniform1i(sectorCount, p.sectorCount);
			glUniform3fv(levelSkyPos, 1, p.skyPos);
			glUniform1i(lightMode, p.lightMode);
			glUniform1f(levelFogDensity, p.fogDensity);
			glUniform1f(outsideFogDensity, p.outsideFogDensity);
			glUniform1ui(outsideFog, (GLuint)p.outsideFog);
			glUniform1ui(levelFlags3, p.flags3);
			glUniform1ui(levelFlags2, p.flags2);
			glUniform1ui(levelFlags, p.flags);
			glUniform1i(skyfog, p.skyfog);
			glUniform1f(cullDist, p.culldist);
			glUniform4fv(glCullColor, 1, p.cullColor);
			glUniform1i(rDistanceCullType, p.distanceCullType);
			glUniform1i(fogMode, p.fogMode);
			glUniform1f(rVisibility, p.visibility);
			glUniform1i(rExtralight, p.extralight);
			glUniform1i(weaponLight, p.weaponLight);
			glUniform1i(rFakeContrast, p.fakeContrast);
			glUniform1i(wallHorizLight, p.wallHorizLight);
			glUniform1i(wallVertLight, p.wallVertLight);
			glUniform1i(insybox, (int)p.insybox);
		}
	};

	LevelMeshUniforms lmU;

	int lights_index;
	int modelmatrix_index;
	int normalmodelmatrix_index;
	int texturematrix_index;

	int currentglowstate = 0;
	int currentgradientstate = 0;
	int currentsplitstate = 0;
	int currentcliplinestate = 0;
	int currentfixedcolormap = 0;
	bool currentTextureMatrixState = true;// by setting the matrix state to 'true' it is guaranteed to be set the first time the render state gets applied.
	bool currentModelMatrixState = true;

public:
	FShader(const char *name)
		: mName(name)
	{
		hShader = hVertProg = hFragProg = 0;
	}

	~FShader();

	// Ticket 03 (level-mesh): push one frame's levelmesh parameters into the
	// program. No-op on non-levelmesh shaders (all locations -1).
	void SetLevelMesh(const LevelMeshDrawParams &p) { lmU.Set(p); }

	bool Load(const char * name, const char * vert_prog_lump, const char * fragprog, const char * fragprog2, const char * light_fragprog, const char *defines, bool isGBuffer, AllShaderIndex type);

	bool Bind();
	unsigned int GetHandle() const { return hShader; }
};

//==========================================================================
//
// The global shader manager
//
//==========================================================================
class FShaderManager
{
public:
	FShaderManager();
	~FShaderManager();

	FShader *BindEffect(int effect, EPassType passType);
	FShader *Get(unsigned int eff, bool alphateston, EPassType passType);

	void SetActiveShader(FShader *sh);
	bool CompileNextShader();
private:

	FShader *mActiveShader = nullptr;
	TArray<FShaderCollection*> mPassShaders;
	int mCompilePass = 0;

	friend class FShader;
};

class FShaderCollection
{
	TArray<FShader*> mMaterialShaders;
	TArray<FShader*> mMaterialShadersNAT;
	FShader *mEffectShaders[MAX_EFFECTS];
	int mCompileState = 0, mCompileIndex = 0;
	EPassType mPassType;

	void Clean();

public:
	FShaderCollection(EPassType passType);
	~FShaderCollection();
	FShader *Compile(const char *ShaderName, const char *ShaderPath, const char *LightModePath, const char *shaderdefines, bool usediscard, EPassType passType, AllShaderIndex type);
	int Find(const char *mame);
	bool CompileNextShader();
	FShader *BindEffect(int effect);

	FShader *Get(unsigned int eff, bool alphateston)
	{
		// indices 0-2 match the warping modes, 3 no texture, the following are custom
		if (!alphateston && eff < SHADER_NoTexture && mCompileState == -1)
		{
			return mMaterialShadersNAT[eff];	// Non-alphatest shaders are only created for default, warp1+2 and brightmap. The rest won't get used anyway
		}
		if (eff < mMaterialShaders.Size())
		{
			return mMaterialShaders[eff];
		}
		return NULL;
	}
};

}
#endif
