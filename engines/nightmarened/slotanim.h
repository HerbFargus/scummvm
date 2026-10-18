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

#ifndef NIGHTMARENED_SLOTANIM_H
#define NIGHTMARENED_SLOTANIM_H

#include "common/scummsys.h"

namespace NightmareNed {

class Game;

/*
 * Generic slot animation helpers that several scenes keep their own copies of (the
 * exe has identical functions at several addresses). Placement is slot_place_nx
 * (0x430530): no xform, not relative, not mirrored.
 */

/**
 * 0x41d330 / 0x479e20: before the deadline the slot is placed again; after it the step
 * advances and is placed, with the deadline now + (duration or the step's) * 41. Returns
 * false, leaving the step at the sequence length, when the sequence has ended.
 */
bool slotStep(Game *game, int area, int id, int ref, uint32 now, int16 duration);

/**
 * 0x41d400 / 0x479ef0: slot_switch(newSlot, oldSlot); a positive newSlot starts at step
 * (a random step when negative), is placed against ref and gets its deadline. Returns newSlot.
 */
int16 slotStart(Game *game, int area, int newSlot, int oldSlot, int ref, uint32 now, int16 step);

/** 0x479fb0: moves the slot's stored position and its sprite by (dx, dy) */
void slotNudge(Game *game, int area, int id, int dx, int dy);

/** 0x41e060: the slot's sprite visibility, then its draw order */
void slotShow(Game *game, int area, int id, int z, bool visible);

} // End of namespace NightmareNed

#endif // NIGHTMARENED_SLOTANIM_H
