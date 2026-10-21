/*
** r_interpolate.h
**
** Movement interpolation
**
**---------------------------------------------------------------------------
**
** Copyright 2008-2016 Christoph Oelckers
** Copyright 2017-2025 GZDoom Maintainers and Contributors
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** Code written prior to 2026 is also licensed under:
**
** SPDX-License-Identifier: BSD-3-Clause
**
**---------------------------------------------------------------------------
**
*/

#ifndef R_INTERPOLATE_H
#define R_INTERPOLATE_H

#include "dobject.h"

struct FLevelLocals;
struct sector_t;

//==========================================================================
//
//
//
//==========================================================================

class DInterpolation : public DObject
{
	friend struct FInterpolator;

	DECLARE_ABSTRACT_CLASS(DInterpolation, DObject)
	HAS_OBJECT_POINTERS

	TObjPtr<DInterpolation*> Next;
	TObjPtr<DInterpolation*> Prev;

protected:
	FLevelLocals *Level;
	int refcount = 0;

	DInterpolation(FLevelLocals *l = nullptr) : Level(l) {}

public:
	int AddRef();
	int DelRef(bool force = false);

	virtual void UnlinkFromMap();
	virtual void UpdateInterpolation() = 0;
	virtual void Restore() = 0;
	virtual void Interpolate(double smoothratio) = 0;
	// The sector whose plane this interpolation moves (sector plane interpolations only).
	virtual sector_t *MovedSector() const { return nullptr; }
	// Called by DoInterpolations instead of Interpolate for sector planes, which the renderer interpolates on the GPU:
	// only removes a finished interpolation, like Interpolate does.
	virtual void InterpolationSkipped() {}

	virtual void Serialize(FSerializer &arc);
};

//==========================================================================
//
//
//
//==========================================================================

struct FInterpolator
{
	TObjPtr<DInterpolation*> Head = MakeObjPtr<DInterpolation*>(nullptr);
	bool didInterp = false;
	int count = 0;

	int CountInterpolations ();

public:
	void UpdateInterpolations();
	void AddInterpolation(DInterpolation *);
	void RemoveInterpolation(DInterpolation *);
	// Sector plane interpolations are left alone (the renderer interpolates them on the GPU from
	// R_GetPlaneInterpolationOldTexZ); everything else (scrollers, polyobjects) is interpolated.
	void DoInterpolations(double smoothratio);
	void RestoreInterpolations();
	void ClearInterpolations();
	// Calls 'func' with the sector of every sector plane interpolation.
	template<class Func> void ForEachMovedSector(Func func)
	{
		for (DInterpolation *probe = Head; probe != nullptr; probe = probe->Next)
		{
			if (sector_t *sec = probe->MovedSector()) func(sec);
		}
	}
};

// Previous-tic texture Z (sector_t::GetPlaneTexZ) of a sector plane that has a plane interpolation
// (pos = sector_t::floor / sector_t::ceiling). Returns false when the plane is not interpolated.
bool R_GetPlaneInterpolationOldTexZ(sector_t *sector, int pos, double &oldtexz);


#endif
