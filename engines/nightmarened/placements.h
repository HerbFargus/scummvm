/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */


#ifndef NIGHTMARENED_PLACEMENTS_H
#define NIGHTMARENED_PLACEMENTS_H

#include "common/array.h"

#include "nightmarened/data.h"

namespace NightmareNed {

class Game;
struct BitmapRec;

/** The bitmap record a kind 0/1 placement names: the DAT_004ddb88 entry at va, or nullptr */
typedef BitmapRec *(*PlacementBitmapLookup)(void *user, uint32 va);

/** 0x45a560 sst_load: "%02d%03d0%02d.sst" from the area prefix 0x4afcd8[areaIndex], scene and n */
bool loadSceneSst(Game *game, int areaIndex, int scene, int n, Common::Array<Placement> &list);

/**
 * 0x43e460, the olist_foreach callback that applies a placement: a kind 2 entry sets
 * an area 1..4 slot's position, order, offsets, colour key and mirror flag; a kind 0/1
 * entry sets the position and order of the bitmap record DAT_004ddb88[(index * 15 +
 * slot) * 100 + sub], where index is the area's (0x45a440) and lookup finds it by address.
 */
void applyPlacement(Game *game, const Placement &e, PlacementBitmapLookup lookup, void *user);

enum {
	kPlacementBitmapTable = 0x4ddb88  // DAT_004ddb88
};

} // End of namespace NightmareNed

#endif // NIGHTMARENED_PLACEMENTS_H
