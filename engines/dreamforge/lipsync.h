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

#ifndef DREAMFORGE_LIPSYNC_H
#define DREAMFORGE_LIPSYNC_H

#include "common/array.h"
#include "common/str.h"
#include "graphics/surface.h"

namespace DreamForge {

class DreamForgeEngine;
class ResourceArchive;

enum {
	kActorAnimSlots = 33,    // animation slots per puppy
	kActorViews = 5,         // drawn views per slot (the rest are mirrored)
	kLipSyncStepMs = 100     // one viseme per game step (see LipSync)
};

/**
 * The puppies' animations are the DFAs in PIG2: 2 actors x 33 slots x 5 views.
 * Frames per slot are in C.PRG's table at 0x16340.
 */
uint actorAnimIndex(int actor, int slot, int view);
int actorAnimFrames(int slot);

/**
 * The talking animation for a line (C.PRG 0x1bd20). Every one has 6 frames.
 * stateA/stateB are two per-actor state flags the game keeps (at 0x6480c and
 * 0x64814; their meaning isn't known yet), quick is the call's third argument.
 */
int talkSlot(bool stateA, bool stateB, bool quick);

/** Every frame of a PIG2 animation, decoded up front (the frames are deltas) */
struct AnimFrames {
	Common::Array<Graphics::Surface *> frames;
	byte palette[256 * 3];

	AnimFrames();
	~AnimFrames();
	bool load(ResourceArchive *archive, uint index);
	void clear();
};

/**
 * Lip sync (C.PRG 0x1bd20): PIG6 entry n goes with voice n in PIG3, for the
 * 801 voices before the 20000 bank. It's a string of visemes, one per game
 * step: viseme v shows frame v - 1 of the talking animation and 0 ends it.
 * The game walks the string while the voice plays, so a line stops moving
 * when its sound ends. One step is two ticks of the game timer; the data
 * averages 10.2 visemes per second of speech, so steps are 100 ms here.
 */
class LipSync {
public:
	/** Visemes for a voice number (11000, 20000...), empty if it has none */
	static bool load(DreamForgeEngine *vm, int voiceId, Common::Array<byte> &visemes);
};

} // End of namespace DreamForge

#endif // DREAMFORGE_LIPSYNC_H
