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

#ifndef DREAMFORGE_ACT1_USE_H
#define DREAMFORGE_ACT1_USE_H

namespace DreamForge {

class Logic;

// Act 1 "use" handlers (mask 0x20 cursor), run after the puppy has walked to the thing.
// Each gets the clicked record's global id; the caller (0x2a950 / 0x2ae80 / 0x2b360)
// returns the handler's result, which is always 0.
int act1_37ea0(Logic &L, int layerId);    // layers, table 0xdfb28 (via 0x2a950)
int act1_3bed0(Logic &L, int hotspotId);  // hotspots, table 0xdfb38 (via 0x2ae80)
int act1_3c5b0(Logic &L, int objectId);   // objects, table 0xdfb48 (via 0x2b360)

void act1_use_initVars(Logic &L);

} // End of namespace DreamForge

#endif // DREAMFORGE_ACT1_USE_H
