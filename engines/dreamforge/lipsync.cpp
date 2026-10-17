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

#include "common/debug.h"
#include "common/textconsole.h"

#include "dreamforge/detection.h"
#include "dreamforge/dfa.h"
#include "dreamforge/dreamforge.h"
#include "dreamforge/lipsync.h"
#include "dreamforge/resource.h"
#include "dreamforge/sound.h"

namespace DreamForge {

// C.PRG 0x16340: frames in each animation slot
static const int8 slotFrames[kActorAnimSlots] = {
	10, 2, 3, 4, 3, 1, 5, 5, 14, 6, 18, 6, 24, 22, 6, 12, 9,
	5, 6, 1, 4, 18, 19, 9, 14, 10, 6, 1, 4, 3, 6, 13, 8
};

uint actorAnimIndex(int actor, int slot, int view) {
	return (actor - 1) * kActorAnimSlots * kActorViews + slot * kActorViews + view;
}

int actorAnimFrames(int slot) {
	return slot >= 0 && slot < kActorAnimSlots ? slotFrames[slot] : 0;
}

int talkSlot(bool stateA, bool stateB, bool quick) {
	if (stateA)
		return quick ? 0x12 : 0x1A;
	if (stateB)
		return quick ? 0x0B : 0x0E;
	return quick ? 0x09 : 0x1E;
}

AnimFrames::AnimFrames() {
	memset(palette, 0, sizeof(palette));
}

AnimFrames::~AnimFrames() {
	clear();
}

void AnimFrames::clear() {
	for (uint i = 0; i < frames.size(); i++) {
		frames[i]->free();
		delete frames[i];
	}
	frames.clear();
}

bool AnimFrames::load(ResourceArchive *archive, uint index) {
	clear();
	Resource *res = archive ? archive->getResource(index) : nullptr;
	if (!res || res->type != kResAnimation) {
		delete res;
		return false;
	}
	uint32 size = res->body->readUint32LE();
	DFADecoder dfa;
	bool ok = dfa.load(res->body->readStream(size));
	delete res;
	if (!ok)
		return false;
	while (const Graphics::Surface *s = dfa.decodeNextFrame()) {
		Graphics::Surface *copy = new Graphics::Surface();
		copy->copyFrom(*s);
		frames.push_back(copy);
	}
	memcpy(palette, dfa.getPalette(), sizeof(palette));
	return !frames.empty();
}

bool LipSync::load(DreamForgeEngine *vm, int voiceId, Common::Array<byte> &visemes) {
	visemes.clear();
	ResourceArchive *pig6 = vm->getArchive(6);
	int index = vm->getSound()->voiceIndex(voiceId);
	if (!pig6 || index < 0 || (uint)index >= pig6->size())
		return false;
	Resource *res = pig6->getResource(index);
	if (!res || res->type != kResLipSync) {
		delete res;
		return false;
	}
	uint32 n = res->body->readUint32LE();
	for (uint32 i = 0; i < n && !res->body->eos(); i++)
		visemes.push_back(res->body->readByte());
	delete res;
	debugC(1, kDebugSound, "lip sync for voice %d (PIG6 #%d): %u visemes", voiceId, index, visemes.size());
	return true;
}

} // End of namespace DreamForge
