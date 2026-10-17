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

#ifndef DREAMFORGE_ACT1_CLICK_H
#define DREAMFORGE_ACT1_CLICK_H

namespace DreamForge {

class Logic;

// Act 1 click handler (C.PRG 0x43d60, table 0xdf020); 0 = the click was handled
int act1_43d60(Logic &L, int objectId, int layerId, int hotspotId);
void act1_click_initVars(Logic &L);

} // End of namespace DreamForge

#endif // DREAMFORGE_ACT1_CLICK_H
