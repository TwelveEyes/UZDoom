/*
** SectorUpdates.h
**
** Helion world events (WorldBase.SectorMoveStart / SectorMoveComplete /
** SectorPlaneTransformed / SideTextureChanged / PlaneTextureChanged) as
** subscribable callback lists, plus the de-duplicated sector list of
** Helion.Render.SectorUpdates.
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

#include <functional>
#include <utility>
#include "tarray.h"
#include "r_defs.h"

namespace Helion::World::SectorUpdates
{

// Equivalent of a C# `event EventHandler<T>`: an ordered list of subscribers.
template<typename... Args>
class Event
{
public:
	using Handler = std::function<void(Args...)>;

	// Returns a token for Unsubscribe.
	int Subscribe(Handler handler)
	{
		int token = m_nextToken++;
		m_handlers.Push({ token, std::move(handler) });
		return token;
	}

	void Unsubscribe(int token)
	{
		for (unsigned i = 0; i < m_handlers.Size(); i++)
		{
			if (m_handlers[i].first == token)
			{
				m_handlers.Delete(i);
				return;
			}
		}
	}

	void Clear() { m_handlers.Clear(); }
	bool HasSubscribers() const { return m_handlers.Size() != 0; }

	void Invoke(Args... args)
	{
		for (unsigned i = 0; i < m_handlers.Size(); i++)
			m_handlers[i].second(args...);
	}

private:
	TArray<std::pair<int, Handler>> m_handlers;
	int m_nextToken = 1;
};

// A sector plane started / finished moving. pos = sector_t::floor or sector_t::ceiling.
inline Event<sector_t*, int> SectorMoveStartEvent;
inline Event<sector_t*, int> SectorMoveCompleteEvent;
// A plane's texture transform (offset, scale, angle, base) or geometry changed. pos == -1: both planes.
inline Event<sector_t*, int> PlaneTransformedEvent;
// (sector, pos, new texture, previous texture)
inline Event<sector_t*, int, FTextureID, FTextureID> PlaneTextureChangedEvent;
// (side, side_t::top/mid/bottom, new texture, previous texture)
inline Event<side_t*, int, FTextureID, FTextureID> SideTextureChangedEvent;

// Drops every subscriber; called when the renderer is torn down.
void ClearSubscribers();

// Port of Helion.Render.SectorUpdates: a sector list that holds each sector once until cleared.
class SectorUpdateList
{
public:
	TArray<sector_t*> UpdateSectors;

	void EnsureCapacity(unsigned capacity);
	void ClearAndReset();
	void Add(sector_t* sector);
	void Clear();

private:
	TArray<int> m_updateLookup;
	int m_counter = 1;
};

}
