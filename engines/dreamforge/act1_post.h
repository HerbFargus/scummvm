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

#ifndef DREAMFORGE_ACT1_POST_H
#define DREAMFORGE_ACT1_POST_H

namespace DreamForge {

class Logic;

// Act 1 entries of the per-act handler tables run around clicks and scripts
int act1_35af0(Logic &L, int layerId);                              // 0xdfac8, after a mask-0x10 layer
void act1_37540(Logic &L, int hotspotId);                           // 0xdf9a8, after a mask-4 hotspot
void act1_379c0(Logic &L, int layerId);                             // 0xdf9b8, after a mask-4 layer
void act1_371d0(Logic &L, int objectId);                            // 0xdf9c8, after a mask-4 object
void act1_3d200(Logic &L);                                          // 0xdf010, clicked on a puppy
void act1_3c970(Logic &L);                                          // 0xe1304, puppy 1 clicked / bark
int act1_46c50(Logic &L, int hotspotId);                            // 0xdf030, plain walk click filter
void act1_3c610(Logic &L);                                          // 0xdf92c, the held item is dropped
int act1_3c860(Logic &L, int hotspotId, int held);                  // 0xdf93c
int act1_3c890(Logic &L, int layerId, int held);                    // 0xdf94c
int act1_3c7e0(Logic &L, int hotspotId, int held);                  // 0xdf96c
int act1_3c820(Logic &L, int layerId, int held);                    // 0xdf97c
void act1_3c8e0(Logic &L, int *result, int arg, int layerId, int held);  // 0xdf90c
void act1_37e80(Logic &L, int hotspotId);                           // 0xdfa08
void act1_37e20(Logic &L, int layerId);                             // 0xdfa18
void act1_37e50(Logic &L, int objectId);                            // 0xdfa28

void act1_post_initVars(Logic &L);

} // End of namespace DreamForge

#endif // DREAMFORGE_ACT1_POST_H
