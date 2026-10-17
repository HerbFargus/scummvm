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

#ifndef DREAMFORGE_ACT2_H
#define DREAMFORGE_ACT2_H

namespace DreamForge {

class Logic;

/**
 * Act 2 object-update hook (table 0xdf05c). objectId = struct +0x298,
 * frame = struct +0x2b4, frameCount = the DFA header's frame count
 * (word at [struct +0x2b0] + 6).
 */
void act2_48730(Logic &L, int objectId, int frame, int frameCount);

/** Act 2 idle hook (table 0x6de38) */
void act2_48a80(Logic &L);

/** Act 2 fidget veto (table 0x88370): true if puppy g may fidget */
bool act2_48b40(Logic &L, int g);

/** Act 2 scene entry (table 0x88384) */
void act2_48bd0(Logic &L, int room, int area);

/** Act 2 after-load handler (table 0x88394) */
void act2_48c30(Logic &L, int room, int area);

void act2_initVars(Logic &L);

} // End of namespace DreamForge

#endif // DREAMFORGE_ACT2_H
