/*
** SectorUpdates.cpp
**
** Helion world events and the de-duplicated sector update list.
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

#include "SectorUpdates.h"

namespace Helion::World::SectorUpdates
{

void SectorMoveStart(sector_t* sec, int pos)
{
	SectorMoveStartEvent.Invoke(sec, pos);
}

void SectorMoveComplete(sector_t* sec, int pos)
{
	SectorMoveCompleteEvent.Invoke(sec, pos);
}

void PlaneTransformed(sector_t* sec, int pos)
{
	PlaneTransformedEvent.Invoke(sec, pos);
}

void PlaneTextureChanged(sector_t* sec, int pos, FTextureID newTexture, FTextureID previousTexture)
{
	PlaneTextureChangedEvent.Invoke(sec, pos, newTexture, previousTexture);
}

void SideTextureChanged(side_t* side, int which, FTextureID newTexture, FTextureID previousTexture)
{
	SideTextureChangedEvent.Invoke(side, which, newTexture, previousTexture);
}

void ClearSubscribers()
{
	SectorMoveStartEvent.Clear();
	SectorMoveCompleteEvent.Clear();
	PlaneTransformedEvent.Clear();
	PlaneTextureChangedEvent.Clear();
	SideTextureChangedEvent.Clear();
}

void SectorUpdateList::EnsureCapacity(unsigned capacity)
{
	if (m_updateLookup.Size() < capacity)
	{
		unsigned old = m_updateLookup.Size();
		m_updateLookup.Resize(capacity);
		for (unsigned i = old; i < capacity; i++)
			m_updateLookup[i] = 0;
	}
}

void SectorUpdateList::ClearAndReset()
{
	UpdateSectors.Clear();
	for (int& entry : m_updateLookup)
		entry = 0;
	m_counter = 1;
}

void SectorUpdateList::Add(sector_t* sector)
{
	const unsigned id = sector->Index();
	if (id >= m_updateLookup.Size())
		return;

	if (m_updateLookup[id] == m_counter)
		return;

	m_updateLookup[id] = m_counter;
	UpdateSectors.Push(sector);
}

void SectorUpdateList::Clear()
{
	UpdateSectors.Clear();
	m_counter++;
}

}
