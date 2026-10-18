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
#include "common/system.h"

#include "nightmarened/detection.h"
#include "nightmarened/display.h"
#include "nightmarened/game.h"
#include "nightmarened/ned.h"
#include "nightmarened/nightmarened.h"
#include "nightmarened/rdx.h"
#include "nightmarened/scene7.h"

namespace NightmareNed {

// 0x429b70: the creature at the right end (slots 0x32..0x35 against 0x31). Zapped, it
// swallows Ned (0x34), spits him out on its far side and goes (0x35), which opens the way
// to scene 2. Region 0x419 follows it.
void Scene7::creatureTick(uint32 now, bool init) {
	if (init) {
		_creatureNew = 1;
		return;
	}
	if (_nedPos.x < 7000)
		return;
	if (_creatureNew) {
		show(kMainList, kMainCount, 0x32, 0x96, false);
		targetAdd(0x32);
		_creatureSlot = switchSlot(0x32, -1);
		_creatureNew = 0;
		regionEnable(&_userMap, _creatureRegion, true);
	}
	if (_creatureSlot == -1)
		return;
	if (now <= slot(_creatureSlot).deadline)
		return;
	slot(_creatureSlot).step++;
	if (_creatureSlot == 0x32 && slot(0x32).step == 3)
		playSound(0x32);
	if (_creatureSlot == 0x34) {
		if (slot(0x34).step == 1) {
			ned()->show(false);
			ned()->enable(false);
		} else if (slot(0x34).step == 0x22) {
			_nedPos.x = slot(0x31).x - 0x3c;
			ned()->setBasePosition(_nedPos.x, _nedPos.y);
			ned()->show(true);
			ned()->enable(true);
			nedTick(0, 0);
		}
	}
	Slot &s = slot(_creatureSlot);
	if (s.sequenceLength <= s.step) {
		s.step = 0;
		if (_creatureSlot == 0x33) {
			show(kMainList, kMainCount, 0x34, 0x96, false);
			_creatureSlot = switchSlot(0x34, _creatureSlot);
			playSound(0x34, 0x33);
		} else if (_creatureSlot == 0x34) {
			show(kSlot35List, 2, 0x35, 0x96, false);
			_creatureSlot = switchSlot(0x35, _creatureSlot);
			playSound(0x35, 0x34);
		} else if (_creatureSlot == 0x35) {
			_creatureSlot = -1;
			visible(0x35, false);
			regionEnable(&_userMap, _creatureRegion, false);
			if (_creatureRegion) {
				removeRegion(_creatureRegion);
				_creatureRegion = 0;
			}
			hint(2);
			_creatureDone = 1;
		}
	}
	int frame = 0;
	if (_creatureSlot != -1) {
		int duration = place(_creatureSlot, 0x31, true, &frame);
		slot(_creatureSlot).deadline = now + (int16)duration * kMsPerStep;
	}
	if (_creatureRegion && _creatureSlot != -1) {
		int32 rect[4];
		int pos[2];
		boxRect(_creatureSlot, frame, 0, rect);
		attachPos(_creatureSlot, 0x31, pos, true);
		rect[0] += pos[0];
		rect[2] += pos[0];
		rect[1] += pos[1];
		rect[3] += pos[1];
		regionSetRect(&_userMap, _creatureRegion, rect);
	}
}

// 0x429fb0: the bug that pops out of five holes in turn (slots 0x14..0x17 against 0x13).
// It comes up (0x14), dives (0x16) and taunts (0x15, a target, for 7 s) until zapped (0x17).
void Scene7::bugTick(uint32 now, bool init) {
	static const int32 kHoles[10] = { 0x24a, 0x57, 0x898, 0x46, 0xf7f, 0x6f, 0x1676, 0xa9, 0x1884, 0xac };
	if (init) {
		_bugNew = 1;
		_bugIndex = 0;
		return;
	}
	// Hole 5 reads past the table: the return address and the now argument on the stack
	// (so the sixth bug sits off the level); -1 reads other locals and is treated as no hole
	int16 i = _bugIndex;
	int32 holeX = i >= 0 && i < 5 ? kHoles[i * 2] : (i == 5 ? 0x42cfe5 : 0x7fffffff);
	if (holeX - 0xdc < _nedPos.x) {
		_bugArmed = 1;
		if (i < 5 && i != -1) {
			int32 nextX = i + 1 < 5 ? kHoles[(i + 1) * 2] : 0x42cfe5;
			if (nextX - 0xdc < _nedPos.x) {
				i++;
				_bugNew = 1;
				_bugIndex = i;
			}
		}
	}
	holeX = i >= 0 && i < 5 ? kHoles[i * 2] : (i == 5 ? 0x42cfe5 : 0x7fffffff);
	int32 holeY = i >= 0 && i < 5 ? kHoles[i * 2 + 1] : (int32)now;
	if (_bugSlot != -1 && holeX + _scrollX < -slot(_bugSlot).w)
		return;
	if (!_bugArmed)
		return;
	if (_bugNew) {
		show(kBugList, 9, 0x14, 0x96, false);
		_bugSlot = switchSlot(0x14, _bugSlot);
		_bugNew = 0;
		slot(0x13).x = holeX;
		slot(0x13).y = holeY;
		if (slot(0x13).sprite)
			slot(0x13).sprite->setPosition(holeX, holeY);
	}
	if (_bugSound) {
		playSound(0x14);
		_bugSound = 0;
	}
	if (_bugSlot == -1)
		return;
	if (now <= slot(_bugSlot).deadline)
		return;
	slot(_bugSlot).step++;
	Slot &s = slot(_bugSlot);
	if (s.sequenceLength <= s.step) {
		s.step = 0;
		switch (_bugSlot) {
		case 0x14:
			show(kBugList, 9, 0x16, 0x96, false);
			_bugSlot = switchSlot(0x16, _bugSlot);
			regionEnable(&_userMap, _bugRegion, true);
			playSound(0x16);
			break;
		case 0x15:
			if (now <= _bugTime) {
				playSound(0x15);
				break;
			}
			show(kBugList, 9, 0x16, 0x96, false);
			_bugSlot = switchSlot(0x16, _bugSlot);
			targetRemove(0x15);
			playSound(0x16);
			break;
		case 0x16:
			show(kBugList, 9, 0x15, 0x96, false);
			_bugSlot = switchSlot(0x15, _bugSlot);
			targetAdd(0x15);
			_bugTime = _game->now() + 7000;
			playSound(0x15);
			break;
		case 0x17:
			_bugSlot = -1;
			_bugSound = 1;
			visible(0x17, false);
			targetRemove(0x15);
			_bugIndex++;
			if (5 < _bugIndex)
				_bugIndex = -1;
			if (_bugIndex != -1) {
				int16 n = _bugIndex;
				int32 x = n < 5 ? kHoles[n * 2] : 0x42cfe5;
				int32 y = n < 5 ? kHoles[n * 2 + 1] : (int32)now;
				slot(0x13).x = x;
				slot(0x13).y = y;
				if (slot(0x13).sprite)
					slot(0x13).sprite->setPosition(x, y);
				_bugArmed = 0;
				_bugSlot = switchSlot(0x14, _bugSlot);
			}
			if (_bugRegion && _bugIndex == -1) {
				removeRegion(_bugRegion);
				_bugRegion = 0;
			}
			regionEnable(&_userMap, _bugRegion, false);
			break;
		default:
			break;
		}
	}
	int frame = 0;
	if (_bugSlot != -1) {
		int duration = place(_bugSlot, 0x13, true, &frame);
		slot(_bugSlot).deadline = now + (int16)duration * kMsPerStep;
	}
	if (_bugRegion && _bugSlot != -1) {
		int32 rect[4];
		int pos[2];
		boxRect(_bugSlot, frame, 0, rect);
		attachPos(_bugSlot, 0x13, pos, true);
		rect[0] += pos[0];
		rect[2] += pos[0];
		rect[1] += pos[1];
		rect[3] += pos[1];
		regionSetRect(&_userMap, _bugRegion, rect);
	}
}

// 0x42a660: what a cavity spits (slot 0xb from the jaw slot _dropRef). It falls 30 a step
// to above the floor; if it lands on Ned it swallows him (0x1a5), carries him off (0xc) and
// drops him back on the floor (0x1a4); otherwise it splashes (0xd).
void Scene7::dropTick(uint32 now, bool init) {
	if (init) {
		_dropNew = 1;
		_dropFloorKnown = 0;
		return;
	}
	if (_dropNew) {
		if (_dropSlot == -1)
			return;
		show(kMainList, kMainCount, 0xb, 0x96, false);
		_dropSlot = switchSlotPre(0xb, _dropSlot);
		_dropNew = 0;
		place(_dropSlot, 0xd3, true);
	}
	if (_dropSlot == -1)
		return;
	Slot &ref = slot(_dropRef);
	if (!_dropFloorKnown) {
		for (int16 y = 0; y < 0x280; y++) {
			if (solidAt(kMapFloor, (int16)((int16)(ref.w / 2) + (int16)ref.x), y)) {
				_dropFloorKnown = 1;
				_dropFloorY = y;
				break;
			}
		}
	}
	if (now <= slot(_dropSlot).deadline)
		return;
	if (_dropSlot == 0xb && slot(0xb).step == 5) {
		ref.y += 0x1e;
		if (_dropFloorY - ref.h / 2 - 0x78 < ref.y || _nedHit) {
			slot(_dropSlot).step++;
			ref.y = _dropFloorY - ref.h - 0x78;
		}
	} else {
		slot(_dropSlot).step++;
	}
	Slot &s = slot(_dropSlot);
	if (s.sequenceLength <= s.step) {
		s.step = -1;
		switch (_dropSlot) {
		case 0xd:
			visible(0xd, false);
			_dropSlot = -1;
			_jawArmed = 1;
			_dropFloorKnown = 0;
			break;
		case 0xc: {
			show(kMainList, kMainCount, 0x1a4, 0x96, false);
			_dropSlot = switchSlot(0x1a4, _dropSlot);
			ned()->enable(true);
			_nedPos.x = ref.x + 3;
			ned()->resetIdle();
			bool found = false;
			for (int16 y = 0; y < 0x280; y++) {
				if (!solidAt(kMapFloor, _nedPos.x, y))
					continue;
				found = true;
				if (_nedPos.y < y - 3) {
					int32 rect[4];
					boxRect(0xc, 0, 2, rect);
					_nedPos.y = rect[1] + _dropBoxY;
					ned()->startFall();
				} else {
					_nedPos.y = y;
				}
				break;
			}
			_cameraLock = 0;
			(void)found;
			break;
		}
		case 0xb:
			if (!_nedHit) {
				show(kMainList, kMainCount, 0xd, 0x96, false);
				_dropSlot = switchSlot(0xd, _dropSlot);
			} else {
				show(kMainList, kMainCount, 0x1a5, 6, false);
				_dropSlot = switchSlot(0x1a5, _dropSlot);
				playSound(0xc);
				Slot &c = slot(0xc);
				int frame = 0, duration = 0;
				if (c.sequence)
					c.sequence->getStep(0, frame, duration);
				int16 fx = 0;
				if (c.boxes && frame >= 0 && frame < (int)c.boxes->frames.size())
					fx = c.boxes->frames[frame].offX;
				int32 rect[4];
				boxRect(0xc, frame, 1, rect);
				_cameraLock = 1;
				ref.x = fx - (rect[2] - rect[0]) / 2 - rect[0] + _nedPos.x;
			}
			break;
		case 0x1a4:
			show(kMainList, kMainCount, 0xd, 0x96, false);
			_dropSlot = switchSlot(0xd, _dropSlot);
			_nedHit = 0;
			playSound(0xd);
			_inputLock = 0;
			break;
		case 0x1a5:
			show(kMainList, kMainCount, 0xc, 0x96, false);
			_dropSlot = switchSlot(0xc, _dropSlot);
			ned()->show(false);
			ned()->enable(false);
			ned()->resetIdle();
			playSound(0xc);
			break;
		default:
			break;
		}
	}
	if (_dropSlot != -1) {
		Common::Point pos;
		int duration = place(_dropSlot, _dropRef, true, nullptr, &pos);
		slot(_dropSlot).deadline = now + (int16)duration * kMsPerStep;
		if (_dropSlot == 0xc && slot(0xc).step == 0)
			_dropBoxY = pos.y;
	}
}

// 0x42aca0: the monster a bite calls up (one per hit: 2/4/6/7 against 1, 0x99.. against
// 0x98, 0xa0.. against 0x9f, 0xa7.. against 0xa6). It appears beside Ned on the side whose
// floor is level with his, attacks (a target while it does) and leaves; zapped, it dies.
void Scene7::monsterTick(uint32 now) {
	if (_monsterSlot == -1)
		return;
	if (_jawY != kJawOpenY)
		return;
	_jawArmed = 0;
	if (_monsterNew) {
		// srand(now) + rand(): the side
		_game->crtSeed(_game->now());
		int side = (int)(_game->crtRandom() & 1);
		int16 off = slot(_monsterRef).w;
		if (side == 0) {
			_monsterFlip = 1;
			applyFlip(_monsterSlot, false);
			off = -off;
		} else {
			_monsterFlip = 0;
			applyFlip(_monsterSlot, true);
		}
		// Each side's floor below x + off / 2 must be within 0x1f of Ned's; failing both, the
		// nearer one wins
		int32 remembered = 0;
		int16 next = off, cur = off;
		for (;;) {
			cur = next;
			if (_monsterPlaced)
				break;
			_monsterPlaced = 0;
			for (int16 y = 0; y < 0x281; y++) {
				if (solidAt(kMapFloor, (int16)(_nedPos.x + cur / 2), y)) {
					_monsterFloorY = (_monsterFloorY & ~0xffff) | (uint16)y;
					break;
				}
			}
			int32 keep;
			if (ABS(_monsterFloorY - _nedPos.y) < 0x1f) {
				_monsterPlaced = 1;
				next = cur;
				keep = remembered;
			} else {
				_monsterFlip = _monsterFlip == 0;
				_monsterPlaced = 0;
				applyFlip(_monsterSlot, _monsterFlip == 0);
				int16 other = (int16)remembered;
				next = -cur;
				keep = _monsterFloorY;
				if (other != 0) {
					_monsterPlaced = 1;
					if (ABS(_monsterFloorY - _nedPos.y) < ABS(other - _nedPos.y)) {
						_monsterFlip = _monsterFlip == 0;
						applyFlip(_monsterSlot, _monsterFlip == 0);
						next = cur;
					} else {
						_monsterFloorY = (_monsterFloorY & ~0xffff) | (uint16)other;
					}
					keep = remembered;
				}
			}
			remembered = keep;
		}
		_monsterSlot = switchSlotPre(_monsterSlot, -1);
		Slot &ref = slot(_monsterRef);
		ref.y = _monsterFloorY - ref.h;
		ref.x = cur + _nedPos.x;
		place(_monsterSlot, _monsterRef, _monsterFlip != 0);
		_monsterNew = 0;
		ned()->setFacing(_monsterFlip == 0);
	}
	if (_monsterSlot == -1)
		return;
	if (now <= slot(_monsterSlot).deadline)
		return;
	slot(_monsterSlot).step++;
	Slot &s = slot(_monsterSlot);
	if (s.step < s.sequenceLength) {
		advance(_monsterSlot, _monsterRef, _monsterFlip != 0, now);
		return;
	}
	s.step = -1;
	bool reset = false;   // LAB_0042b4e5: the monster is gone
	int16 attack = -1;    // the next slot, a target
	int16 strike = -1;    // the next slot, the strike (sound 2 over 4)
	uint32 list = 0;
	int count = 5;
	switch (_monsterSlot) {
	case 2:
		attack = 4;
		list = kEnemy0List;
		count = 8;
		break;
	case 4:
		strike = 6;
		list = kEnemy0List;
		count = 8;
		break;
	case 6:
	case 7:
		targetRemove(4);
		visible(_monsterSlot, false);
		reset = true;
		break;
	case 0x99:
		attack = 0x9b;
		list = kEnemy1List;
		break;
	case 0x9b:
		strike = 0x9d;
		list = kEnemy1List;
		break;
	case 0x9d:
	case 0x9e:
		visible(_monsterSlot, false);
		targetRemove(0x9b);
		reset = true;
		break;
	case 0xa0:
		attack = 0xa2;
		list = kEnemy2List;
		break;
	case 0xa2:
		strike = 0xa4;
		list = kEnemy2List;
		break;
	case 0xa4:
	case 0xa5:
		targetRemove(0xa2);
		visible(_monsterSlot, false);
		reset = true;
		break;
	case 0xa7:
		attack = 0xa9;
		list = kEnemy3List;
		break;
	case 0xa9:
		strike = 0xab;
		list = kEnemy3List;
		break;
	case 0xab:
		// The last monster leaves: with four hits taken, the game ends
		stopSound(2);
		targetRemove(0xa9);
		visible(0xab, false);
		_monsterSlot = -1;
		_jawArmed = 1;
		_monsterPlaced = 0;
		_monsterNew = 1;
		_inputLock = 0;
		_monsterGone = 1;
		break;
	case 0xac:
		targetRemove(0xa9);
		visible(0xac, false);
		reset = true;
		break;
	default:
		break;
	}
	if (attack != -1) {
		show(list, count, attack, 0x91, false);
		_monsterSlot = switchSlot(attack, _monsterSlot);
		targetAdd(attack);
		applyFlip(_monsterSlot, _monsterFlip == 0);
	} else if (strike != -1) {
		show(list, count, strike, 0x91, false);
		_monsterSlot = switchSlot(strike, _monsterSlot);
		applyFlip(_monsterSlot, _monsterFlip == 0);
		playSound(2, 4);
		_inputLock = 0;
	} else if (reset) {
		_monsterSlot = -1;
		_jawArmed = 1;
		_monsterPlaced = 0;
		_monsterNew = 1;
		_inputLock = 0;
	}
	if (_monsterSlot != -1)
		advance(_monsterSlot, _monsterRef, _monsterFlip != 0, now);
}

// 0x42b660: region 0x36 hands Ned to slot 8, which carries him from x 0x1cac to 0x1ec1
// (the camera pans there first, drawing as it goes)
void Scene7::liftTick(uint32 now) {
	int16 cur = ned()->getCurSlot();
	if (!_lift || cur == 0x1a3 || cur == 0xfc || cur == 0x1a1 || cur != 1 || _scrollX + _nedPos.x <= 0x29f)
		return;
	if (_liftFirst) {
		_liftFirst = 0;
		ned()->show(false);
		ned()->enable(false);
		_cameraLock = 0;
		_nedPos.x = 0x1cac;
		camera(0);
		while (!_cameraSettled && !_game->getEngine()->shouldQuit()) {
			camera(0);
			_game->display().draw();
		}
		show(kSlot8List, 4, 8, 0x91, true);
		place(8, 8, true);
		_cameraLock = 1;
		_nedPos.x = 0x1ec1;
		_nedPos.y = 0x16e;
		playSound(8);
	}
	if (now <= slot(8).deadline)
		return;
	slot(8).step++;
	if (slot(8).step == 10) {
		_tongueRef = _stillSlots[3];
		_tongueSlot = switchSlotPre(0x19, _tongueRef);
		place(_tongueSlot, _tongueRef, true);
		playSound(0x19);
	}
	if (slot(8).sequenceLength <= slot(8).step) {
		slot(8).step = 0;
		visible(8, false);
		_lift = 0;
		_cameraLock = 0;
		_nedPos.x = 0x1ec1;
		_nedPos.y = 0x16e;
		ned()->show(true);
		ned()->enable(true);
		nedTick(0, 0);
		_liftFirst = 1;
	}
	advance(8, 8, true, now);
}

// 0x42b9c0: the pop where a cavity was zapped (slot 0xb9 at _popRef)
void Scene7::popTick(uint32 now) {
	if (!_pop || now <= slot(0xb9).deadline)
		return;
	slot(0xb9).step++;
	if (slot(0xb9).sequenceLength <= slot(0xb9).step) {
		slot(0xb9).step = 0;
		visible(0xb9, false);
		_pop = 0;
		if (_dropAfterPop) {
			_dropAfterPop = 0;
			ned()->show(false);
			ned()->startFall();
		}
	}
	advance(0xb9, _popRef, true, now);
}

// The slot 9 pop where a tooth spat (inline in 0x42cf90)
void Scene7::anim9Tick(uint32 now) {
	if (!_anim9 || now <= slot(9).deadline)
		return;
	slot(9).step++;
	if (slot(9).sequenceLength <= slot(9).step) {
		slot(9).step = 0;
		_anim9 = 0;
		visible(9, false);
	}
	advance(9, _popRef, true, now);
}

// 0x42baa0: the regions Ned's landing slot 0x1a3 touches start the gum (0x12) and tongue
// (0x19) animations; 0x36 also starts the lift. Then whether the bite may catch Ned.
void Scene7::regionTick(bool init) {
	int16 cur = ned()->getCurSlot();
	if (init) {
		_tongueLoad = 1;
		_gumLoad = 1;
		return;
	}
	uint32 flags;
	int32 id;
	int32 rect[4];
	ned()->getRegion(flags, id, rect);
	if (cur == 0x1a3) {
		int16 gumRef = -1, tongueRef = -1;
		bool gumOld11 = false;
		switch (id) {
		case 0x1f:
			if (_gumLoad) {
				show(kSlot12List, 6, 0x12, 0x96, false);
				_gumLoad = 0;
			}
			_gumRef = 0x10;
			_gumSlot = switchSlotPre(0x12, 0x11);
			place(_gumSlot, _gumRef, true);
			_gumNext = 0x11;
			playSound(0x12);
			gumOld11 = true;
			break;
		case 0x26:
			gumRef = 0xd2;
			break;
		case 0x34:
			if (_gumLoad) {
				show(kSlot12List, 6, 0x12, 0x96, false);
				_gumLoad = 0;
			}
			gumRef = 0xd0;
			break;
		case 0x48:
			gumRef = 0xd1;
			break;
		case 0x28:
			tongueRef = _stillSlots[1];
			break;
		case 0x35:
		case 0x36:
			if (_tongueLoad) {
				show(kSlot12List, 6, 0x19, 0x96, false);
				_tongueLoad = 0;
			}
			tongueRef = id == 0x35 ? _stillSlots[0] : _stillSlots[3];
			break;
		case 0x186:
		case 0x2ee:
		case 0x2f8:
			tongueRef = _stillSlots[2];
			break;
		default:
			break;
		}
		if (!gumOld11 && gumRef != -1) {
			_gumRef = gumRef;
			_gumSlot = switchSlotPre(0x12, gumRef);
			place(_gumSlot, _gumRef, true);
			_gumNext = _gumRef;
			playSound(0x12);
		}
		if (tongueRef != -1) {
			_tongueRef = tongueRef;
			_tongueSlot = switchSlotPre(0x19, _tongueRef);
			place(_tongueSlot, _tongueRef, true);
			if (!_game->isVoicePlaying(slot(0x19).voice))
				playSound(0x19);
			if (id == 0x36) {
				_cameraLock = 1;
				_lift = 1;
				ned()->setFacing(1);
			}
		}
	}
	static const int16 kBusy[] = { 5, 6, 0x10d, 0x10e, 0x10f, 0x17, 0x18, 0x19, 0x110, 0x112, 0x111, 0x114, 0x113, 0x10c };
	bool busy = ned()->isHanging() || _dropSlot != -1 || _dropAfterPop;
	for (int16 b : kBusy)
		busy = busy || cur == b;
	_jawArmed = busy ? 0 : 1;
}

// 0x42c040: the gum animation, then its next slot again
void Scene7::gumTick(uint32 now) {
	if (_gumSlot == -1 || now <= slot(_gumSlot).deadline)
		return;
	slot(_gumSlot).step++;
	if (slot(_gumSlot).sequenceLength <= slot(_gumSlot).step) {
		slot(_gumSlot).step = 0;
		switchSlot(_gumNext, _gumSlot);
		_gumSlot = -1;
		return;
	}
	advance(_gumSlot, _gumRef, true, now);
}

// 0x42bf30: the tongue animation, then its still slot again
void Scene7::tongueTick(uint32 now) {
	if (_tongueSlot == -1 || now <= slot(_tongueSlot).deadline)
		return;
	slot(_tongueSlot).step++;
	if (slot(_tongueSlot).sequenceLength <= slot(_tongueSlot).step) {
		slot(_tongueSlot).step = 0;
		switchSlot(_tongueRef, _tongueSlot);
		place(_tongueRef, _tongueRef, true);
		_tongueSlot = -1;
		return;
	}
	advance(_tongueSlot, _tongueRef, true, now);
}

// 0x42c120: the five slots that always loop
void Scene7::loopTick(uint32 now) {
	for (int i = 0; i < 5; i++) {
		int16 id = _loopSlots[i];
		if (id == -1 || now <= slot(id).deadline)
			continue;
		slot(id).step++;
		if (slot(id).sequenceLength <= slot(id).step)
			slot(id).step = 0;
		advance(id, _loopRefs[i], true, now);
	}
}

// 0x42c4b0: the anchors and marks on the upper jaw follow it
void Scene7::jawMarksPlace() {
	for (int i = 0; i < 8; i++) {
		slot(_jawAnchors[i]).y = _jawAnchorY[i] + _jawY;
		place(_jawAnchors[i], _jawAnchors[i], true);
	}
	for (int i = 0; i < 4; i++) {
		slot(_jawMarks[i]).y = _jawMarkY[i] + _jawY;
		place(_jawMarks[i], _jawMarks[i], true);
	}
}

// 0x42c570: the teeth follow the upper jaw; the gates move with the scroll
void Scene7::teethPlace(int dx, int dy) {
	for (int i = 0; i < 12; i++) {
		if (_teeth[i] == -1)
			continue;
		slot(_teeth[i]).y = _teethY[i] + _jawY;
		place(_teeth[i], _teeth[i], true);
	}
	for (int16 id : _gates) {
		if (id != -1)
			adj(id, dx, dy);
	}
}

// 0x42c820: the upper jaw opens (0x30 up every 33 ms) and stays open for 30 s, then bites
// (0x30 down) and stays shut for 1 s. While it comes down, a tooth region over Ned catches him.
void Scene7::jawTick(uint32 now) {
	if (!_jawArmed || hintActive())
		return;
	if (_jawStepTime < now && _jawShutTime < now) {
		_jawStepTime = now + 0x21;
		if (_jawTime < now) {
			if (!_jawOpen) {
				_jawShutTime = 0;
				_jawY -= 0x30;
				if (!_jawMoving) {
					removeMap(kMapJawShut);
					removeMap(kMapJaw);
					_jawMoving = 1;
				}
				if (-0x115 < _jawY) {
					moveMapBy(kMapJaw, -0x30);
				} else {
					moveMapBy(kMapJaw, -0x144 - _jawY);
					_jawTime = now + 30000;
					_jawY = kJawOpenY;
					_jawOpen = 1;
					addMap(kMapJawOpen);
					_jawOpenMapIn = 1;
				}
			} else {
				_jawY += 0x30;
				if (_jawY < 1) {
					moveMapBy(kMapJaw, 0x30);
				} else {
					moveMapBy(kMapJaw, 0x30 - _jawY);
					_jawShutTime = now + 1000;
					_jawY = 0;
					_jawOpen = 0;
					addMap(kMapJawShut);
					_jawMoving = 0;
					playSound(0xfa);
				}
			}
		}
	}
	if (_jawY != kJawOpenY && _jawOpenMapIn) {
		removeMap(kMapJawOpen);
		addMap(kMapJaw);
		_jawOpenMapIn = 0;
	}
	if (!_jawOpen || now <= _jawTime)
		return;
	int16 cur = ned()->getCurSlot();
	int32 box[4];
	ned()->getBox(cur, _nedPos, box);
	CollisionMap *jaw = map(kMapJaw);
	if (!jaw)
		return;
	for (const CollisionMap::Region &r : jaw->regions) {
		if (r.type & 0x80000000)
			continue;
		int32 rect[4] = { r.rect.left, r.rect.top, r.rect.right, r.rect.bottom };
		if (!intersect(rect, box) || cur == 0x81 || cur == 0x36)
			continue;
		// Caught: Ned is bitten and a monster comes for him
		debugC(1, kDebugGraphics, "Scene 7: bitten under region %d, hit %d", r.id, _hits);
		ned()->dropToFloor(_world, _nedPos, 300, 2);
		ned()->playAction(0x13, _nedPos, _scrollX, _scrollY);
		_inputLock = 1;
		switch (_hits) {
		case 0:
			show(kEnemy0List, 8, 2, 0x91, false);
			ensureFlip(2);
			_monsterSlot = 2;
			_monsterRef = 1;
			playSound(2);
			return;
		case 1:
			show(kEnemy1List, 5, 0x99, 0x91, false);
			ensureFlip(0x99);
			_monsterSlot = 0x99;
			_monsterRef = 0x98;
			break;
		case 2:
			show(kEnemy2List, 5, 0xa0, 0x91, false);
			ensureFlip(0xa0);
			_monsterSlot = 0xa0;
			_monsterRef = 0x9f;
			break;
		case 3:
			show(kEnemy3List, 5, 0xa7, 0x91, false);
			ensureFlip(0xa7);
			_monsterSlot = 0xa7;
			_monsterRef = 0xa6;
			break;
		default:
			return;
		}
		playSound(2);
		return;
	}
}

// 0x42f470: past four points the cavities spit by themselves (slot 9 on the tooth, then 0xb
// falls from the cavity)
void Scene7::spitTick(bool init) {
	static const struct {
		int x;
		int16 tooth, anchor, cavity;
		int index;
	} kSpits[4] = {
		{ 0x725, 0x91, 0xc6, 0xd3, 0 },
		{ 0xb02, 0x92, 0xc8, 0xd5, 2 },
		{ 0xc16, 0x93, 0xc9, 0xd6, 3 },
		{ 0x17a9, 0x95, 0xcc, 0xd8, 5 }
	};
	if (init) {
		for (int i = 0; i < 4; i++)
			_spit[i] = 0;
		return;
	}
	for (int i = 0; i < 4; i++) {
		if (_nedPos.x <= kSpits[i].x || _spit[i])
			continue;
		show(kMainList, kMainCount, 9, 6, false);
		int16 s = switchSlot(9, kSpits[i].tooth);
		place(s, kSpits[i].anchor, true);
		targetRemove(kSpits[i].tooth);
		_anim9 = 1;
		_dropRef = kSpits[i].cavity;
		visible(_jawSlots[kSpits[i].index], false);
		_jawSlots[kSpits[i].index] = -1;
		_dropSlot = switchSlotPre(0xb, _dropSlot);
		place(_dropSlot, _dropRef, true);
		_popRef = kSpits[i].anchor;
		_jawArmed = 0;
		regionEnable(map(kMapJaw), kSpits[i].tooth, false);
		_spit[i] = 1;
		playSound(9);
	}
}

// The two hit tests inline in 0x42cf90: what a cavity spat (box 1) against Ned marks him
// hit; the monster (box 1, or 4 flipped) against Ned hurts him, and the fourth hurt is lost
void Scene7::hitChecks() {
	if (_dropSlot != -1) {
		int16 cur = ned()->getCurSlot();
		int32 rect[4], box[4];
		boxRect(_dropSlot, currentFrame(_dropSlot, true), 1, rect);
		ned()->getBox(cur, _nedPos, box);
		Common::Point pos;
		if (slot(_dropSlot).sprite)
			pos = slot(_dropSlot).sprite->getPosition();
		rect[0] += pos.x;
		rect[2] += pos.x;
		rect[3] += pos.y;
		rect[1] += pos.y;
		box[2] += _scrollX;
		box[1] += _scrollY;
		box[3] += _scrollY;
		box[0] += _scrollX;
		if (intersect(rect, box))
			_nedHit = 1;
	}
	if (_monsterSlot != -1) {
		int16 cur = ned()->getCurSlot();
		int32 rect[4], box[4];
		boxRect(_monsterSlot, currentFrame(_monsterSlot, true), _monsterFlip ? 1 : 4, rect);
		ned()->getBox(cur, _nedPos, box);
		Common::Point pos;
		if (slot(_monsterSlot).sprite)
			pos = slot(_monsterSlot).sprite->getPosition();
		rect[0] += pos.x;
		rect[2] += pos.x;
		rect[3] += pos.y;
		rect[1] += pos.y;
		box[1] += _scrollY;
		box[3] += _scrollY;
		box[0] += _scrollX;
		box[2] += _scrollX;
		cur = ned()->getCurSlot();
		if (intersect(rect, box) && cur != 0x98 && cur != 0x36) {
			ned()->playAction(0x28, _nedPos, _scrollX, _scrollY);
			playSound(4, 2);
			_hits++;
			_nedHit = 1;
			debugC(1, kDebugGraphics, "Scene 7: hurt by monster %d, hits %d", _monsterSlot, _hits);
			if (_hits > 3) {
				_hits = 3;
				_lost = 1;
			}
		}
	}
}

// 0x42dac0: Ned's zap hit a target
int Scene7::hitTarget(int16 found) {
	// The cavities: slot 0xb9 (or 9 where a cavity spits) pops at the anchor and the tooth's
	// region in the jaw map goes, so the bite no longer catches Ned there
	static const struct {
		int16 tooth, pop, anchor;
	} kCavities[] = {
		{ 0x8c, 0xb9, 0xc2 }, { 0x8d, 0xb9, 0xc3 }, { 0x95, 9, 0xcc }, { 0xc0, 0xb9, 0xce }, { 0xc1, 0xb9, 0xcf }
	};
	debugC(1, kDebugGraphics, "Scene 7: zapped %d", found);
	for (const auto &c : kCavities) {
		if (c.tooth != found)
			continue;
		show(kMainList, kMainCount, c.pop, 6, false);
		int16 s = switchSlot(c.pop, found);
		place(s, c.anchor, true);
		targetRemove(found);
		if (c.pop == 9)
			_anim9 = 1;
		else
			_pop = 1;
		_popRef = c.anchor;
		regionEnable(map(kMapJaw), found, false);
		playSound(9);
		return 1;
	}
	// The cavities that spit when zapped
	static const struct {
		int16 tooth, anchor, cavity;
		int index;
	} kSpitters[] = {
		{ 0x8e, 0xc7, 0xd4, 1 }, { 0x8f, 0xca, 0xd7, 4 }, { 0x90, 0xcd, 0xd9, 6 }
	};
	for (const auto &c : kSpitters) {
		if (c.tooth != found)
			continue;
		show(kMainList, kMainCount, 9, 6, false);
		int16 s = switchSlot(9, found);
		place(s, c.anchor, true);
		targetRemove(found);
		_anim9 = 1;
		_popRef = c.anchor;
		_dropSlot = switchSlotPre(0xb, -1);
		place(_dropSlot, c.cavity, true);
		_dropRef = c.cavity;
		visible(_jawSlots[c.index], false);
		_jawSlots[c.index] = -1;
		_jawArmed = 0;
		regionEnable(map(kMapJaw), found, false);
		playSound(9);
		return 1;
	}

	switch (found) {
	case 4:
	case 0x9b:
	case 0xa2:
	case 0xa9: {
		// A monster: only once Ned has been up the left ladder
		if (!_armed)
			return 1;
		int16 dying, ref;
		uint32 list;
		int count = 5;
		if (found == 4) {
			dying = 7;
			ref = 1;
			list = kEnemy0List;
			count = 8;
		} else if (found == 0x9b) {
			dying = 0x9e;
			ref = 0x98;
			list = kEnemy1List;
		} else if (found == 0xa2) {
			dying = 0xa5;
			ref = 0x9f;
			list = kEnemy2List;
		} else {
			dying = 0xac;
			ref = 0xa6;
			list = kEnemy3List;
		}
		show(list, count, dying, 0x91, false);
		_monsterSlot = switchSlot(dying, _monsterSlot);
		place(_monsterSlot, ref, _monsterFlip != 0);
		applyFlip(_monsterSlot, _monsterFlip == 0);
		playSound(7, 4);
		return 1;
	}
	case 0x15:
		// The bug
		show(kBugList, 9, 0x17, 6, false);
		_bugSlot = switchSlot(0x17, _bugSlot);
		place(_bugSlot, 0x13, true);
		targetRemove(0x15);
		playSound(0x17);
		return 1;
	case 0x32:
		// The creature, from its left only
		if (slot(0x31).x <= _nedPos.x)
			return 1;
		show(kSlot8List, 4, 0x33, 0x96, false);
		_creatureSlot = switchSlot(0x33, 0x32);
		place(_creatureSlot, 0x31, true);
		targetRemove(0x32);
		playSound(0x33, 0x32);
		return 1;
	case 0x96:
	case 0xdd: {
		// The gates: the gate map loses its mask and the gate opens
		bool a = found == 0x96;
		show(kMainList, kMainCount, 0xb9, 6, false);
		int16 s = switchSlot(0xb9, found);
		place(s, a ? 0xc4 : 0xde, true);
		targetRemove(found);
		_pop = 1;
		_popRef = a ? 0xc4 : 0xde;
		playSound(9);
		clearMask(map(a ? kMapGateA : kMapGateB));
		removeMap(a ? kMapGateA : kMapGateB);
		regionEnable(map(kMapFloor), a ? 0x11 : 0x2b, false);
		regionEnable(map(kMapFloor), a ? 0x12 : 0x2c, false);
		visible(a ? 0xdf : 0xe0, true);
		place(a ? 0xdf : 0xe0, a ? 0xdf : 0xe0, true);
		if (!ned()->dropToFloor(_world, _nedPos, 0x14, 2))
			_dropAfterPop = 1;
		return 1;
	}
	case 0x97:
		// The plug: the floor regions under it come back
		show(kMainList, kMainCount, 0xb9, 6, false);
		place(switchSlot(0xb9, 0x97), 0xc5, true);
		targetRemove(0x97);
		_pop = 1;
		regionEnable(map(kMapFloor), 0x186, true);
		regionEnable(map(kMapFloor), 0x2ee, true);
		regionEnable(map(kMapFloor), 0x2f8, true);
		_popRef = 0xc5;
		playSound(9);
		return 1;
	default:
		return 1;
	}
}

} // End of namespace NightmareNed
