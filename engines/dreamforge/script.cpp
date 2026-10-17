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
#include "common/endian.h"
#include "common/textconsole.h"

#include "dreamforge/actor.h"
#include "dreamforge/detection.h"
#include "dreamforge/dreamforge.h"
#include "dreamforge/script.h"
#include "dreamforge/sound.h"

namespace DreamForge {

enum ScriptOp {
	kOpEnd = 0x00,
	kOpSetFlag = 0x01,
	kOpClearFlag = 0x02,
	kOpToggleFlag = 0x03,
	kOpIfFlag = 0x04,
	kOpBusyOn = 0x05,
	kOpBusyOff = 0x06,
	kOpStopActor = 0x07,
	kOpSetActor = 0x08,
	kOpPlaceActor = 0x09,
	kOpSetAnim = 0x0A,
	kOpWalkTo = 0x0B,
	kOpPose = 0x0E,
	kOpCinematic = 0x0F,
	kOpCutscene = 0x10,
	kOpBark = 0x11,
	kOpSay = 0x12,
	kOpVoice = 0x13,
	kOpMusic = 0x14,
	kOpSfx = 0x15,
	kOpFadeSlot = 0x16,
	kOpStopSlot = 0x17,
	kOpStopMusic = 0x18,
	kOpChangeScene = 0x19,
	kOpIfRandom = 0x2E,
	kOpEndBlock = 0x2F,
	kOpSetHeld = 0x32,
	kOpNop33 = 0x33,
	kOpNop35 = 0x35,
	kOpIfVisits = 0x36,
	kOpHide = 0x37,
	kOpShow = 0x38,
	kOpIfHeld = 0x39,
	kOpFaceHotspot = 0x3A,
	kOpGoto = 0x3B,
	kOpCount3D = 0x3D,
	kOpFaceEachOther = 0x3F
};

ScriptRunner::ScriptRunner(DreamForgeEngine *vm) : _vm(vm) {
}

// Compare modes of 0x2e, 0x36 and 0x39: the jump is taken when this holds
bool ScriptRunner::compare(uint mode, int value, int against) const {
	switch (mode) {
	case 0: return against <= value;
	case 1: return value <= against;
	case 2: return against < value;
	case 3: return value < against;
	case 4: return against != value;
	case 5: return against == value;
	default: return false;
	}
}

void ScriptRunner::run(const byte *script, int visits, const Common::Point &centre) {
	// The first word counts the instructions, but the original ignores it and runs to an end marker
	uint count = kScriptMaxInstrs;
	const byte *code = script + 2;
	uint pc = 0;
	Sound *sound = _vm->getSound();

	while (pc < count && !_vm->shouldQuit()) {
		const byte *ins = code + pc * kScriptInstrSize;
		byte op = ins[0];
		uint16 a[10];
		for (uint i = 0; i < 10; i++)
			a[i] = READ_LE_UINT16(ins + 1 + i * 2);
		debugC(2, kDebugScript, "  %2u: op %02x %u %u %u %u", pc, op, a[0], a[1], a[2], a[3]);

		int jump = -1;
		bool stop = false;
		switch (op) {
		case kOpEnd:
		case kOpEndBlock:
			stop = true;
			break;
		case kOpSetFlag:
			_vm->setFlag(a[0], true);
			break;
		case kOpClearFlag:
			_vm->setFlag(a[0], false);
			break;
		case kOpToggleFlag:
			_vm->setFlag(a[0], !_vm->isFlagSet(a[0]));
			break;
		case kOpIfFlag:
			// a1 = 0: jump if the flag is set; otherwise jump if it's clear
			if (a[1] == 0 ? _vm->isFlagSet(a[0]) : !_vm->isFlagSet(a[0]))
				jump = a[2];
			break;
		case kOpBusyOn:
		case kOpBusyOff:
		case kOpNop33:
		case 0x34:
		case kOpNop35:
			break;
		case kOpSetActor:
			_vm->_currentActor = a[0];
			break;
		case kOpStopActor:
			for (int n = 1; n <= 2; n++)
				if (a[0] == n || a[0] == 3)
					_vm->getActor(n)->stopWalking();
			break;
		case kOpPlaceActor:
			if (Actor *act = _vm->getActor(a[0]))
				act->place(a[1], a[2], a[3]);
			break;
		case kOpSetAnim:
			// 0x169d0: set the facing straight away
			for (int n = 1; n <= 2; n++)
				if (a[0] == n || a[0] == 3)
					_vm->getActor(n)->setDirection(a[1]);
			break;
		case kOpWalkTo:
			_vm->actorWalk(a[0], a[1], a[2], a[3] < 8 ? a[3] : -1);
			break;
		case kOpPose:
			if (Actor *act = _vm->getActor(a[0])) {
				// TODO: the posture changes 0x345a0 makes before some slots
				if (a[2] < 8)
					_vm->actorTurn(*act, a[2]);
				_vm->actorPlay(*act, a[1]);
			}
			break;
		case kOpBark:
			if (Actor *act = _vm->getActor(a[0]))
				_vm->actorPlay(*act, act->_posture == kLying ? kSlotLieBark : kSlotBark);
			break;
		case kOpHide:
		case kOpShow:
			for (int n = 1; n <= 2; n++)
				if (a[0] == n || a[0] == 3)
					_vm->getActor(n)->_hidden = (op == kOpHide);
			break;
		case kOpFaceHotspot:
			for (int n = 1; n <= 2; n++)
				if (a[0] == n || a[0] == 3) {
					Actor *act = _vm->getActor(n);
					_vm->actorTurn(*act, act->directionTo(centre.y, centre.x));
				}
			break;
		case kOpFaceEachOther: {
			Actor *p1 = _vm->getActor(1), *p2 = _vm->getActor(2);
			_vm->actorTurn(*p1, p1->directionTo(p2->_y, p2->_x));
			_vm->actorTurn(*p2, p2->directionTo(p1->_y, p1->_x));
			break;
		}
		case kOpCinematic:
			// cine, top, left, transparent, delay, depth, don't wait, puppy to hide meanwhile
			_vm->playSceneAnim(a[0], (int16)a[1], (int16)a[2], a[3] != 0, a[4], (int16)a[5], a[6] != 0, a[7]);
			break;
		case kOpCutscene:
			_vm->playCutscene(a[0]);
			break;
		case kOpSay:
			_vm->actorSay(a[0], a[1], a[2] != 0);
			break;
		case kOpVoice:
			sound->playVoice(a[0]);
			break;
		case kOpMusic:
			sound->playMusic(a[0], true);
			break;
		case kOpSfx:
			sound->playSfx(a[0], a[3] % kSfxChannels, a[1] != 0);
			break;
		case kOpFadeSlot:
		case kOpStopSlot:
			if (a[0])
				sound->stopSfx(a[0] % kSfxChannels);
			break;
		case kOpStopMusic:
			sound->stopMusic();
			break;
		case kOpChangeScene:
			// 0x1f800: the puppies' new (y, x, facing), then the scene
			_vm->getActor(1)->place(a[0], a[1], a[2]);
			_vm->getActor(2)->place(a[3], a[4], a[5]);
			_vm->loadScene(a[6], a[7], a[8]);
			break;
		case kOpIfRandom: {
			int r = (_vm->getRandom15() * MAX<int>(a[0], 1)) >> 15;
			if (compare(a[1], r, a[2]))
				jump = a[3];
			break;
		}
		case kOpSetHeld:
			_vm->_heldItem = a[0];
			break;
		case kOpIfVisits:
			if (compare(a[0], visits, a[1]))
				jump = a[2];
			break;
		case kOpIfHeld:
			if (compare(a[0], _vm->_heldItem, a[1]))
				jump = a[2];
			break;
		case kOpGoto:
			jump = a[0];
			break;
		case kOpCount3D:
			_vm->_counter3D++;
			break;
		default:
			// TODO: puppy movement, poses, objects and the other engine calls
			debugC(1, kDebugScript, "script op %02x not done yet (%u %u %u %u)", op, a[0], a[1], a[2], a[3]);
			break;
		}
		if (stop)
			break;
		if (jump >= 0) {
			// Landing on an end marker carries on after it
			pc = jump;
			if (pc < count && (code[pc * kScriptInstrSize] == kOpEnd || code[pc * kScriptInstrSize] == kOpEndBlock))
				pc++;
		} else {
			pc++;
		}
	}
}

} // End of namespace DreamForge
