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

#include "nightmarened/game.h"
#include "nightmarened/ned.h"
#include "nightmarened/rdx.h"
#include "nightmarened/scene5.h"

namespace NightmareNed {

// 0x41a6f0: the nine walking bathtubs (A) and their legs (B, placed against A). A drop
// landing in a tub (_splash 3) plays its spark (0x10e + i, sound 29) for one walk cycle;
// Ned touching the tub he is in while it sparks gets a shock (action 0x3d). The tub zone
// Ned is in has its floor map in the world: the map of the tub's current step, moved to
// the tub. On maps 6 and 7 the tubs are drawn in front of the floor.
void Scene5::tubsTick(uint32 now, int dx, int dy) {
	CollisionMap *standing = _world.getMapCount() ? _world.getLastHitMap() : nullptr;
	if (standing && (standing == mapN(6) || standing == mapN(7))) {
		if (_tubOrder == 0x72) {
			_tubOrder = 7;
			for (int i = 0; i < kTubs; i++)
				order(_tubA[i], _tubOrder);
			for (int i = 0; i < 9; i++)
				order(260 + i, _tubOrder - 1);
		}
	} else if (_tubOrder == 7) {
		_tubOrder = 0x72;
		for (int i = 0; i < kTubs; i++)
			order(_tubA[i], _tubOrder);
		for (int i = 0; i < 9; i++)
			order(260 + i, 0x6a);
	}
	for (int i = 0; i < kTubs; i++) {
		nudge(_tubA[i], dx, dy);
		nudge(0x10e + i, dx, dy);
	}
	for (int i = 0; i < kTubs; i++) {
		int16 a = _tubA[i], b = _tubB[i];
		if (!step(a, a, now))
			start(a, a, a, now, 0);
		if (!step(b, a, now))
			start(b, b, a, now, 0);
		if (_splash[i] == 3) {
			int16 spark = 0x10e + i;
			int16 st = slot(a).step;
			start(spark, -1, spark, now, st);
			slot(spark).deadline = slot(a).deadline;
			if (slot(a).step == 0) {
				_splashEnd[i] = slot(a).sequenceLength - 1;
				playSound(29);
			} else {
				_splashEnd[i] = slot(a).step - 1;
			}
			_splash[i] = 1;
		}
		if (_splash[i] != 0) {
			int16 spark = 0x10e + i;
			if (slot(spark).step == _splashEnd[i]) {
				switchSlot(-1, spark);
				_splash[i] = 0;
			} else if (!step(spark, spark, now)) {
				start(spark, spark, spark, now, 0);
			}
		}
		if (i != _tubZone)
			continue;
		int16 st = slot(a).step;
		if (_splash[_tubZone] != 0 && _tubMapIndex != -1) {
			int32 r[4], box[4];
			bounds(tubMap(_tubMapIndex), r);
			ned()->getBox(ned()->getCurSlot(), _nedPos, box);
			int16 state = ned()->getState();
			if (intersect(box, r) && state != 0xf && (state == 1 || state == 4))
				ned()->playAction(0x3d, _nedPos, _scrollX, _scrollY);
		}
		if (_tubMapIndex == st)
			continue;
		if (_tubMapIndex != -1)
			removeMap(tubMap(_tubMapIndex));
		_tubMapIndex = st;
		if (st < 0 || st >= kTubMapCount)
			continue;
		int32 *own = _tubMapBounds[st];
		addMap(tubMap(st), own);
		Slot &s = slot(a);
		int dxm = s.x - 0xe8 - _scrollX, dym = s.y - 0xcd - _scrollY;
		_world.moveMap(tubMap(st), Common::Rect(own[0] + dxm, own[1] + dym, own[2] + dxm, own[3] + dym));
		if (ned()->getState() != 0x12 && _world.getLastHitMap() == tubMap(st))
			ned()->dropToFloor(_world, _nedPos, 0x38, 3);
	}
}

// Back to the start of the drain's turning loop (slot 5 against 4)
void Scene5::restartDrain(uint32 now) {
	switchSlot(5, _drainSlot);
	slot(5).step = 0;
	int d = place(5, 4);
	slot(5).deadline = now + (int16)d * kMsPerStep;
	_drainSlot = 5;
}

// 0x41ae20: the drain (4 still, 5 turning against 4). The lever asks for it (_drainWant
// 1 start, 3 stop); it speeds up from 3 ticks a step to 1 (state 1), turns (2), slows
// down (3) and stops (0). Stopped, vision 0 may play once the rats are caught.
void Scene5::drainTick(uint32 now, int dx, int dy) {
	if (_drainIdle && _ratsDone)
		hint(0, _nedPos.x, _nedPos.y, xform);
	nudge(4, dx, dy);
	if (_drainState != _drainWant) {
		bool done = false;
		if (_drainWant == 1) {
			_drainDelta = -0x10;
			if (_drainState != 3) {
				_drainSpeed = 0x300;
				restartDrain(now);
				_drainState = _drainWant;
				done = true;
			}
		} else if (_drainWant == 3) {
			_drainDelta = 0x10;
			if (_drainState != 1) {
				_drainSpeed = 0x100;
				_drainState = _drainWant;
				done = true;
			}
		}
		if (!done)
			_drainState = _drainWant;
	}
	switch (_drainState) {
	case 0:
		_drainIdle = 1;
		return;
	case 1:
		_drainSpeed += _drainDelta;
		_drainIdle = 0;
		if (_drainSpeed < 0x101) {
			_drainState = 2;
			_drainWant = 2;
			return;
		}
		if (!step(_drainSlot, 4, now, (int16)(_drainSpeed >> 8))) {
			restartDrain(now);
			loopSound(5);
			slot(_drainSlot).deadline = now + (_drainSpeed >> 8) * kMsPerStep;
		}
		break;
	case 2:
		if (!step(_drainSlot, 4, now))
			restartDrain(now);
		break;
	case 3:
		_drainSpeed += _drainDelta;
		if (0x300 < _drainSpeed) {
			_drainState = 0;
			_drainWant = 0;
			stopSound(5);
			switchSlot(4, _drainSlot);
			slot(4).step = 0;
			int d = place(4, 4);
			slot(4).deadline = now + (int16)d * kMsPerStep;
			_drainSlot = 4;
			return;
		}
		if (!step(_drainSlot, 4, now, (int16)(_drainSpeed >> 8))) {
			restartDrain(now);
			slot(5).deadline = now + (_drainSpeed >> 8) * kMsPerStep;
		}
		break;
	default:
		break;
	}
}

// 0x41b290: every 4 s (until 13 are caught) a rat drops from one of three holes
// (0xdc + k, sound 6) and falls 12 pixels a tick. Zapped (0xdf + k, sound 7) it is caught
// when it lands: the counter shows one more (200 + n at step 5), and the 13th puts map 3
// in the world. Missed, it splats (0xe5 + k, sound 111).
void Scene5::ratsTick(uint32 now, int dx, int dy) {
	for (int k = 12; k >= 0; k--)
		nudge(200 + k, dx, dy);
	for (int k = 2; k >= 0; k--) {
		nudge(0xdc + k, dx, dy);
		nudge(0xdf + k, dx, dy);
		nudge(0xe5 + k, dx, dy);
	}
	if (_ratLane == -1) {
		if ((int16)_ratsCaught < kRatsNeeded && _ratTime + 4000 < now) {
			int k = (int)_game->crtRandom() % 3;
			_ratLane = (int16)k;
			start(0xdc + k, _ratSlot[k], 0xdc + k, now, 0);
			_ratSlot[k] = (int16)(0xdc + k);
			playSound(6);
			_ratFall = 0;
			_ratTime = now;
			targetAdd(_ratSlot[k], false);
		}
		return;
	}
	if (_ratLane < 0)
		return;
	int k = _ratLane;
	int16 cur = _ratSlot[k];
	if (cur == k + 0xe5) {
		if (!step(cur, cur, now)) {
			switchSlot(-1, cur);
			_ratLane = -1;
			_ratSlot[k] = -1;
		}
		return;
	}
	if (_ratLanded == 0 || cur != k + 0xdf) {
		if (_ratZapped) {
			targetRemove(cur);
			slot(cur).y -= _ratFall;
			place(cur, cur);
			int16 caught = (int16)(0xdf + k);
			start(caught, _ratSlot[k], caught, now, 0);
			_ratSlot[k] = caught;
			playSound(7);
			cur = caught;
			_ratFall -= 0xdc;
			slot(cur).y += _ratFall;
			place(cur, cur);
			_ratZapped = 0;
		}
		Slot &s = slot(cur);
		if (s.deadline < now) {
			if (cur == _ratLane + 0xdc) {
				s.step++;
				if (s.sequenceLength <= s.step)
					s.step = 0;
			}
			s.y += 0xc;
			_ratFall += 0xc;
			if (s.y - _scrollY < 0x580) {
				place(cur, cur);
				s.deadline = now + 1;
				return;
			}
			s.y -= _ratFall;
			place(cur, cur);
			if (cur == _ratLane + 0xdf) {
				_ratLanded = 1;
				return;
			}
			targetRemove(cur);
			int16 splat = (int16)(0xe5 + _ratLane);
			start(splat, _ratSlot[_ratLane], splat, now, 0);
			_ratSlot[_ratLane] = splat;
			playSound(111);
		}
		return;
	}
	// Caught and landed: when its animation ends the counter goes up
	if (!step(cur, cur, now)) {
		switchSlot(-1, cur);
		_ratSlot[k] = -1;
		_ratLane = -1;
		int16 n = (int16)_ratsCaught;
		start(200 + n, -1, 200 + n, now, 5);
		n++;
		_ratsCaught = (_ratsCaught & ~0xffff) | (uint16)n;
		if (n == kRatsNeeded) {
			_ratsDone = 1;
			addMap(mapN(3));
		}
		_ratLanded = 0;
	}
}

// 0x41b910: the nine pipes drip (0xf0 + i); 20-40 s apart one gets ready (0xfa + i) and
// lets a drop go (0x104 + i) that falls 16 pixels a tick into the tub below (sound 10),
// which then sparks; the next drop comes 20-30 s later
void Scene5::dripsTick(uint32 now, int dx, int dy) {
	for (int i = 0; i < kTubs; i++) {
		int16 base = 0xf0 + i;
		nudge(base, dx, dy);
		nudge(0x104 + i, dx, dy);
		int16 drop = _dropSlot[i];
		if (drop != -1 && slot(drop).deadline < now) {
			Slot &s = slot(drop);
			s.step++;
			if (s.sequenceLength <= s.step)
				s.step = 0;
			s.y += 0x10;
			int16 fell = _dropFall[i];
			_dropFall[i] = fell + 0x10;
			if (s.y - _scrollY < 0x580) {
				place(drop, drop);
			} else {
				s.y -= (int16)(fell + 0x10);
				place(drop, drop);
				switchSlot(-1, drop);
				_dropSlot[i] = -1;
				playSound(10);
				if (_splash[i] == 0)
					_splash[i] = 3;
				_dripTime[i] = now + ((int)_game->crtRandom() % 10 + 0x14) * 1000;
			}
		}
		if (!step(_dripState[i] + i, base, now)) {
			if (_dripState[i] == 0xf0) {
				if (_dripTime[i] == 0)
					_dripTime[i] = now + ((int)_game->crtRandom() % 0x14 + 0x14) * 1000;
				if (now < _dripTime[i] || _dropSlot[i] != -1) {
					start(base, _dripState[i] + i, base, now, 0);
					_dripState[i] = 0xf0;
				} else {
					start(0xfa + i, base, base, now, 0);
					_dripState[i] = 0xfa;
				}
			} else if (_dripState[i] == 0xfa) {
				start(base, 0xfa + i, base, now, 0);
				_dripState[i] = 0xf0;
			}
		}
		if (_dripState[i] == 0xfa && 1 < slot(0xfa + i).step && _dropSlot[i] == -1) {
			_dropSlot[i] = start(0x104 + i, -1, 0x104 + i, now, 0);
			_dropFall[i] = 0;
		}
	}
}

// 0x41bd90: the lever. Zapped up (0x6c) it swings down and stops the drain; zapped down
// (0x52) it swings up and starts it (sound 108 both ways).
void Scene5::leverTick(uint32 now, int dx, int dy) {
	nudge(0x52, dx, dy);
	nudge(0x6c, dx, dy);
	if (_leverState != _leverWant) {
		if (_leverWant == 1) {
			targetRemove(_leverSlot);
			_leverSlot = start(0x52, _leverSlot, 0x52, now, 0);
			playSound(108);
			_drainWant = 1;
		} else if (_leverWant == 3) {
			targetRemove(_leverSlot);
			_leverSlot = start(0x6c, _leverSlot, 0x6c, now, 0);
			playSound(108);
			_drainWant = 3;
		}
		_leverState = _leverWant;
	}
	if (_leverState == 1) {
		if (!step(_leverSlot, _leverSlot, now)) {
			_leverSlot = start(0x6c, _leverSlot, 0x6c, now, 0);
			targetAdd(0x6c, false);
			_leverState = _leverWant = 2;
		}
	} else if (_leverState == 3) {
		if (!step(_leverSlot, _leverSlot, now)) {
			_leverSlot = start(0x52, _leverSlot, 0x52, now, 0);
			targetAdd(0x52, false);
			_leverState = _leverWant = 0;
		}
	}
}

// 0x41c0c0: the outhouse door (0xb7 shut, 0x53 open; 313 is the dark inside). Open, map 9
// replaces map 10; it shuts by itself once Ned is in the level's left half. Both door
// slots sit at the wrap-around seam and are kept on Ned's side.
void Scene5::doorTick(uint32 now, int dx, int dy) {
	nudge(0x53, dx, dy);
	nudge(0xb7, dx, dy);
	static const int16 kSeam[] = { 0x53, 0xb7 };
	for (int16 id : kSeam) {
		Slot &s = slot(id);
		int wx = s.x - _scrollX;
		int shift = 0;
		if (_nedPos.x < 0x900) {
			if (wx > 0x900)
				shift = -kWorldW;
		} else if (wx < 0x900) {
			shift = kWorldW;
		}
		if (shift) {
			s.x += shift;
			if (s.sprite)
				s.sprite->adjPosition(shift, 0);
		}
	}
	if (_nedPos.x >= 0 && _nedPos.x < 0x900 && _doorState == 2)
		_doorWant = 3;
	if (_doorState != _doorWant) {
		targetRemove(_doorSlot);
		_doorState = _doorWant;
	}
	if (_doorState == 1) {
		if (step(_doorSlot, _doorSlot, now))
			return;
		visible(313, true);
		_doorSlot = start(0x53, _doorSlot, 0x53, now, 0);
		targetAdd(0x53, true);
		_doorState = _doorWant = 2;
		removeMap(mapN(10));
		addMap(mapN(9));
	} else if (_doorState == 3) {
		visible(313, false);
		if (step(_doorSlot, _doorSlot, now))
			return;
		_doorSlot = start(0xb7, _doorSlot, 0xb7, now, 0);
		targetAdd(0xb7, false);
		_doorState = _doorWant = 0;
		removeMap(mapN(9));
		addMap(mapN(10));
	}
}

// 0x41c490: every 2 s the tentacle (slot 0x1f, sound 31) comes out at one of eight
// places (slots 0x118..0x11f) and goes back
void Scene5::tentacleTick(uint32 now, int dx, int dy) {
	for (int k = 7; k >= 0; k--)
		nudge(0x118 + k, dx, dy);
	if (_tentaclePos != -1 && !step(0x1f, _tentaclePos + 0x118, now)) {
		switchSlot(-1, 0x1f);
		_tentaclePos = -1;
	}
	if (_tentaclePos == -1 && 2000 < now - _tentacleTime) {
		_tentaclePos = (int16)((int)_game->crtRandom() % 8);
		start(0x1f, -1, _tentaclePos + 0x118, now, 0);
		playSound(0x1f);
		_tentacleTime = now;
	}
}

} // End of namespace NightmareNed
