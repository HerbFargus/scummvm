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

#include "nightmarened/detection.h"
#include "nightmarened/game.h"
#include "nightmarened/ned.h"
#include "nightmarened/rdx.h"
#include "nightmarened/world.h"

namespace NightmareNed {

enum {
	kNedArea = 1,
	kNedNaf = 0,              // file index of ned00000.naf
	kNedZ = 8,                // DAT_004c56d4

	kFrameEventTable = 0x4a5578,
	kPaletteList = 0x4a5c20,
	kFidgetList = 0x4a63e0,
	kListTable = 0x4afb98,
	kListCounts = 0x4afbf8,
	kActionTable = 0x4aeb68,
	kGlobals = 0x4a5548,

	// state flags: low byte from the record, then the phase, then the run-time modifiers
	kStateFree = 0x01,        // may be left at any time
	kStateToEnd = 0x02,       // may be left only from idle or at the last step of a turn
	kStateStep0 = 0x08,       // may be left only at step 0
	kPhaseHold = 0x20,
	kPhaseEnd = 0x40,
	kPhaseTail = 0x80,
	kPhaseLoop = 0x100,
	kPhaseStart = 0x200,
	kPhaseMask = 0x3e0,
	kStateRelease = 0x10000000, // leave the loop or hold for the end phase
	kStateLastStep = 0x20000000, // the loop stops on its last step
	kStateDefer = 0x40000000,   // a leave request sets kStateLastStep instead

	// slot flags
	kSlotBackwards = 0x01,
	kSlotNoFlip = 0x02,
	kSlotNoProbe = 0x10,      // no floor probe while moving
	kSlotLanding = 0x20,

	kStateUse = 0,
	kStateWalk = 1,
	kStateFidget = 3,
	kStateIdle = 4,
	kStateClimbUp = 5,
	kStateClimbDown = 6,
	kStateStairsUp = 7,
	kStateStairsDown = 8,
	kStateButton1 = 9,
	kStateButton3 = 10,
	kStateRegion1 = 0xb,
	kStateTurn = 0xc,
	kStateDuck = 0xd,
	kStateRegion2 = 0xe,
	kStateAction = 0xf,
	kStateHangRight = 0x10,
	kStateHangLeft = 0x11,
	kStateFall = 0x12,
	kStateRegion3 = 0x13,
	kStateWalkTurn = 0x14,
	kStateWalkStart = 0x15,
	kStateDrop = 0x16
};

static const int32 kNone = -0x7fff;

Ned::Ned(Game *game) : _game(game), _fidgetLoaded(false), _state(kStateIdle), _idleTime(1), _fidgetFixed(-1),
	_gravity(1), _costume(1), _sideStep(6), _enabled(true), _regionFlags(0), _regionId(-1), _tickUnit(0x21),
	_wrapMode(0), _wrapWidth(0), _tileW(0), _tiles(0), _wrapUnused(0), _xform(nullptr), _xformUser(nullptr),
	_anchorX(0), _anchorY(0), _halfW(0), _baseW(0), _baseH(0), _holdStart(0), _z(kNedZ), _lastSound(0),
	_facing(1), _fidget(0), _onFinish(nullptr), _onFinishUser(nullptr), _onAction(nullptr), _onActionUser(nullptr),
	_action(-1), _hitTarget(0), _pending(false), _landed(false), _fidgetShown(false), _curSlot(0), _hanging(false),
	_fidgetRotate(false), _soundTime(0), _upHeld(false), _downHeld(false), _randomSeed(1) {
	memset(_palette, 0, sizeof(_palette));
	memset(_saved3, 0, sizeof(_saved3));
	memset(_states, 0, sizeof(_states));
	memset(_matrix, 0, sizeof(_matrix));
	memset(_enterHooks, 0, sizeof(_enterHooks));
	memset(_tickHooks, 0, sizeof(_tickHooks));
	memset(_frameEvents, 0, sizeof(_frameEvents));
	clearRegionCache();
}

Ned::~Ned() {
	_game->freeList(_fidgetList);
	for (int i = kListCount - 1; i >= 0; i--)
		_game->freeList(_lists[i]);
	_game->freeList(_paletteList);
}

Slot &Ned::slot(int id) {
	return _game->res().getSlot(kNedArea, CLIP(id, 0, (int)kAreaSlots - 1));
}

void Ned::readState(State &st, uint32 va) {
	const ExeImage &exe = _game->exe();
	st.flags = exe.readUint32(va);
	for (int i = 0; i < 28; i++)
		st.h[i] = (int16)exe.readUint16(va + 4 + 2 * i);
}

void Ned::readMatrixRow(int row, uint32 va) {
	for (int i = 0; i < kStateCount; i++)
		_matrix[row * kStateCount + i] = _game->exe().readUint32(va + 4 * i);
}

// 0x44f680
void Ned::clearRegionCache() {
	_regionFlags = 0;
	_regionId = -1;
	for (int i = 0; i < 4; i++)
		_regionRect[i] = kNone;
}

void Ned::clearRegion() {
	clearRegionCache();
}

void Ned::storeRegion(const int32 rect[4], uint32 flags, int32 id) {
	for (int i = 0; i < 4; i++)
		_regionRect[i] = rect[i];
	_regionFlags = flags;
	_regionId = id;
}

// 0x44f6b0
void Ned::getRegion(uint32 &flags, int32 &id, int32 rect[4]) const {
	flags = _regionFlags;
	id = _regionId;
	for (int i = 0; i < 4; i++)
		rect[i] = _regionRect[i];
}

void Ned::hideSlot(int16 id) {
	Slot &s = slot(id);
	if (s.sprite)
		s.sprite->setVisibility(false);
}

// 0x48c5e0: the MSVC rand()
uint32 Ned::nextRandom() {
	_randomSeed = _randomSeed * 0x343fd + 0x269ec3;
	return (_randomSeed >> 16) & 0x7fff;
}

// 0x486930: the outputs are left alone outside the sequence
bool Ned::seqStep(Slot &s, int step, int &frame, int &duration) {
	int f, d;
	if (!s.sequence || !s.sequence->getStep(step, f, d))
		return false;
	frame = f;
	duration = d;
	return true;
}

// 0x485700
void Ned::boxAnchor(Slot &s, int &x, int &y) {
	if (!s.boxes)
		return;
	x = s.boxes->anchorX;
	y = s.boxes->anchorY;
}

// 0x485450 (not the mirrored pair)
void Ned::boxFrameOffset(Slot &s, int frame, int &x, int &y) {
	if (!s.boxes || frame < 0 || frame >= (int)s.boxes->frames.size())
		return;
	x = s.boxes->frames[frame].offX;
	y = s.boxes->frames[frame].offY;
}

// 0x4860d0
void Ned::boxRect(Slot &s, int frame, int k, int32 out[4]) {
	if (s.boxes) {
		s.boxes->getRawRect(frame, k, out);
	} else {
		for (int i = 0; i < 4; i++)
			out[i] = kNone;
	}
}

// 0x451270: one step in the slot's direction; true when it ran off the end
bool Ned::advanceSlot(Slot &s) {
	if (!(s.flags & kSlotBackwards)) {
		s.step++;
		return s.sequenceLength <= s.step;
	}
	s.step--;
	return s.step < 0;
}

// 0x451230
void Ned::rewindSlot(Slot &s) {
	s.step = (s.flags & kSlotBackwards) ? s.sequenceLength - 1 : 0;
}

// 0x4511f0
bool Ned::atLastStep(Slot &s) {
	if (!(s.flags & kSlotBackwards))
		return s.sequenceLength - 1 <= s.step;
	return s.step < 1;
}

// 0x4518e0: steps the state's slot and moves on through its phases' slot lists
int Ned::stepState(int16 state, uint32 now, int &finished) {
	finished = 0;
	State &st = _states[state];
	uint32 fl = st.flags;
	int16 *h = st.h;
	bool toEnd = false;

	switch (fl & kPhaseMask) {
	case kPhaseEnd:
	case kPhaseTail: {
		int idxField = (fl & kPhaseMask) == kPhaseEnd ? kEndIdx : kTailIdx;
		int listField = (fl & kPhaseMask) == kPhaseEnd ? kEnd : kTail;
		if (!advanceSlot(slot(h[kSlot])))
			break;
		int16 i = h[idxField];
		if (i > 4 || h[listField + i + 1] == -1) {
			finished = 1;
			break;
		}
		h[idxField] = i + 1;
		h[kSlot] = h[listField + i + 1];
		rewindSlot(slot(h[kSlot]));
		break;
	}

	case kPhaseHold: {
		Slot &s = slot(h[kSlot]);
		bool done = false;
		if (s.step != h[kHoldStep] || (fl & kStateRelease))
			done = advanceSlot(s);
		if (!done) {
			if (!_holdStart)
				_holdStart = now;
			break;
		}
		_holdStart = 0;
		toEnd = true;
		break;
	}

	case kPhaseLoop: {
		Slot &s = slot(h[kSlot]);
		if ((fl & kStateLastStep) && atLastStep(s))
			break;
		if ((fl & kStateRelease) && ((fl & kStateFree) || s.step == 0)) {
			toEnd = true;
			break;
		}
		if (!advanceSlot(s))
			break;
		if (!(fl & kStateRelease)) {
			if (h[kLoopIdx] < 1) {
				if (h[kLoop + 1] != -1)
					h[kLoopIdx] = 1;
			} else {
				h[kLoopIdx] = 0;
			}
			h[kSlot] = h[kLoop + h[kLoopIdx]];
			rewindSlot(slot(h[kSlot]));
			break;
		}
		toEnd = true;
		break;
	}

	case kPhaseStart: {
		if (!advanceSlot(slot(h[kSlot])))
			break;
		int16 i = h[kStartIdx];
		if (i < 5 && h[kStart + i + 1] != -1) {
			h[kStartIdx] = i + 1;
			h[kSlot] = h[kStart + i + 1];
		} else if (h[kLoop] != -1 && !(fl & kStateRelease)) {
			h[kLoopIdx] = 0;
			h[kSlot] = h[kLoop];
			st.flags = (st.flags & 0xfffffd1f) | kPhaseLoop;
		} else if (h[kHold] != -1) {
			h[kHoldStep] = 0; // entering the hold from the start rewinds its step
			h[kSlot] = h[kHold];
			st.flags = (st.flags & 0xfffffc3f) | kPhaseHold;
		} else {
			toEnd = true;
			break;
		}
		rewindSlot(slot(h[kSlot]));
		break;
	}

	default:
		break;
	}

	if (toEnd) {
		if (h[kEnd] != -1) {
			h[kEndIdx] = 0;
			h[kSlot] = h[kEnd];
			st.flags = (st.flags & 0xfffffc5f) | kPhaseEnd;
			rewindSlot(slot(h[kSlot]));
		} else if (h[kTail] != -1) {
			h[kTailIdx] = 0;
			h[kSlot] = h[kTail];
			st.flags = (st.flags & 0xfffffc9f) | kPhaseTail;
			rewindSlot(slot(h[kSlot]));
		} else {
			finished = 1;
		}
	}

	Slot &s = slot(h[kSlot]);
	int frame = 0, duration = 0;
	seqStep(s, s.step, frame, duration);
	s.deadline = (int16)duration * _tickUnit + now;
	frameEvent(h[kSlot], s.step, state);
	return 1;
}

// 0x455880 with the previous Ned sound (DAT_004c5734) as the one to stop
void Ned::playSound(int16 slotId) {
	debugC(1, kDebugSound, "Ned sound %d (stops %d)", slotId, _lastSound);
	_game->playSlotSound(slot(slotId), &slot(_lastSound));
	_lastSound = slotId;
}

// The 0x4a5578 frame-event table: table[slot](slot, step, state) after every step
void Ned::frameEvent(int16 id, int16 step, int16 state) {
	uint32 fn = (id >= 0 && id < kSlotCount) ? _frameEvents[id] : 0;
	switch (fn) {
	case 0x451370:
		// The default: the slot's own sound on step 0
		if (step == 0 && !slot(id).sound.empty())
			playSound(id);
		break;
	case 0x451330:
		if (step == 0 && !slot(0x78).sound.empty())
			playSound(0x78);
		break;
	case 0x4513d0:
		if ((((id == 0x15 || id == 0x141) && step == 2) || ((id == 0x117 || id == 0x116) && step == 5)) &&
			!slot(4).sound.empty())
			playSound(4);
		break;
	case 0x451460:
		if (_lastSound != 0x35 && !slot(0x35).sound.empty())
			playSound(0x35);
		break;
	case 0x4514a0:
		if (_landed && (id == 0x11 || id == 0x13 || id == 0x17e || id == 0x180)) {
			_landed = false;
			if (!slot(0x11).sound.empty())
				playSound(0x11);
		}
		break;
	case 0x451510:
		if (((id == 0x148 && (step == 0 || step == 4)) || (id == 0x8e && step == 1)) && !slot(0x148).sound.empty())
			playSound(0x148);
		break;
	case 0x4515a0:
		if (id == 0x9b) {
			if (step == 0 && !slot(0x9b).sound.empty())
				playSound(0x9b);
		} else {
			uint32 now = _game->now();
			if (step == 0 && (now & 0xf) == 0xf && _soundTime < now) {
				_soundTime = now + 5000;
				debugC(1, kDebugSound, "Ned sound 289");
				_game->playSlotSound(slot(289), &slot(289));
			}
			if (step == 2 && !slot(0x2d).sound.empty())
				playSound(0x2d);
		}
		break;
	case 0x451670: {
		// Footsteps on the shared slot 0x18 sound
		bool play = ((id == 0x18 || id == 0x10c || id == 0x19) && step == 0) ||
			((id == 0x111 || id == 0x112) && step == 4) || (id == 0x114 && step == 6) ||
			((id == 0x110 || id == 0x10f) && step == 4);
		if (play && !slot(0x18).sound.empty())
			playSound(0x18);
		break;
	}
	case 0x451760:
		if ((step == 2 || step == 7) && !slot(4).sound.empty())
			playSound(4);
		break;
	case 0x4517b0:
		if ((step == 2 || step == 5) && !slot(4).sound.empty())
			playSound(4);
		break;
	case 0x451800:
		if (((id == 0x4e && step == 2) || (id == 0x4d && (step == 3 || step == 9))) && !slot(0x4d).sound.empty())
			playSound(0x4d);
		break;
	case 0x451890:
		// The walk loop's footsteps
		if ((step == 1 || step == 6) && !slot(4).sound.empty())
			playSound(4);
		break;
	default:
		break;
	}
	(void)state;
}

// 0x450240: the sprite position for Ned's world position, relative to the base slot's anchor
void Ned::placement(int16 id, const Common::Point &pos, int &x, int &y) {
	Slot &s = slot(id);
	int ax = 0, ay = 0, frame = 0, duration = 0, ox = 0, oy = 0;
	boxAnchor(s, ax, ay);
	seqStep(s, s.step, frame, duration);
	boxFrameOffset(s, frame, ox, oy);
	int16 d = (int16)(ox - ax - _halfW + _anchorX);
	if (_facing)
		x = pos.x - d - _halfW;
	else
		x = pos.x - s.w + d + _halfW;
	y = pos.y - (int16)oy + (int16)ay - _anchorY;
}

// The image + placement + xform sequence 0x454920, 0x44ee30 and 0x452060 share
void Ned::placeSprite(int16 id, const Common::Point &pos, int scrollX, int scrollY) {
	Slot &s = slot(id);
	int frame = 0, duration = 0;
	seqStep(s, s.step, frame, duration);
	if (s.sprite)
		s.sprite->setCurrentImage(frame);
	int x, y;
	placement(id, pos, x, y);
	if (_xform) {
		_xform(_xformUser, x, y);
	} else {
		x += scrollX;
		y += scrollY;
	}
	if (s.sprite)
		s.sprite->setPosition(x, y);
}

void Ned::applyFlip(Slot &s) {
	if (!s.sprite || s.flip)
		return;
	s.flip = s.sprite->applyEffect(kFxHorizFlip);
	s.flip->active = false;
}

int Ned::wrapX(int x) const {
	if (_wrapMode == 1) {
		if (x < 0)
			return x + _wrapWidth;
		if (x >= _wrapWidth)
			return x - _wrapWidth;
	} else if (_wrapMode == 2) {
		if (x > 0x302a)
			return x - _tiles * _tileW;
		if (x < 0)
			return x + _tiles * _tileW;
	}
	return x;
}

// 0x450760
void Ned::stateReset(int16 state) {
	State &st = _states[state];
	int16 *h = st.h;
	_holdStart = 0;
	h[kStartIdx] = 0;
	h[kLoopIdx] = 0;
	h[kEndIdx] = 0;
	h[kTailIdx] = 0;
	st.flags &= 0xcffffc1f;
	for (int i = 4; i >= 0; i--) {
		if (h[kTail + i] != -1) {
			h[kSlot] = h[kTail + i];
			st.flags |= kPhaseTail;
			slot(h[kTail + i]).step = -1;
		}
	}
	for (int i = 4; i >= 0; i--) {
		if (h[kEnd + i] != -1) {
			h[kSlot] = h[kEnd + i];
			st.flags = (st.flags & 0xfffffc5f) | kPhaseEnd;
			slot(h[kEnd + i]).step = -1;
		}
	}
	if (h[kHold] != -1) {
		h[kSlot] = h[kHold];
		st.flags = (st.flags & 0xfffffc3f) | kPhaseHold;
		slot(h[kHold]).step = -1;
	}
	for (int i = 1; i >= 0; i--) {
		if (h[kLoop + i] != -1) {
			h[kSlot] = h[kLoop + i];
			st.flags = (st.flags & 0xfffffd1f) | kPhaseLoop;
			slot(h[kLoop + i]).step = -1;
		}
	}
	for (int i = 4; i >= 0; i--) {
		if (h[kStart + i] != -1) {
			h[kSlot] = h[kStart + i];
			st.flags = (st.flags & 0xfffffe1f) | kPhaseStart;
			slot(h[kStart + i]).step = -1;
		}
	}
}

// 0x4504c0: zeroes the x speed and clears the landing bits of every slot of the state
void Ned::clearSlotOffsets(int16 state) {
	int16 *h = _states[state].h;
	static const int fields[] = { kTail + 4, kTail + 3, kTail + 2, kTail + 1, kTail,
		kEnd + 4, kEnd + 3, kEnd + 2, kEnd + 1, kEnd, kHold, kLoop + 1, kLoop,
		kStart + 4, kStart + 3, kStart + 2, kStart + 1, kStart };
	for (int f : fields) {
		if (h[f] != -1) {
			Slot &s = slot(h[f]);
			s.offsets[0] = 0;
			s.flags &= 0xffcf;
		}
	}
}

// The release + reset + step + switch sequence the movement code uses to change state
void Ned::enterState(int16 &state, int16 next) {
	int finished;
	_states[_state].flags |= kStateRelease;
	stateReset(next);
	stepState(next, _game->now(), finished);
	state = next;
}

// 0x44f150: drop into the fall state
void Ned::fall(int16 &state) {
	slot(0x11).flags |= kSlotLanding;
	slot(0x14).offsets[1] = 0x1e;
	slot(0x14).offsets[0] = 0;
	_states[_state].flags |= kStateRelease;
	stateReset(kStateFall);
	state = kStateFall;
	clearRegionCache();
}

// 0x44f1d0
void Ned::startFall() {
	fall(_state);
}

// 0x44ebd0
void Ned::releaseState() {
	_states[_state].flags |= kStateRelease;
}

// 0x44eb60
bool Ned::canRelease() const {
	return !(_states[_state].flags & kStateDefer);
}

// 0x4510a0: may Ned go from cur to next now?
bool Ned::canLeave(int16 next, int16 cur) {
	if (!_matrix[cur * kStateCount + next])
		return false;
	State &st = _states[cur];
	uint32 fl = st.flags;
	if (cur == next) {
		if ((fl & kStateLastStep) && (fl & kPhaseLoop) == kPhaseLoop)
			st.flags = fl & ~kStateLastStep;
		return true;
	}
	if (fl & kStateFree)
		return true;
	if (fl & kStateToEnd) {
		if (cur == kStateIdle)
			return true;
		if (cur != kStateWalkTurn && cur != kStateWalkStart && cur != kStateTurn)
			return false;
		Slot &s = slot(st.h[kSlot]);
		return s.step == s.sequenceLength - 1;
	}
	if (fl & kStateStep0) {
		if (cur == kStateIdle)
			return true;
		return slot(st.h[kSlot]).step == 0;
	}
	if (fl & kStateDefer) {
		st.flags = fl | kStateLastStep;
		return false;
	}
	return false;
}

// 0x452130: asks the transition matrix and the next state's enter hook; on refusal next = current
int Ned::tryState(CollisionWorld &world, int16 &next, Common::Point &pos, int scrollX, int scrollY,
	const NedHooks *hooks, int flag) {
	if (!canLeave(next, _state) || (next != _state && (_states[_state].flags & 0xc0))) {
		next = _state;
		return 0;
	}
	uint32 fn = _enterHooks[next];
	if (!fn)
		return 0;
	(void)scrollX;
	(void)scrollY;
	if (enterHook(fn, world, next, (int16)_facing, pos, hooks, flag) != 1) {
		next = _state;
		return 0;
	}
	int16 n = next;
	if (n != _state) {
		uint32 fl = _states[_state].flags;
		if ((fl & kPhaseLoop) || (fl & kPhaseHold)) {
			// Finish the loop or hold first; idle simply waits for that
			_states[_state].flags = fl | kStateRelease;
			if (next == kStateIdle) {
				next = _state;
				return 0;
			}
			stateReset(next);
			return 1;
		}
		stateReset(n);
	}
	return 1;
}

// 0x4546e0: a direction key; turn is set when it points behind Ned
int Ned::walk(uint32 input, int16 &next, CollisionWorld &world, Common::Point &pos, int scrollX, int scrollY,
	const NedHooks *hooks, bool turn) {
	int r = 0;
	if (_state == kStateAction)
		return 0;
	if (_hanging) {
		next = ((~(input >> 1)) & 1) | kStateHangRight;
		return tryState(world, next, pos, scrollX, scrollY, hooks, 0);
	}
	if (!turn) {
		next = kStateStairsUp;
		r = tryState(world, next, pos, scrollX, scrollY, hooks, (input >> 2) & 1);
		if (r)
			return r;
		next = kStateStairsDown;
		r = tryState(world, next, pos, scrollX, scrollY, hooks, (input >> 5) & 1);
		if (r)
			return r;
		if (_state != kStateIdle) {
			next = kStateWalk;
			return tryState(world, next, pos, scrollX, scrollY, hooks, 0);
		}
		next = kStateWalkStart;
		r = tryState(world, next, pos, scrollX, scrollY, hooks, 0);
		if (r)
			return r;
		next = kStateWalk;
		return tryState(world, next, pos, scrollX, scrollY, hooks, 0);
	}
	if (_state == kStateStairsUp || _state == kStateStairsDown) {
		next = kStateStairsUp;
		r = tryState(world, next, pos, scrollX, scrollY, hooks, (input >> 2) & 1);
		if (r)
			return r;
		next = kStateStairsDown;
		r = tryState(world, next, pos, scrollX, scrollY, hooks, (input >> 5) & 1);
		if (r)
			return r;
		next = kStateWalk;
		return tryState(world, next, pos, scrollX, scrollY, hooks, 0);
	}
	next = _state != kStateWalk ? kStateTurn : kStateWalkTurn;
	r = tryState(world, next, pos, scrollX, scrollY, hooks, 0);
	if (r == 1) {
		_facing = _facing == 0;
		return 1;
	}
	return r;
}

// 0x452250: on a slot's last step, its +0x18/+0x1a offset moves Ned, then snaps him to the floor
void Ned::attachOffset(CollisionWorld &world, int16 state, Common::Point &pos) {
	Slot &s = slot(_states[state].h[kSlot]);
	if (s.step != s.sequenceLength - 1)
		return;
	if (s.offsets[3] == 0 && s.offsets[2] == 0)
		return;
	pos.y += s.offsets[3];
	int dx = s.offsets[2];
	if (!_facing)
		dx = -dx;
	int x = wrapX(pos.x + dx);
	pos.x = x;
	int d;
	if (probe(world, x, pos.y, 10, 0, 1, d, 3))
		pos.y += d;
}

// 0x452350: in state 0, Ned's box against the scene's target
void Ned::targetCheck(Common::Point &pos, const NedHooks *hooks) {
	if (_state != kStateUse || _hitTarget || !hooks || !hooks->findTarget)
		return;
	int16 a = 0, b = -1;
	int32 target[4] = { 0, 0, 0, 0 };
	hooks->findTarget(hooks->user, pos.x, pos.y, &a, &b, target);
	if (b == -1)
		return;
	int16 id = _states[_state].h[kSlot];
	Slot &s = slot(id);
	int frame = 0, duration = 0;
	seqStep(s, s.step, frame, duration);
	int32 r[4];
	boxRect(s, frame, _facing ? 1 : 4, r);
	int px, py;
	placement(id, pos, px, py);
	r[0] += px;
	r[2] += px;
	r[1] += py;
	r[3] += py;
	// IntersectRect
	int32 isect[4] = { MAX(r[0], target[0]), MAX(r[1], target[1]), MIN(r[2], target[2]), MIN(r[3], target[3]) };
	if (isect[0] < isect[2] && isect[1] < isect[3] && hooks->hitTarget)
		_hitTarget = hooks->hitTarget(hooks->user, pos.x, pos.y, &a, &b, isect);
}

// 0x4509d0: the first enabled region of any map containing (x, y), both edges inclusive
bool Ned::regionAt(CollisionWorld &world, int x, int y, int32 rect[4], uint32 &flags, int32 &id) {
	int count = world.getMapCount();
	for (int m = 0; m < count; m++) {
		const CollisionMap *map = world.getMap(m);
		if (!map)
			continue;
		for (const CollisionMap::Region &r : map->regions) {
			if ((r.type & 0x80000000) || r.rect.left > x || x > r.rect.right || r.rect.top > y || y > r.rect.bottom)
				continue;
			rect[0] = r.rect.left;
			rect[1] = r.rect.top;
			rect[2] = r.rect.right - 1;
			rect[3] = r.rect.bottom - 1;
			flags = r.type;
			id = r.id;
			return true;
		}
	}
	return false;
}

// 0x450910: the first enabled region of the world's user map overlapping box
bool Ned::userRegionAt(CollisionWorld &world, const int32 box[4], int32 rect[4], uint32 &flags, int32 &id) {
	const CollisionMap *map = world.getUser();
	if (!map)
		return false;
	for (const CollisionMap::Region &r : map->regions) {
		if (r.type & 0x80000000)
			continue;
		if (MAX<int32>(box[0], r.rect.left) >= MIN<int32>(box[2], r.rect.right) ||
			MAX<int32>(box[1], r.rect.top) >= MIN<int32>(box[3], r.rect.bottom))
			continue;
		rect[0] = r.rect.left;
		rect[1] = r.rect.top;
		rect[2] = r.rect.right - 1;
		rect[3] = r.rect.bottom - 1;
		flags = r.type;
		id = r.id;
		return true;
	}
	return false;
}

// 0x455550: the floor (dirs 2 = below, 1 = above) within maxDist of (x, y); out is the offset
bool Ned::probe(CollisionWorld &world, int x, int y, int maxDist, int start, int step, int &out, uint dirs) {
	out = 0;
	if (world.isSolid(x, y))
		return true;
	out = start;
	while (out < maxDist) {
		if ((dirs & 2) && world.isSolid(x, (int16)(y + out)))
			return true;
		if ((dirs & 1) && world.isSolid(x, (int16)(y - out))) {
			out = -out;
			return true;
		}
		out += step;
	}
	return false;
}

// 0x455420: like probe, but a hit stops only where no region carries all of need
bool Ned::probeRegion(CollisionWorld &world, int x, int y, int maxDist, int start, int step, int &out,
	uint dirs, uint32 need) {
	out = start;
	if (maxDist <= start)
		return false;
	for (;;) {
		int32 rect[4];
		uint32 flags;
		int32 id;
		if ((dirs & 2) && world.isSolid(x, (int16)(y + out))) {
			if (!regionAt(world, x, y + out, rect, flags, id) || (flags & need) != need)
				return true;
		} else if ((dirs & 1) && world.isSolid(x, (int16)(y - out))) {
			if (!regionAt(world, x, y - out, rect, flags, id) || (flags & need) != need) {
				out = -out;
				return true;
			}
		}
		out += step;
		if (maxDist <= out)
			return false;
	}
}

// 0x4530f0: when a state ends with a pending check, a region under Ned may take over
int Ned::enterRegion(CollisionWorld &world, int16 &state, int16 slotId, int step, int px, int py,
	int x, int y, int scrollX, int scrollY, const NedHooks *hooks) {
	int32 box[4], rect[4];
	uint32 flags;
	int32 id;
	// The step stands in for the frame here
	boxRect(slot(slotId), step, _facing ? 0 : 3, box);
	box[0] += px;
	box[2] += px;
	box[1] += py;
	box[3] += py;
	if (!regionAt(world, x, y, rect, flags, id)) {
		if (!userRegionAt(world, box, rect, flags, id)) {
			clearRegionCache();
			return 0;
		}
		int c = (rect[2] - rect[0]) / 2 + rect[0];
		bool behind = _facing ? c < x : x < c;
		if (!behind) {
			storeRegion(rect, flags, id);
			if (hooks && hooks->region && hooks->region(hooks->user, id, flags, x, y, &state)) {
				enterState(state, state);
				return 1;
			}
		}
	} else if (flags & 0x200) {
		storeRegion(rect, flags, id);
		if (hooks && hooks->region && hooks->region(hooks->user, id, flags, scrollX, scrollY, &state)) {
			enterState(state, state);
			return 1;
		}
	}
	return 0;
}

// 0x4529c0: Ned moves to (nx, ny) unless a region (or the scene) takes over
int Ned::moveCheck(CollisionWorld &world, int16 &state, int16 slotId, int frame, Common::Point &pos,
	int px, int py, int nx, int ny, int scrollX, int scrollY, const NedHooks *hooks, bool force) {
	int32 box[4], rect[4];
	uint32 flags;
	int32 id;
	boxRect(slot(slotId), frame, _facing ? 0 : 3, box);
	box[0] += px;
	box[2] += px;
	box[1] += py;
	box[3] += py;

	if (!regionAt(world, nx, ny, rect, flags, id)) {
		if (!userRegionAt(world, box, rect, flags, id)) {
			clearRegionCache();
			pos.x = nx;
			pos.y = ny;
			return 0;
		}
		int c = (rect[2] - rect[0]) / 2 + rect[0];
		bool behind = _facing ? pos.x > c : pos.x < c;
		if (behind) {
			pos.x = nx;
			pos.y = ny;
			return 0;
		}
		// Still in the region it last reported: Ned doesn't move
		if (!force && flags == _regionFlags && id == _regionId)
			return 0;
		storeRegion(rect, flags, id);
		if (!hooks || !hooks->region)
			return 0;
		if (!hooks->region(hooks->user, id, flags, nx, ny, &state)) {
			pos.x = nx;
			pos.y = ny;
			return 0;
		}
		enterState(state, state);
		return 1;
	}

	if (flags & 0x70000) {
		int c = (rect[2] - rect[0]) / 2 + rect[0];
		bool behind = _facing ? pos.x > c : pos.x < c;
		if (!behind) {
			if (!force && flags == _regionFlags && id == _regionId)
				return 0;
			storeRegion(rect, flags, id);
			if (flags & 0x10000) {
				enterState(state, kStateRegion1);
				return 1;
			}
			if (flags & 0x20000) {
				enterState(state, kStateRegion2);
				return 1;
			}
			if (flags & 0x40000) {
				enterState(state, kStateRegion3);
				return 1;
			}
			pos.x = nx;
			pos.y = ny;
			return 0;
		}
	}

	if (!(flags & 0x200)) {
		pos.x = nx;
		pos.y = ny;
		return 0;
	}
	storeRegion(rect, flags, id);
	int r = 0;
	if (hooks && hooks->region && hooks->region(hooks->user, id, flags, scrollX, scrollY, &state)) {
		enterState(state, state);
		r = 1;
	}
	pos.x = nx;
	pos.y = ny;
	return r;
}

// 0x452eb0: the region test of moveCheck without moving or switching
int Ned::regionCheck(CollisionWorld &world, int16 slotId, int frame, int px, int py, int x, int y, bool force) {
	int32 box[4], rect[4];
	uint32 flags;
	int32 id;
	boxRect(slot(slotId), frame, _facing ? 0 : 3, box);
	box[0] += px;
	box[2] += px;
	box[1] += py;
	box[3] += py;
	if (!regionAt(world, x, y, rect, flags, id)) {
		if (!userRegionAt(world, box, rect, flags, id)) {
			clearRegionCache();
			return 0;
		}
		int c = (rect[2] - rect[0]) / 2 + rect[0];
		bool behind = _facing ? c < x : x < c;
		if (!behind && (force || flags != _regionFlags || id != _regionId)) {
			storeRegion(rect, flags, id);
			return 1;
		}
	} else if (flags & 0x70000) {
		int c = (rect[2] - rect[0]) / 2 + rect[0];
		bool behind = _facing ? c < x : x < c;
		if (!behind && (force || flags != _regionFlags || id != _regionId)) {
			storeRegion(rect, flags, id);
			return 1;
		}
	}
	return 0;
}

// 0x4524f0: on landing, rope and pole regions (0x800, 0x1000) start their scripted actions
int Ned::landCheck(CollisionWorld &world, int16 &state, Common::Point &pos) {
	int32 rect[4];
	uint32 flags;
	int32 id;
	if (!regionAt(world, pos.x, pos.y, rect, flags, id))
		return 0;
	int finished;

	if ((flags & 0x800) && !(flags & 0x1000)) {
		// Climb to the 0x80 region above, in steps of 8 or 9
		storeRegion(rect, flags, id);
		int i = 0;
		int32 above[4];
		uint32 aboveFlags = 0;
		int32 aboveId;
		while (!regionAt(world, pos.x, pos.y - i, above, aboveFlags, aboveId) || !(aboveFlags & 0x80)) {
			i += 0x13;
			if (i > 399) {
				slot(419).offsets[0] = 0;
				slot(419).offsets[1] = 0;
				slot(52).offsets[0] = 0;
				slot(52).offsets[1] = 0;
				enterState(state, kStateDrop);
				return 1;
			}
		}
		int d = pos.y - above[3];
		int div = (d % 9 == 0) ? 8 : 9;
		_action = 0x25;
		slot(419).offsets[0] = 0;
		slot(52).offsets[0] = 0;
		int16 q = (int16)(d / div);
		slot(52).offsets[1] = -q;
		int16 t = (int16)div * q;
		slot(142).offsets[5] = t - (int16)d;
		slot(146).offsets[3] = (int16)d - t;
		slot(419).offsets[1] = slot(52).offsets[1];
		readState(_states[kStateAction], kActionTable + 0x25 * 0x3c);
		state = kStateAction;
		readState(_states[kStateIdle], 0x4aea34);
		_hanging = true;
		readMatrixRow(kStateIdle, 0x4aeacc);
		stateReset(kStateAction);
		stepState(kStateAction, 0, finished);
		return 1;
	}

	if (flags & 0x800) {
		// Both bits
		slot(419).offsets[1] = -15;
		slot(52).offsets[1] = -15;
		slot(419).offsets[0] = 0x3c;
		slot(52).offsets[0] = 0x3c;
		readState(_states[kStateAction], kActionTable + 0x26 * 0x3c);
		_action = 0x26;
	} else if (flags & 0x1000) {
		slot(419).offsets[0] = 0;
		slot(419).offsets[1] = -15;
		slot(52).offsets[1] = -15;
		slot(52).offsets[0] = 0;
		slot(53).offsets[1] = -40;
		readState(_states[kStateAction], kActionTable + 0x27 * 0x3c);
		_action = 0x27;
	} else {
		return 0;
	}
	_states[_state].flags |= kStateRelease;
	stateReset(kStateAction);
	stepState(kStateAction, _game->now(), finished);
	state = kStateAction;
	storeRegion(rect, flags, id);
	return 1;
}

// ---- DAT_004c5758: enter hooks (world, &state, facing, pos, &scroll, hooks, flag) ----

int Ned::enterHook(uint32 fn, CollisionWorld &world, int16 &state, int16 facing, Common::Point &pos,
	const NedHooks *hooks, int flag) {
	switch (fn) {
	case 0x44e980:
		return 1;
	case 0x44e990:
		return 0;
	case 0x44eb60:
		return canRelease();
	case 0x450dc0:
		// State 0: tell the scene
		if (hooks && hooks->use)
			hooks->use(hooks->user, pos.x, pos.y, &state);
		_hitTarget = 0;
		return 1;
	case 0x450c80:
		return enterSideStep(world, state, facing, pos);
	case 0x450e40:
		return enterClimb(world, kStateClimbUp, pos, 0x10);
	case 0x450ec0:
		return enterClimb(world, kStateClimbDown, pos, 0x20);
	case 0x450f30:
		return enterStairs(world, facing, pos, kStateStairsUp, 2, 1, flag);
	case 0x450fe0:
		return enterStairs(world, facing, pos, kStateStairsDown, 8, 4, flag);
	case 0x4503e0:
		return enterHang(world, state, pos);
	case 0x4505e0:
		restoreButton3();
		return 1;
	case 0x450ac0:
		return enterHangWalk(world, state, facing, pos, 0x10, false);
	case 0x450ba0:
		return enterHangWalk(world, state, facing, pos, -0x10, true);
	default:
		warning("Ned: unknown enter hook %x", fn);
		return 0;
	}
}

// 0x450c80 (state 1): a step into a 0x70000 region switches to its state
int Ned::enterSideStep(CollisionWorld &world, int16 &state, int16 facing, Common::Point &pos) {
	if (_state == kStateWalk)
		return 1;
	int x = wrapX(pos.x + (facing ? _sideStep : -_sideStep));
	if (regionAt(world, x, pos.y, _regionRect, _regionFlags, _regionId) && (_regionFlags & 0x70000)) {
		int c = (_regionRect[2] - _regionRect[0]) / 2 + _regionRect[0];
		bool behind = facing == 0 ? x < c : c < x;
		if (!behind) {
			if (_regionFlags & 0x10000)
				state = kStateRegion1;
			else if (_regionFlags & 0x20000)
				state = kStateRegion2;
			else if (_regionFlags & 0x40000)
				state = kStateRegion3;
		}
	}
	return 1;
}

// 0x450e40 / 0x450ec0 (states 5, 6): needs a region with bit; leaving it ends the climb
int Ned::enterClimb(CollisionWorld &world, int16 climb, Common::Point &pos, uint32 bit) {
	int y = pos.y;
	if (climb == kStateClimbUp && _state == kStateClimbUp)
		y -= 0x84;
	int32 rect[4];
	uint32 flags = 0;
	int32 id;
	int r = 0;
	if (regionAt(world, pos.x, y, rect, flags, id) && (flags & bit))
		r = 1;
	if (_state == climb && !r) {
		_states[climb].flags |= kStateRelease;
		r = 1;
		_pending = true;
	}
	return r;
}

// 0x450f30 / 0x450fe0 (states 7, 8): stairs regions, bitLeft / bitRight by facing
int Ned::enterStairs(CollisionWorld &world, int16 facing, Common::Point &pos, int16 stairs,
	uint32 bitLeft, uint32 bitRight, int flag) {
	int32 rect[4];
	uint32 flags;
	int32 id;
	if (!regionAt(world, pos.x, pos.y, rect, flags, id))
		return 0;
	uint32 bit = facing == 0 ? bitLeft : bitRight;
	if (_state == stairs) {
		if (!(flags & bit))
			_states[stairs].flags |= kStateRelease;
		return 1;
	}
	if ((flags & bit) && (flag || (flags & 0x400)))
		return 1;
	return 0;
}

// 0x4503e0 (state 9): a 0x80 bar 0x5b..0x78 above starts hanging (action 17)
int Ned::enterHang(CollisionWorld &world, int16 &state, Common::Point &pos) {
	int32 rect[4];
	uint32 flags = 0;
	int32 id;
	int16 d = 0x5b;
	for (;;) {
		if (regionAt(world, pos.x, pos.y - d, rect, flags, id) && (flags & 0x80))
			break;
		d++;
		if (d > 0x78) {
			slot(0x11).flags |= kSlotLanding;
			slot(160).flags |= kSlotLanding;
			return 1;
		}
	}
	readState(_states[kStateAction], kActionTable + 0x11 * 0x3c);
	_action = 0x11;
	state = kStateAction;
	readState(_states[kStateIdle], 0x4aea34);
	slot(146).offsets[3] = d;
	readMatrixRow(kStateIdle, 0x4aeacc);
	_hanging = true;
	slot(142).offsets[5] = -d;
	return 1;
}

// 0x450ac0 / 0x450ba0 (states 16, 17): hand over hand along a 0x80 bar
int Ned::enterHangWalk(CollisionWorld &world, int16 &state, int16 facing, Common::Point &pos, int dx, bool left) {
	int x = wrapX(pos.x + dx);
	Slot &s = slot(328);
	if (regionAt(world, x, pos.y, _regionRect, _regionFlags, _regionId) && _regionFlags == 0x80) {
		if ((facing != 0) != left) {
			s.flags &= 0xfffe;
			s.offsets[0] = 0x10;
		} else {
			s.flags |= 1;
			s.offsets[0] = -0x10;
		}
		return 1;
	}
	s.offsets[0] = 0;
	state = kStateIdle;
	return 1;
}

// 0x4505e0 (state 10)
void Ned::restoreButton3() {
	slot(19).flags |= 0x20;
	slot(315).flags |= 0x10;
	slot(316).flags |= 0x10;
	slot(317).flags |= 0x10;
	slot(319).flags |= 0x10;
	slot(322).flags |= 0x10;
	slot(319).offsets[0] = _saved3[0];
	slot(322).offsets[0] = _saved3[1];
	slot(316).offsets[0] = _saved3[3];
	slot(315).offsets[0] = _saved3[2];
	slot(317).offsets[0] = _saved3[4];
}

// ---- DAT_004c56d8: tick hooks (world, &state, pos, scrollX, scrollY, hooks) ----

int Ned::tickHook(uint32 fn, CollisionWorld &world, int16 &state, Common::Point &pos, int scrollX, int scrollY,
	const NedHooks *hooks) {
	switch (fn) {
	case 0x453780:
		return hookMove(world, state, pos, scrollX, scrollY, hooks);
	case 0x453390:
		return hookWalk(world, state, pos, scrollX, scrollY, hooks);
	case 0x453570:
		return hookFall(world, state, pos);
	case 0x453b20:
		return hookAction(world, state, pos);
	case 0x453e60:
		return hookButton1(world, state, pos);
	case 0x454080:
		return hookButton3(world, state, pos, scrollX, scrollY, hooks);
	default:
		warning("Ned: unknown tick hook %x", fn);
		return 0;
	}
}

// 0x453780: moves by the slot's offsets (+0x1c/+0x1e once on step 0) and follows the floor
int Ned::hookMove(CollisionWorld &world, int16 &state, Common::Point &pos, int scrollX, int scrollY,
	const NedHooks *hooks) {
	int16 id = _states[_state].h[kSlot];
	if (_states[_state].flags & kStateLastStep)
		return 0;
	Slot &s = slot(id);
	int16 dx = s.offsets[0];
	if (dx == 0 && s.offsets[1] == 0 && s.offsets[5] == 0 && s.offsets[4] == 0 && !(s.flags & kSlotLanding))
		return 0;
	int x, y;
	if (!_facing) {
		x = wrapX(pos.x - dx);
		y = s.offsets[1] + pos.y;
		if (id == 0x35) {
			int16 v = slot(0x35).offsets[1] + _gravity;
			slot(0x35).offsets[1] = v > 0 ? 0 : v;
		}
		if (s.step == 0) {
			y += s.offsets[5];
			x = wrapX(x - s.offsets[4]);
		}
	} else {
		x = wrapX(pos.x + dx);
		y = s.offsets[1] + pos.y;
		if (id == 0x35) {
			int16 v = slot(0x35).offsets[1] + _gravity;
			slot(0x35).offsets[1] = v > 0 ? 0 : v;
		}
		if (s.step == 0) {
			y += s.offsets[5];
			x = wrapX(s.offsets[4] + x);
		}
	}
	int px, py, frame = 0, duration = 0;
	placement(id, pos, px, py);
	seqStep(s, s.step, frame, duration);
	if (!(s.flags & kSlotNoProbe)) {
		int d;
		if (!probe(world, x, y, 400, 0, 1, d, 3)) {
			int finished;
			_states[_state].flags |= kStateRelease;
			stateReset(kStateIdle);
			stepState(kStateIdle, _game->now(), finished);
			state = kStateIdle;
			return 1;
		}
		y += d;
		pos.y += d;
	}
	return moveCheck(world, state, id, frame, pos, px, py, x, y, scrollX, scrollY, hooks, false);
}

// 0x453390 (state 1): walks along the floor, falling when there is none within 0x1b
int Ned::hookWalk(CollisionWorld &world, int16 &state, Common::Point &pos, int scrollX, int scrollY,
	const NedHooks *hooks) {
	int16 id = _states[_state].h[kSlot];
	Slot &s = slot(id);
	int y = s.offsets[1] + pos.y;
	int x = _facing ? s.offsets[0] + pos.x : pos.x - s.offsets[0];
	x = wrapX(x);
	int px, py, frame = 0, duration = 0;
	placement(id, pos, px, py);
	seqStep(s, s.step, frame, duration);
	int d;
	if (probe(world, x, y, 0x1b, 0, 1, d, 3))
		return moveCheck(world, state, id, frame, pos, px, py, x, y + d, scrollX, scrollY, hooks, true);
	int finished;
	fall(state);
	stepState(state, 0, finished);
	pos.x = x;
	pos.y = y;
	return 1;
}

// 0x453570 (state 18): falling with gravity (at most 40 a step) until the floor, then landing
int Ned::hookFall(CollisionWorld &world, int16 &state, Common::Point &pos) {
	Slot &s = slot(_states[_state].h[kSlot]);
	if ((s.flags & kSlotLanding) == kSlotLanding) {
		if (!_pending)
			_landed = true;
		_pending = true;
		int d;
		if (probe(world, pos.x, pos.y, 0x1e, 0, 1, d, 3)) {
			pos.y += d;
			return landCheck(world, state, pos) != 0;
		}
		return 0;
	}
	int16 dy = s.offsets[1];
	int16 g = 0;
	if (dy != 0) {
		g = (int16)_gravity;
		if (g + dy > 0x28)
			g = 0x28 - dy;
		s.offsets[1] = dy + g;
	}
	if (g < 0x15)
		g = 0x14;
	int x = pos.x;
	int i = -g;
	int y = pos.y + s.offsets[1];
	if (i < 0x5c) {
		for (; i < 0x5c; i++) {
			if (!world.isSolid(x, i + y))
				continue;
			int32 rect[4];
			uint32 flags;
			int32 id;
			if (!regionAt(world, x, i + y, rect, flags, id))
				break;
			if (!(flags & 0x80) && (!(flags & 0x20) || !(flags & 0x10)))
				break;
		}
		if (i < 0x5c) {
			int finished;
			_states[_state].flags |= kStateRelease;
			stepState(_state, 0, finished);
			while (slot(_states[_state].h[kSlot]).step != 0)
				stepState(_state, 0, finished);
			y += i;
		}
	}
	pos.x = x;
	pos.y = y;
	return 0;
}

// 0x453b20 (state 15): a scripted action's offsets, on the floor unless the slot says not to
int Ned::hookAction(CollisionWorld &world, int16 &state, Common::Point &pos) {
	int16 id = _states[_state].h[kSlot];
	if (_states[_state].flags & kStateLastStep)
		return 0;
	Slot &s = slot(id);
	int16 dx = s.offsets[0];
	if (dx == 0 && s.offsets[1] == 0 && s.offsets[5] == 0 && s.offsets[4] == 0 && !(s.flags & kSlotLanding))
		return 0;
	int x, y;
	if (!_facing) {
		x = wrapX(pos.x - dx);
		y = s.offsets[1] + pos.y;
		if (id == 0x35) {
			int16 v = slot(0x35).offsets[1] + _gravity;
			slot(0x35).offsets[1] = v > 0 ? 0 : v;
		}
		if (s.step == 0) {
			y += s.offsets[5];
			x = wrapX(x - s.offsets[4]);
		}
	} else {
		x = wrapX(pos.x + dx);
		y = s.offsets[1] + pos.y;
		if (id == 0x35) {
			int16 v = slot(0x35).offsets[1] + _gravity;
			slot(0x35).offsets[1] = v > 0 ? 0 : v;
		}
		if (s.step == 0) {
			y += s.offsets[5];
			x = wrapX(s.offsets[4] + x);
		}
	}
	if (s.flags & kSlotNoProbe) {
		pos.x = x;
		pos.y = y;
		return 0;
	}
	int d;
	if (probe(world, x, y, 400, 0, 1, d, 3)) {
		pos.x = x;
		pos.y = d + y;
		return 0;
	}
	int finished;
	_states[_state].flags |= kStateRelease;
	stateReset(kStateIdle);
	stepState(kStateIdle, _game->now(), finished);
	state = kStateIdle;
	return 1;
}

// 0x453e60 (state 9): on its landing slot, finds the floor below box 2 (5 flipped)
int Ned::hookButton1(CollisionWorld &world, int16 &state, Common::Point &pos) {
	int16 id = _states[_state].h[kSlot];
	Slot &s = slot(id);
	if ((s.flags & kSlotLanding) != kSlotLanding)
		return 0;
	int x = wrapX(pos.x);
	int px, py, frame = 0, duration = 0;
	placement(id, pos, px, py);
	seqStep(s, s.step, frame, duration);
	int32 r[4];
	boxRect(s, frame, _facing ? 2 : 5, r);
	int y = r[3] == kNone ? pos.y : r[3] + py;
	int d;
	if (!probeRegion(world, x, y, 0x96, 0, 1, d, 2, 0x30)) {
		int finished;
		fall(state);
		stepState(state, 0, finished);
		return 1;
	}
	pos.x = x;
	pos.y = d + y;
	if (!landCheck(world, state, pos)) {
		if (!_pending)
			_landed = true;
		_pending = true;
		clearSlotOffsets(_state);
		return 0;
	}
	return 1;
}

// 0x454080 (state 10): moves by the slot's x speed a pixel at a time against walls and slopes
int Ned::hookButton3(CollisionWorld &world, int16 &state, Common::Point &pos, int scrollX, int scrollY,
	const NedHooks *hooks) {
	int16 id = _states[_state].h[kSlot];
	Slot &s = slot(id);
	int finished;
	(void)scrollX;
	(void)scrollY;
	(void)hooks;

	if ((s.flags & kSlotLanding) == kSlotLanding) {
		int d;
		if (!probeRegion(world, pos.x, pos.y, 0x5c, -20, 1, d, 2, 0x80)) {
			fall(state);
			stepState(state, 0, finished);
		} else {
			pos.y += d;
			if (!landCheck(world, state, pos)) {
				if (!_pending)
					_landed = true;
				_pending = true;
				return 0;
			}
		}
		return 1;
	}

	int px, py, frame = 0, duration = 0;
	placement(id, pos, px, py);
	seqStep(s, s.step, frame, duration);
	int32 box[4];
	boxRect(s, frame, _facing ? 2 : 5, box);
	int n = s.offsets[0], dir = 1;
	if (!_facing) {
		n = -n;
		dir = -1;
	}

	// The regions along the way
	for (int i = 0; n != 0 && i != n; i += dir) {
		int x = wrapX(pos.x + i);
		if (!regionCheck(world, id, frame, px, py, x, pos.y, true))
			continue;
		if (_regionFlags & 0x20000) {
			pos.x = _facing ? _regionRect[0] - 1 : _regionRect[2] + 1;
			enterState(state, kStateRegion2);
			return 1;
		}
		if (_regionFlags & 0x10000) {
			pos.x = _facing ? _regionRect[0] - 1 : _regionRect[2] + 1;
			int d;
			if (probe(world, pos.x, pos.y, 0x38, 0, 1, d, 3))
				pos.y += d;
			enterState(state, kStateRegion1);
			_states[kStateRegion1].flags |= kStateRelease;
			return 1;
		}
	}

	// Then a column of the box at a time
	int lx = pos.x;
	int top = box[1] + py, bottom = py + box[3];
	int count = n;
	if (count == 0)
		return 0;
	for (;;) {
		int x = wrapX(lx + dir);
		lx = x;
		int y = top;
		bool blocked = false;
		for (; y < bottom; y++) {
			if (!world.isSolid(x, y))
				continue;
			int32 rect[4];
			uint32 flags;
			int32 rid;
			if (regionAt(world, x, y, rect, flags, rid)) {
				if (flags & 0x80)
					continue;
				int d;
				if (!_facing) {
					if ((flags & 2) && y < rect[3] && (world.isSolid(x, y - 1) || world.isSolid(x, y + 1)) &&
						probe(world, x, y, 0x1d, 0, 1, d, 3))
						y += d;
				} else if (flags & 1) {
					if (y < rect[3] && (world.isSolid(x, y - 1) || world.isSolid(x, y + 1)) &&
						probe(world, x, y, 0x1d, 0, 1, d, 3))
						y += d;
				}
				if ((flags & 0x20) && (flags & 0x10))
					continue;
			}
			blocked = true;
			break;
		}
		if (blocked) {
			if (!_pending)
				_landed = true;
			_pending = true;
			stepState(_state, 0, finished);
			while (!finished && !(slot(_states[_state].h[kSlot]).flags & kSlotLanding))
				stepState(_state, 0, finished);
			pos.x = x;
			pos.y = y;
		}
		if (y != bottom)
			return 0;
		pos.x = wrapX(pos.x + dir);
		count -= dir;
		if (count == 0)
			return 0;
	}
}

// ---- the controller ----

int Ned::tick(CollisionWorld &world, uint32 input, int &facing, Common::Point &pos, uint32 now,
	int scrollX, int scrollY, const NedHooks *hooks) {
	uint32 hi = input & 0xff00;
	uint32 lo = input & 0xff;
	int result = 0;
	if (_upHeld)
		lo = kInputUp;
	if (_downHeld)
		lo = kInputDown;
	if (!_enabled)
		return 0;
	if (_hanging) {
		lo &= ~(uint32)kInputUp;
		hi &= 0xfffff4ff;
	}
	if (_state == kStateWalk && !(lo & 3))
		lo &= 0xb00;
	if (_state == kStateClimbUp || _state == kStateClimbDown)
		hi &= 0xfffff4ff;
	if (_state == kStateStairsUp || _state == kStateStairsDown) {
		lo &= ~0x24u;
		hi &= 0xfffff4ff;
	}
	hideSlot(_states[_state].h[kSlot]);

	int16 cur = _state;
	int16 next = _state;
	facing = _facing;
	if (hi & kInputButton1) {
		if (_state == kStateWalk)
			next = (_states[kStateWalk].flags & kPhaseMask) != kPhaseLoop ? kStateButton1 : kStateButton3;
		else
			next = _state == kStateWalkStart ? kStateButton3 : kStateButton1;
		result = tryState(world, next, pos, scrollX, scrollY, hooks, 0);
	} else if (hi & kInputButton3) {
		next = kStateButton3;
		result = tryState(world, next, pos, scrollX, scrollY, hooks, 0);
	} else if (hi & kInputButton2) {
		next = kStateUse;
		result = tryState(world, next, pos, scrollX, scrollY, hooks, 0);
	} else if (lo & kInputUp) {
		next = kStateClimbUp;
		result = tryState(world, next, pos, scrollX, scrollY, hooks, 0);
		if (!result) {
			_upHeld = false;
			if (lo & kInputLeft)
				result = walk(lo, next, world, pos, scrollX, scrollY, hooks, _facing != 0);
			else if (lo & kInputRight)
				result = walk(lo, next, world, pos, scrollX, scrollY, hooks, _facing == 0);
		} else {
			_upHeld = true;
		}
	} else if (lo & kInputDown) {
		if (_hanging) {
			// Letting go of the bar
			int16 id = _states[_state].h[kSlot];
			if (id == 0x90 || id == 0x148) {
				slot(0x11).flags |= kSlotLanding;
				slot(0x14).offsets[1] = 0x1e;
				slot(0x14).offsets[0] = 0;
				_states[_state].flags |= kStateRelease;
				stateReset(kStateFall);
				readState(_states[kStateIdle], 0x4ae9f8);
				readMatrixRow(kStateIdle, 0x4aea70);
				next = kStateFall;
				clearRegionCache();
				_hanging = false;
			}
		} else if (!(lo & kInputLeft) && !(lo & kInputRight)) {
			next = kStateClimbDown;
			result = tryState(world, next, pos, scrollX, scrollY, hooks, 0);
			if (!result) {
				if (_downHeld) {
					_downHeld = false;
				} else if (next != kStateDuck) {
					next = kStateDuck;
					result = tryState(world, next, pos, scrollX, scrollY, hooks, 0);
				}
			} else {
				_downHeld = true;
			}
		} else {
			next = kStateClimbDown;
			result = tryState(world, next, pos, scrollX, scrollY, hooks, 0);
			if (!result) {
				if (lo & kInputLeft)
					result = walk(lo, next, world, pos, scrollX, scrollY, hooks, _facing != 0);
				else if (lo & kInputRight)
					result = walk(lo, next, world, pos, scrollX, scrollY, hooks, _facing == 0);
			}
		}
	} else if (lo & kInputLeft) {
		result = walk(lo, next, world, pos, scrollX, scrollY, hooks, _facing != 0);
	} else if (lo & kInputRight) {
		result = walk(lo, next, world, pos, scrollX, scrollY, hooks, _facing == 0);
	} else {
		next = kStateIdle;
		result = tryState(world, next, pos, scrollX, scrollY, hooks, 0);
	}

	// A held 0x13 region gives way after 2 s
	if (_state == kStateRegion3 && _holdStart && _holdStart + 2000 < now) {
		slot(0x11).flags |= kSlotLanding;
		_states[kStateRegion3].flags |= kStateRelease;
		slot(0x14).offsets[1] = 0x1e;
		slot(0x14).offsets[0] = 0;
		stateReset(kStateFall);
		next = kStateFall;
		clearRegionCache();
	}

	// After 30 s of idling, a fidget
	if (next == kStateIdle) {
		if (_idleTime + 30000 < now && _idleTime != 0 && !_hanging && _fidgetLoaded) {
			int16 fidgetSlot = (int16)_fidgetList[0].id;
			if (_fidgetFixed == -1) {
				if (!_fidgetShown) {
					_fidgetShown = true;
					Slot &f = slot(fidgetSlot);
					if (f.sprite) {
						f.sprite->setDrawOrder(_z);
						f.sprite->setDestination(_game->display().getPrimary());
						f.sprite->setVisibility(false);
					}
					_states[kStateFidget].h[kStart] = fidgetSlot;
					applyFlip(f);
				}
			} else {
				_states[kStateFidget].h[kStart] = _fidgetFixed;
			}
			next = kStateFidget;
			result = tryState(world, next, pos, scrollX, scrollY, hooks, 0);
			if (result) {
				_states[kStateFidget].h[kStart] = fidgetSlot;
				_idleTime = now;
			}
		}
	} else {
		_idleTime = now;
	}

	Slot &cs = slot(_states[_state].h[kSlot]);
	if (_state == next && now <= cs.deadline) {
		// Not yet time for the next step
		int16 id = _states[_state].h[kSlot];
		placeSprite(id, pos, scrollX, scrollY);
		_curSlot = id;
	} else {
		attachOffset(world, cur, pos);
		int finished;
		stepState(next, now, finished);
		if (!finished) {
			_state = next;
		} else {
			if (_state == kStateFidget) {
				if (_fidgetRotate) {
					_game->stopSlotSound(slot(_fidgetList[0].id));
					_game->freeList(_fidgetList);
					int16 f;
					do {
						f = (int16)((nextRandom() % 11) * 2);
					} while (f == _fidget);
					_fidget = f;
					_fidgetList = _game->readList(kFidgetList + _fidget * 0x24, 2);
					_fidgetLoaded = _game->loadList(kNedNaf, _fidgetList, _game->readList(kFidgetList, 1)[0].scene);
				}
				_fidgetShown = false;
			}
			if (_state == kStateAction) {
				if (_onFinish)
					_onFinish(_onFinishUser, _action);
				if (_onAction) {
					_onAction(_onActionUser, _action);
					_onAction = nullptr;
				}
				_action = -1;
				if (!_enabled) {
					_state = next;
					return 0;
				}
			}
			_state = next;
			if (!_pending) {
				_upHeld = _downHeld = false;
				stateReset(kStateIdle);
				stepState(kStateIdle, 0, finished);
				_state = kStateIdle;
			} else {
				_pending = false;
				int16 id = _states[next].h[kSlot];
				int step = slot(id).step;
				int px, py;
				placement(id, pos, px, py);
				if (!enterRegion(world, _state, id, step, px, py, pos.x, pos.y, scrollX, scrollY, hooks)) {
					_upHeld = _downHeld = false;
					stateReset(kStateIdle);
					stepState(kStateIdle, 0, finished);
					_state = kStateIdle;
				}
			}
			result = 1;
		}

		uint32 fn = _tickHooks[_state];
		if (fn && tickHook(fn, world, next, pos, scrollX, scrollY, hooks))
			_state = next;

		int16 id = _states[_state].h[kSlot];
		placeSprite(id, pos, scrollX, scrollY);
		_curSlot = id;
		targetCheck(pos, hooks);
	}

	// The flip by facing; a slot without the effect stays hidden
	int16 id = _states[_state].h[kSlot];
	Slot &s = slot(id);
	if (!s.flip)
		return -1;
	s.flip->active = _facing == 0;
	_curSlot = id;
	if (!s.sprite)
		return -1;
	s.sprite->setVisibility(_enabled);
	return result;
}

// ---- public entry points ----

bool Ned::init() {
	const ExeImage &exe = _game->exe();
	for (int i = 0; i < kSlotCount; i++)
		_frameEvents[i] = exe.readUint32(kFrameEventTable + 4 * i);
	_state = (int16)exe.readUint16(kGlobals);
	_idleTime = exe.readUint32(kGlobals + 0x04);
	_fidgetFixed = (int16)exe.readUint16(kGlobals + 0x08);
	_gravity = exe.readSint32(kGlobals + 0x0c);
	for (int i = 0; i < 5; i++)
		_saved3[i] = (int16)exe.readSint32(kGlobals + 0x10 + 4 * i);
	_costume = exe.readSint32(kGlobals + 0x24);
	_sideStep = exe.readSint32(kGlobals + 0x28);
	_enabled = exe.readSint32(kGlobals + 0x2c) != 0;

	// 0x44f710: NED00000 and Ned's palette (bitmap 0x63)
	_z = kNedZ;
	if (!_game->getNaf(kNedNaf))
		return false;
	_paletteList = _game->readList(kPaletteList, 1);
	if (!_game->loadList(kNedNaf, _paletteList, _paletteList[0].scene))
		return false;
	if (_paletteList[0].hasPalette)
		memcpy(_palette, _paletteList[0].palette, sizeof(_palette));

	// 0x44fbc0
	_lastSound = 0;
	_facing = 1;
	_tickUnit = 0x21;
	listLoad(0);
	listShow(0);
	slot(301).flags |= 0x10;
	slot(301).offsets[0] = 6;
	slot(301).offsets[1] = -40;
	_fidget = (nextRandom() % 11) * 2;
	_fidgetList = _game->readList(kFidgetList + _fidget * 0x24, 2);
	_fidgetLoaded = _game->loadList(kNedNaf, _fidgetList, _game->readList(kFidgetList, 1)[0].scene);
	defaultSlotSetup();
	setCostume(1);
	debugC(1, kDebugLoading, "Ned: costume %d, fidget %d (%s)", _costume, _fidget, _fidgetLoaded ? "loaded" : "missing");
	return true;
}

// 0x44fa00: the normal costume's speeds and flags, and the base slot's anchor
void Ned::defaultSlotSetup() {
	slot(4).flags |= 8;
	_saved3[4] = 0x26;
	slot(317).offsets[0] = 0x26;
	slot(20).flags |= 0x10;
	slot(315).flags |= 0x10;
	slot(316).flags |= 0x10;
	slot(317).flags |= 0x10;
	slot(319).flags |= 0x10;
	slot(322).flags |= 0x10;
	slot(19).flags |= 0x20;
	slot(24).offsets[3] = -44;
	slot(268).offsets[3] = -44;
	_saved3[3] = 0x1c;
	slot(316).offsets[0] = 0x1c;
	slot(279).offsets[3] = 0x1c;
	slot(278).offsets[3] = 0x1c;
	slot(279).offsets[2] = 0x1c;
	slot(278).offsets[2] = 0x1c;
	slot(21).offsets[2] = 0x1c;
	slot(321).offsets[2] = 0x1c;
	_saved3[2] = 0x1b;
	slot(315).offsets[0] = 0x1b;
	slot(272).offsets[5] = 0x2c;
	slot(271).offsets[5] = 0x2c;
	slot(53).flags |= 0x10;
	slot(419).flags |= 0x10;
	slot(52).flags |= 0x10;
	slot(4).offsets[0] = 6;
	_saved3[0] = 0x2d;
	_saved3[1] = 0x28;
	slot(319).offsets[0] = 0x2d;
	slot(322).offsets[0] = 0x28;
	slot(53).offsets[1] = -30;
	slot(25).offsets[3] = -132;
	slot(276).offsets[5] = 0x84;
	slot(21).offsets[3] = -28;
	slot(321).offsets[3] = -28;
	slot(256).offsets[2] = 0x14;
	slot(104).offsets[0] = 3;
	slot(407).offsets[0] = 6;
	slot(120).offsets[0] = -3;
	Slot &base = slot(1);
	int ax = _anchorX, ay = _anchorY;
	boxAnchor(base, ax, ay);
	_halfW = base.w / 2;
	_anchorX = (int16)(ax + _halfW);
	_baseW = base.w;
	_anchorY = (int16)(ay + base.h);
	_baseH = base.h;
	stateReset(kStateIdle);
	_state = kStateIdle;
}

// 0x44f7d0: costume 7 (base slot 376)
void Ned::costume7SlotSetup() {
	slot(379).flags |= 8;
	slot(397).flags |= 0x10;
	slot(398).flags |= 0x10;
	slot(399).flags |= 0x10;
	slot(387).flags |= 0x10;
	slot(384).flags |= 0x20;
	slot(382).flags |= 0x20;
	slot(379).offsets[0] = 4;
	slot(395).offsets[0] = 0x22;
	slot(396).offsets[0] = 0x1e;
	slot(397).offsets[0] = 0x14;
	slot(398).offsets[0] = 0x15;
	slot(399).offsets[0] = 0x1d;
	slot(387).offsets[0] = -2;
	Slot &base = slot(376);
	int ax = _anchorX, ay = _anchorY;
	boxAnchor(base, ax, ay);
	_baseW = base.w;
	_anchorX = (int16)(ax + base.w / 2);
	_halfW = base.w / 2;
	_anchorY = (int16)(ay + base.h);
	_baseH = base.h;
}

void Ned::setCostume(int mode) {
	int16 cur = _states[_state].h[kSlot];
	if (cur > 0 && slot(cur).sprite)
		slot(cur).sprite->setVisibility(false);
	_fidgetFixed = -1;

	uint32 matrix = 0, states = 0, enter = 0, tick = 0;
	switch (mode) {
	case 2:
		matrix = 0x4a85a8; states = 0x4abfa0; enter = 0x4ae578; tick = 0x4ae7b8;
		break;
	case 3:
		matrix = 0x4a8df0; states = 0x4ac508; enter = 0x4ae5d8; tick = 0x4ae818;
		break;
	case 4:
		readState(_states[kStateButton1], 0x4aeb28);
		break;
	case 5:
		matrix = 0x4a9e80; states = 0x4acfd8; enter = 0x4ae638; tick = 0x4ae878;
		break;
	case 6:
		matrix = 0x4a9638; states = 0x4aca70; enter = 0x4ae698; tick = 0x4ae8d8;
		break;
	case 7:
		matrix = 0x4aa6c8; states = 0x4ad540; enter = 0x4ae758; tick = 0x4ae998;
		break;
	case 8:
		slot(294).flags |= 8;
		slot(295).flags |= 0x10;
		slot(299).flags |= 0x20;
		slot(296).flags |= 0x20;
		slot(294).offsets[2] = 6;
		slot(295).offsets[1] = 0x1e;
		matrix = 0x4aaf10; states = 0x4adaa8; enter = 0x4ae6f8; tick = 0x4ae938;
		break;
	default:
		matrix = 0x4ab758; states = 0x4ae010; enter = 0x4ae758; tick = 0x4ae998;
		break;
	}
	if (matrix) {
		const ExeImage &exe = _game->exe();
		for (int i = 0; i < kStateCount * kStateCount; i++)
			_matrix[i] = exe.readUint32(matrix + 4 * i);
		for (int i = 0; i < kStateCount; i++) {
			readState(_states[i], states + 0x3c * i);
			_enterHooks[i] = exe.readUint32(enter + 4 * i);
			_tickHooks[i] = exe.readUint32(tick + 4 * i);
		}
	}
	if (mode == 7) {
		costume7SlotSetup();
		_fidgetFixed = 0x185;
	} else if (mode == 8) {
		_fidgetFixed = 0x125;
	} else if (mode != 2 && mode != 3 && mode != 4 && mode != 5 && mode != 6) {
		defaultSlotSetup();
	}
	_costume = mode;
	if (slot(1).sprite) {
		if (_state == kStateAction && _onAction) {
			_onAction(_onActionUser, _action);
			_action = -1;
			_onAction = nullptr;
		}
		stateReset(kStateIdle);
		_state = kStateIdle;
		_curSlot = 1;
		slot(1).step = 0;
	}
}

void Ned::playAction(int action, Common::Point &pos, int scrollX, int scrollY) {
	if (_states[_state].h[kSlot] > 0)
		hideSlot(_states[_state].h[kSlot]);
	if (action == 2)
		slot(0x35).offsets[1] = -40;
	else if (action == 0x1b)
		slot(0x35).offsets[1] = -50;
	else if (action == 0x3c)
		_facing = _facing == 0;
	_action = action;
	readState(_states[kStateAction], kActionTable + action * 0x3c);
	stateReset(kStateAction);
	int finished;
	stepState(kStateAction, 0, finished);
	_state = kStateAction;
	int16 id = _states[kStateAction].h[kSlot];
	if (slot(id).sprite)
		slot(id).sprite->setVisibility(true);
	_curSlot = id;
	Slot &s = slot(id);
	int x;
	if (_facing) {
		pos.y += s.offsets[5];
		x = s.offsets[4] + pos.x;
	} else {
		pos.y -= s.offsets[5];
		x = pos.x - s.offsets[4];
	}
	pos.x = wrapX(x);
	if (s.flip)
		s.flip->active = _facing == 0;
	placeSprite(id, pos, scrollX, scrollY);
	_curSlot = id;
	debugC(1, kDebugGraphics, "Ned: action %d, slot %d", action, id);
}

void Ned::setAction(int action) {
	_action = action;
	readState(_states[kStateAction], kActionTable + action * 0x3c);
}

void Ned::show(bool visible) {
	if (_curSlot != 0 && slot(_curSlot).sprite)
		slot(_curSlot).sprite->setVisibility(visible);
}

Common::Point Ned::getSpritePosition(int16 id) const {
	if (_curSlot > 0) {
		Slot &s = _game->res().getSlot(kNedArea, id);
		if (s.sprite)
			return s.sprite->getPosition();
	}
	return Common::Point(0, 0);
}

void Ned::adjPosition(int dx, int dy) {
	if (_curSlot > 0 && slot(_curSlot).sprite)
		slot(_curSlot).sprite->adjPosition(dx, dy);
}

int16 Ned::getSlotStep(int16 id) {
	return slot(id).step;
}

bool Ned::isSlotDue(int16 id, uint32 now) {
	return slot(id).deadline < now;
}

bool Ned::isStanding() const {
	return _curSlot == 4 || _curSlot == 0x4d || _curSlot == 0x17b;
}

bool Ned::isState10Ending() const {
	return _state == kStateButton3 && _states[kStateButton3].h[kSlot] == 0x13;
}

void Ned::resetIdle() {
	if (!slot(1).sprite)
		return;
	if (_state == kStateAction && _onAction) {
		_onAction(_onActionUser, _action);
		_action = -1;
		_onAction = nullptr;
	}
	stateReset(kStateIdle);
	_state = kStateIdle;
	_curSlot = 1;
	slot(1).step = 0;
}

// 0x450660
void Ned::resumeWalk(int16 id) {
	int16 step = slot(id).step;
	stateReset(kStateWalk);
	slot(id).step = step;
	int16 *h = _states[kStateWalk].h;
	_states[kStateWalk].flags = (_states[kStateWalk].flags & 0xfffffd1f) | kPhaseLoop;
	h[kSlot] = h[kLoop] != id ? h[kLoop + 1] : h[kLoop];
	h[kLoopIdx] = h[kLoop] != id;
	_state = kStateWalk;
	_curSlot = 4;
}

void Ned::setWrap1(bool wrap, int width, NedXform xform, void *user) {
	_tickUnit = 0x21;
	_xform = xform;
	_xformUser = user;
	_wrapMode = wrap ? 1 : 0;
	_wrapWidth = width;
}

void Ned::setWrap2(bool on, int unused, int tileW, int16 tiles, NedXform xform, void *user) {
	if (!on) {
		_wrapMode = 0;
		_tickUnit = 0x21;
	} else {
		_wrapMode = 2;
		_tickUnit = 0x29;
	}
	_wrapUnused = unused;
	_tileW = tileW;
	_tiles = tiles;
	_xform = xform;
	_xformUser = user;
}

bool Ned::listFree(int list) {
	if (list < 0 || list >= kListCount)
		return false;
	_game->freeList(_lists[list]);
	return true;
}

void Ned::freeMainList() {
	listFree(0);
}

int Ned::listLoad(int list, NedListCallback cb, void *user, int *done) {
	if (list < 0 || list >= kListCount)
		return 0;
	const ExeImage &exe = _game->exe();
	_lists[list] = _game->readList(exe.readUint32(kListTable + 4 * list), exe.readSint32(kListCounts + 4 * list));
	if (_lists[list].empty())
		return 1;
	if (cb && done)
		*done = 0;
	if (!_game->loadList(kNedNaf, _lists[list], _lists[list][0].scene))
		warning("Ned: list %d did not load completely", list);
	if (cb)
		cb(user);
	return 1;
}

bool Ned::listShow(int list) {
	if (list < 0 || list >= kListCount)
		return false;
	for (Request &r : _lists[list]) {
		if (r.type != Request::kSprite || r.area != kNedArea)
			continue;
		Slot &s = slot(r.id);
		if (!s.sprite)
			continue;
		s.sprite->setDrawOrder(_z);
		s.sprite->setDestination(_game->display().getPrimary());
		s.sprite->setVisibility(false);
		if (!(s.flags & kSlotNoFlip))
			applyFlip(s);
	}
	return true;
}

// 0x44fe70: the direction to (tx, ty) by slope bands
static int16 targetDirection(int x, int y, int tx, int ty, int facing) {
	double slope = 0.0;
	if ((double)(x - tx) != 0.0)
		slope = (double)(y - ty) / (double)(x - tx);
	if (facing) {
		if (slope < -3.0)
			return 0x2d;
		if (slope < -0.45)
			return 0x2e;
		if (slope < 0.45)
			return 0x2f;
	} else {
		if (slope > 3.0)
			return 0x2d;
		if (slope > 0.45)
			return 0x2e;
		if (slope > -0.45)
			return 0x2f;
	}
	return 0x30;
}

// 0x4885a0: the octagonal distance
static int distance(int x0, int y0, int x1, int y1) {
	int dx = ABS(x1 - x0), dy = ABS(y1 - y0);
	return dy - (MIN(dx, dy) >> 1) + dx;
}

bool Ned::findTarget(int facing, int16 nedSlot, const Common::Array<Placement> &objects, int16 &dir,
	int &unused, int16 &found) {
	dir = 1;
	found = -1;
	unused = 0;
	if (objects.empty()) {
		dir = 0x2f;
		return false;
	}
	Slot &ned = slot(nedSlot);
	Common::Point p = ned.sprite ? ned.sprite->getPosition() : Common::Point(0, 0);
	int nx = p.x + ned.w / 2, ny = p.y + ned.h / 2;
	int bestX = _facing ? 0x7fff : kNone, bestY = bestX, best = 0x7fff;

	// 0x44ff40 on each entry
	for (const Placement &e : objects) {
		if (e.kind != 2 || e.area < 0 || e.area >= kAreaCount || e.slot < 0 || e.slot >= kAreaSlots)
			continue;
		Slot &s = _game->res().getSlot(e.area, e.slot);
		if (!s.sprite || !s.sprite->getVisibility())
			continue;
		bool flipped = s.flip && s.flip->active;
		int frame = 0, duration = 0;
		seqStep(s, s.step, frame, duration);
		int32 r[4];
		boxRect(s, frame, flipped ? 3 : 0, r);
		Common::Point sp = s.sprite->getPosition();
		int cx, cy;
		if (r[0] == kNone) {
			cx = s.w / 2 + sp.x;
			cy = s.h / 2;
		} else {
			cx = (r[2] - r[0]) / 2 + r[0] + sp.x;
			cy = (r[3] - r[1]) / 2 + r[1];
		}
		cy += sp.y;
		if (_facing ? cx < nx : cx > nx)
			continue;
		int d = distance(cx, cy, nx, ny);
		if (d < best) {
			bestX = cx;
			best = d;
			bestY = cy;
			found = e.slot;
		}
	}
	dir = found == -1 ? 0x2f : targetDirection(nx, ny, bestX, bestY, facing);
	return found != -1;
}

void Ned::getBox(int16 id, const Common::Point &pos, int32 out[4]) {
	Slot &s = slot(id);
	int frame = 0, duration = 0;
	seqStep(s, MAX<int>(s.step, 0), frame, duration);
	boxRect(s, frame, _facing ? 0 : 3, out);
	int px, py;
	placement(id, pos, px, py);
	out[2] += px;
	out[0] += px;
	out[1] += py;
	out[3] += py;
}

void Ned::copyPalette(byte *palette) const {
	memcpy(palette + 11 * 3, _palette + 11 * 3, 31 * 3);
}

void Ned::sceneSwitchBegin() {
	_idleTime = _game->now();
	if (_state == kStateAction) {
		if (_onFinish)
			_onFinish(_onFinishUser, -_action);
		if (_state == kStateAction && _onAction)
			_onAction(_onActionUser, -_action);
	}
	_onFinish = nullptr;
	_onAction = nullptr;
	_fidgetFixed = -1;
	if (_curSlot != 0)
		hideSlot(_curSlot);
	if (slot(1).sprite) {
		stateReset(kStateIdle);
		_state = kStateIdle;
		_curSlot = 1;
		slot(1).step = 0;
	}
}

void Ned::markTime() {
	_idleTime = _game->now();
}

void Ned::setUseSlot(int16 id) {
	_states[kStateUse].h[kStart] = id;
}

} // End of namespace NightmareNed
