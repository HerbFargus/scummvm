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
#include "nightmarened/media.h"
#include "nightmarened/ned.h"
#include "nightmarened/rdx.h"
#include "nightmarened/scene5.h"

namespace NightmareNed {

// ---- Sub-scene 0: the toilet hall ----

Scene5Toilets::Scene5Toilets(Game *game) : Scene5Base(game, 0) {
	const ExeImage &exe = _game->exe();
	for (int i = 0; i < kLanes; i++) {
		_laneSlots[i] = (int16)exe.readUint16(0x495478 + 4 * i);
		_lane[i] = -1;
		_laneTime[i] = 0;
	}
	_paletteVA = kPaletteVA;
	for (int i = 0; i < kPipes; i++)
		_pipeSlot[i] = -1;
	_active = 0;
	_far = _still = _main = _mid = nullptr;
	memset(_rects, 0, sizeof(_rects));
	_chainPulls = 0;
	_midRemX = _midRemY = _farRemX = _farRemY = 0;
	_chainUnpulled = 1;
	_scrollX = _scrollY = 0;
	_exit = 0;
	_jumped = 0;
	_screenX = _screenY = 0;
	_scrollTime = 0;
	_camNedX = _camNedY = 0;

	_hooks.use = hookUse;
	_hooks.region = hookRegion;
	_hooks.findTarget = hookFindTarget;
	_hooks.hitTarget = hookHitTarget;
}

// 0x478a20
int Scene5Toilets::load(int fromScene, int fromSub) {
	nedLoad(fromScene);
	return submit(kList, kCount, 2);
}

// 0x478a40
int Scene5Toilets::enter(int fromScene, int fromSub) {
	uint32 now = _game->now();
	_game->media().playMusic(kMusic);
	if (!isLoaded(kLast))
		return 5;
	attachList(kList, kCount);
	if (_nedListLoaded)
		ned()->listShow(kNedList);
	ned()->copyPalette(_palette);
	_game->display().applyPalette(_palette, kPalAll);
	_world = CollisionWorld();
	addMap(map(kMap1));
	addMap(map(kMap2));
	_targets.clear();
	for (int i = 0; i < kLanes; i++) {
		visible(_laneSlots[i], false);
		order(_laneSlots[i], 100);
		_lane[i] = -1;
		_laneTime[i] = 0;
	}
	_active = 0;
	for (int j = 0; j < kPipes; j++) {
		visible(0x14a + j, false);
		order(0x14a + j, 0x66);
		showSlot(0x14d + j, 0x66, false);
		showSlot(0x150 + j, 0x66, false);
		showSlot(0x153 + j, 0x66, false);
		_pipeSlot[j] = start(0x14d + j, -1, 0x14a + j, now, -1);
	}
	_chainUnpulled = 1;
	_jumped = 0;
	visible(0x7c, true);
	order(0x7c, 0x65);
	start(0x7c, -1, 0x7c, now, 0);
	targetAdd(0x7c, false);
	_chainPulls = 0;
	_main = showBitmap(kMainBitmap, 0x68);
	_still = showBitmap(kStillBitmap, 0x6b);
	_mid = showBitmap(kMidBitmap, 0x6a);
	_far = showBitmap(kFarBitmap, 0x69);
	_midRemX = _midRemY = _farRemX = _farRemY = 0;
	_game->res().getSlot(1, 4).offsets[0] = 6;
	ned()->setWrap1(false, 0, xform, this);
	_nedPos = Common::Point(0x6c4, 0);
	ned()->setFacing(0);
	ned()->show(false);
	ned()->startFall();
	_scrollX = 0;
	_scrollY = 0x1e0 - (_main ? _main->bitmap.surface.h : 0x1e0);
	if (!loadSst(1))
		return 5;
	applySst(_scrollX, _scrollY);
	static const int16 kPieces[][2] = { { 322, 3 }, { 327, 3 }, { 328, 3 }, { 329, 0x67 } };
	for (const auto &p : kPieces) {
		visible(p[0], true);
		order(p[0], p[1]);
	}
	regionRects(map(kMap3), _rects, 100, 8);
	_exit = 0;
	_lastDraw = 0;
	return 0;
}

// 0x478f60
int Scene5Toilets::leave(int toScene, int toSub) {
	stopAllSounds();
	// The overlay would draw the maps freed here during the exit clip
	_game->display().setCollisionOverlay(Common::Array<CollisionMap *>(), Common::Point());
	_sst.clear();
	ned()->setWrap1(false, 0, nullptr, nullptr);
	release(kList, kCount);
	_main = _still = _mid = _far = nullptr;
	if (_exit == 1) {
		_game->media().loadClip(0x2c);
		Scene5Washroom *room = (Scene5Washroom *)_game->getScene(5, 1);
		if (room)
			room->armHint();
	} else if (_exit == 2) {
		_game->media().loadClip(0x20);
	}
	_world = CollisionWorld();
	_targets.clear();
	if (toScene != 5) {
		ned()->listFree(kNedList);
		_game->closeNaf(kNaf);
	}
	return 0;
}

// 0x479000: down the ladder (8744) or fallen through the floor (8732)
int Scene5Toilets::unload(int toScene, int toSub) {
	if (_exit == 1)
		_game->media().playClipWithMusic(1, 0x2c);
	else if (_exit == 2)
		_game->media().playClipWithMusic(1, 0x20);
	return 0;
}

// 0x479a50
void Scene5Toilets::xform(void *user, int &x, int &y) {
	Scene5Toilets *s = (Scene5Toilets *)user;
	x += s->_scrollX;
	y += s->_scrollY;
}

// 0x479a70: 0x41cea0's camera without the freeze, standing still in state 10; a big move
// (more than half the room) adds Ned's slot-4 offset to the catching-up distance
void Scene5Toilets::cameraWant(uint32 now, int d[2]) {
	d[0] = d[1] = 0;
	int16 state = ned()->getState();
	if (state == 10)
		return;
	int half = (_main ? _main->bitmap.surface.w : 0) / 2;
	bool jump = half < _camNedX - _nedPos.x || _camNedX - _nedPos.x < -half;
	int16 extra = _game->res().getSlot(1, 4).offsets[0];
	_camNedY = _nedPos.y;
	int sy = _nedPos.y + _scrollY;
	_camNedX = _nedPos.x;
	int sx = _nedPos.x + _scrollX;
	d[1] = 9999;
	int v = 0;
	bool set = false;
	if (ned()->getFacing() == 0) {
		bool far = false;
		if (state == 7) {
			if (_scrollTime + 0x12 < now) {
				d[0] = 1;
				d[1] = 1;
			}
		} else if (state == 8) {
			if (_scrollTime + 0xc < now) {
				d[0] = 1;
				d[1] = -1;
			}
		} else if (state == 1 && 0x1b7 < sx && !jump) {
			if (_scrollTime + 0xc < now)
				d[0] = 1;
		} else if (state == 1 && sx <= 0x1b7) {
			far = true;
		} else if (sx < 0x1b9) {
			far = sx <= 0x1b7;
		} else {
			v = sx - 0x1b8;
			if (jump)
				v += extra;
			if (v < 7)
				v = 1;
			else if (v < 0xd)
				v = 2;
			else if (v < 0x19)
				v = 4;
			else if (v < 0x31)
				v = 8;
			else if (v < 0x141)
				v = 0xc;
			v = -v;
			set = true;
		}
		if (far) {
			v = 0x1b8 - sx;
			if (v < 7)
				v = 1;
			else if (v < 0xd)
				v = 2;
			else if (v < 0x19)
				v = 4;
			else if (v < 0x31)
				v = 8;
			else if (v <= 0x140)
				v = 0xc;
			set = true;
		}
	} else {
		bool right = false;
		if (state == 7) {
			if (_scrollTime + 0x12 < now) {
				d[0] = -1;
				d[1] = 1;
			}
		} else if (state == 8) {
			if (_scrollTime + 0xc < now) {
				d[0] = -1;
				d[1] = -1;
			}
		} else if (state == 1 && sx < 0xc9 && !jump) {
			if (_scrollTime + 0xc < now)
				d[0] = -1;
		} else if (state == 1 && sx >= 0xc9) {
			right = true;
		} else if (sx < 200) {
			v = 200 - sx;
			if (jump)
				v += extra;
			if (v < 7)
				v = 1;
			else if (v < 0xd)
				v = 2;
			else if (v < 0x19)
				v = 4;
			else if (v < 0x31)
				v = 8;
			else if (v < 0x141)
				v = 0xc;
			set = true;
		} else {
			right = sx >= 0xc9;
		}
		if (right) {
			v = sx - 200;
			if (v < 7)
				v = -1;
			else if (v < 0xd)
				v = -2;
			else if (v < 0x19)
				v = -4;
			else if (v < 0x31)
				v = -8;
			else if (0x140 < v)
				v = -v;
			else
				v = -0xc;
			set = true;
		}
	}
	if (set)
		d[0] = v;
	if (d[1] == 9999) {
		v = sy - 400;
		if (v < 0) {
			if (v < -100)
				d[1] = 0x10;
			else
				d[1] = -8 < v ? 1 : 8;
		} else if (v < 1) {
			d[1] = 0;
		} else if (v < 0x65) {
			d[1] = v < 8 ? -1 : -8;
		} else {
			d[1] = -0x10;
		}
	}
	if (d[0] != 0 || d[1] != 0)
		_scrollTime = _game->now();
	_screenX = sx;
	_screenY = sy;
}

// 0x479680: the three bubbling pipes (0x14a + j). Before the chain is pulled they play
// 0x14d + j (sound 52), after it 0x150 + j (sound 54); the jump at region 2 makes the
// first one play 0x153 (its sound slot 53 has none)
void Scene5Toilets::pipesTick(uint32 now, int dx, int dy) {
	for (int j = 0; j < kPipes; j++) {
		int16 ref = 0x14a + j;
		nudge(ref, dx, dy);
		if (j == 0 && _jumped && _pipeSlot[0] != 0x153) {
			_pipeSlot[0] = start(0x153, _pipeSlot[0], 0x14a, now, 0);
			playSound(53);
			continue;
		}
		if (step(_pipeSlot[j], ref, now))
			continue;
		int16 next = _chainUnpulled ? 0x14d + j : 0x150 + j;
		_pipeSlot[j] = start(next, _pipeSlot[j], ref, now, 0);
		playSound(_chainUnpulled ? 0x34 : 0x36);
		_jumped = 0;
	}
}

// 0x479630: the monster in a toilet is up (past its first two steps, before its last two)
bool Scene5Toilets::monsterGrabbing(int lane) {
	int16 s = _lane[lane];
	if (s == -1)
		return false;
	if (slot(s).step < 2)
		return false;
	return slot(s).step < slot(s).sequenceLength - 2;
}

// 0x479050 (no throttle of its own; run at 41 ms here)
int Scene5Toilets::tick(uint32 input, uint32 now) {
	if (now <= _lastDraw + kMsPerStep) {
		g_system->delayMillis(MIN<uint32>(_lastDraw + kMsPerStep + 1 - now, 10));
		return 0;
	}
	nedTick(input, now, _scrollX, _scrollY);
	int h = _main ? _main->bitmap.surface.h : 0x1e0;
	int w = _main ? _main->bitmap.surface.w : 0x280;
	if (h + 400 < _nedPos.y)
		_exit = 2;
	int d[2];
	cameraWant(now, d);
	if (d[0] + _scrollX > 0) {
		d[0] = -_scrollX;
	} else if (d[0] + _scrollX < 0x280 - w) {
		d[0] = (0x280 - w) - _scrollX;
	}
	if (d[1] + _scrollY > 0) {
		d[1] = -_scrollY;
	} else if (d[1] + _scrollY < 0x1e0 - h) {
		d[1] = (0x1e0 - h) - _scrollY;
	}
	if (d[0] != 0 || d[1] != 0) {
		ned()->adjPosition(d[0], d[1]);
		_scrollX += d[0];
		_scrollY += d[1];
		if (_main && _main->object)
			_main->object->adjView(-d[0], -d[1], 0, 0);
		int vx = _midRemX - d[0], vy = _midRemY - d[1];
		_midRemX = vx % 6;
		_midRemY = vy % 6;
		if (_mid && _mid->object)
			_mid->object->adjView(vx / 6, vy / 6, 0, 0);
		vx = _farRemX - d[0];
		vy = _farRemY - d[1];
		_farRemX = vx % 3;
		_farRemY = vy % 3;
		if (_far && _far->object)
			_far->object->adjView(vx / 3, vy / 3, 0, 0);
	}
	order(50, _nedPos.y <= 0xbe ? 100 : 2);

	// Two monsters at most are up; each toilet waits 0-4 s, a monster 2-4 s after going down
	for (int i = 0; i < kLanes; i++) {
		nudge(_laneSlots[i], d[0], d[1]);
		if (_lane[i] == -1) {
			if (_laneTime[i] == 0) {
				_laneTime[i] = now + ((int)_game->crtRandom() % 5) * 1000;
			} else if (_laneTime[i] <= now && _active < 2) {
				_lane[i] = _laneSlots[i];
				start(_lane[i], -1, _lane[i], now, 0);
				playSound(_lane[i]);
				_active++;
			}
		} else if (!step(_lane[i], _lane[i], now)) {
			_lane[i] = start(-1, _lane[i], -1, now, 0);
			_active--;
			_laneTime[i] = now + ((int)_game->crtRandom() % 3 + 2) * 1000;
		}
	}
	pipesTick(now, d[0], d[1]);
	nudge(0x7c, d[0], d[1]);
	if (_chainPulls != 0 && !step(0x7c, 0x7c, now)) {
		start(0x7c, 0x7c, 0x7c, now, 0);
		_chainPulls--;
	}
	nudge(0x142, d[0], d[1]);
	nudge(0x147, d[0], d[1]);
	nudge(0x148, d[0], d[1]);
	nudge(0x149, d[0], d[1]);

	int i = nedInRect(_rects, 8);
	switch (i) {
	case 0:
		// The top of the ladder: the floors go
		if (_exit == 0 && ned()->getCurSlot() == 0x10) {
			order(329, 2);
			removeMap(map(kMap1));
			removeMap(map(kMap2));
			_exit = -1;
		}
		break;
	case 1:
		if (_exit == -1 && ned()->getCurSlot() == 0x14)
			_exit = 1;
		break;
	case 2:
		if (_chainUnpulled && !_jumped) {
			_nedPos.x = 0x1e0;
			ned()->playAction(0x40, _nedPos, _scrollX, _scrollY);
			_jumped = 1;
		}
		break;
	case 3:
	case 4:
	case 5:
	case 6:
	case 7:
		// Ducking into a toilet while its monster is up: back to the start
		if (ned()->getState() == 10 && monsterGrabbing(i - 3)) {
			ned()->show(false);
			_nedPos = Common::Point(0x6c4, 0);
			ned()->setFacing(0);
			ned()->startFall();
		}
		break;
	default:
		break;
	}
	_game->display().setCollisionOverlay(overlayMaps(), Common::Point(_scrollX, _scrollY));
	_game->display().draw();
	_lastDraw = now;
	debugC(3, kDebugGraphics, "Scene 5:0: input %x Ned %d,%d state %d slot %d scroll %d,%d exit %d",
		input, _nedPos.x, _nedPos.y, ned()->getState(), ned()->getCurSlot(), _scrollX, _scrollY, _exit);
	if (_exit == 1 || _exit == 2) {
		stopAllSounds();
		switchTo(5, 1);
	}
	return 0;
}

// 0x479960 (+0x00)
int Scene5Toilets::hookUse(void *user, int x, int y, int16 *state) {
	Scene5Toilets *s = (Scene5Toilets *)user;
	int16 dir = 0x2f, found = -1;
	int32 rect[4] = { 0, 0, 0, 0 };
	s->findTarget(dir, found, rect, s->_scrollX, s->_scrollY);
	s->ned()->setUseSlot(dir);
	return 1;
}

// 0x479950 (+0x04)
int Scene5Toilets::hookRegion(void *user, int32 id, uint32 flags, int x, int y, int16 *state) {
	return 0;
}

// 0x479a20 (+0x08)
void Scene5Toilets::hookFindTarget(void *user, int x, int y, int16 *a, int16 *b, int32 rect[4]) {
	Scene5Toilets *s = (Scene5Toilets *)user;
	*a = 6;
	int16 dir = 6;
	s->findTarget(dir, *b, rect, s->_scrollX, s->_scrollY);
}

// 0x4799b0 (+0x0c): zapping the chain pulls it three times and plays vision 4
int Scene5Toilets::hookHitTarget(void *user, int x, int y, int16 *a, int16 *b, const int32 rect[4]) {
	Scene5Toilets *s = (Scene5Toilets *)user;
	if (*b == 0x7c && s->_chainPulls == 0) {
		s->_chainPulls = 3;
		s->_chainUnpulled = 0;
		s->hint(4, s->_nedPos.x, s->_nedPos.y + 0x96, xform);
	}
	return 1;
}

// ---- Sub-scene 1: the washroom ----

Scene5Washroom::Scene5Washroom(Game *game) : Scene5Base(game, 1) {
	_paletteVA = kPaletteVA;
	_scrollX = _scrollY = 0;
	memset(_rects, 0, sizeof(_rects));
	_hatchState = 0;
	_room = nullptr;
	_hintArmed = 0;

	_hooks.use = hookUse;
	_hooks.region = hookRegion;
	_hooks.findTarget = hookFindTarget;
	_hooks.hitTarget = hookHitTarget;
}

// 0x45d4d0
void Scene5Washroom::shutdown() {
	_hintArmed = 0;
}

// 0x45d1a0
int Scene5Washroom::load(int fromScene, int fromSub) {
	nedLoad(fromScene);
	return submit(kList, kCount, 4);
}

// 0x45d1c0
int Scene5Washroom::enter(int fromScene, int fromSub) {
	_game->media().playMusic(kMusic);
	if (!isLoaded(kLast))
		return 5;
	attachList(kList, kCount);
	if (_nedListLoaded)
		ned()->listShow(kNedList);
	ned()->copyPalette(_palette);
	_game->display().applyPalette(_palette, kPalAll);
	_world = CollisionWorld();
	addMap(map(kMap1));
	addMap(map(kMap2));
	_room = showBitmap(kBitmap, 0x65);
	_nedPos = Common::Point(0x140, 0);
	_game->res().getSlot(1, 4).offsets[0] = 6;
	ned()->setFacing(0);
	ned()->setWrap1(true, _room ? _room->bitmap.surface.w : 0x280, xform, this);
	ned()->show(true);
	_scrollX = _scrollY = 0;
	if (!loadSst(2))
		return 5;
	slot(kHatch).step = 0;
	applySst(_scrollX, _scrollY);
	_targets.clear();
	targetAdd(kHatch, false);
	ned()->show(false);
	ned()->startFall();
	static const int16 kPieces[][2] = { { 342, 4 }, { 343, 4 }, { 344, 4 }, { kHatch, 100 } };
	for (const auto &p : kPieces) {
		visible(p[0], true);
		order(p[0], p[1]);
	}
	_hatchState = 0;
	regionRects(map(kMap1), _rects, 100, 2);
	_lastDraw = 0;
	return 0;
}

// 0x45d4f0
int Scene5Washroom::leave(int toScene, int toSub) {
	stopAllSounds();
	// The overlay would draw the maps freed here during the exit clip
	_game->display().setCollisionOverlay(Common::Array<CollisionMap *>(), Common::Point());
	ned()->setWrap1(false, 0, nullptr, nullptr);
	_sst.clear();
	release(kList, kCount);
	_room = nullptr;
	if (toScene == 2)
		_game->media().loadClip(0x1e);
	else if (toScene == 5)
		_game->media().loadClip(0x2c);
	_world = CollisionWorld();
	_targets.clear();
	if (toScene != 5) {
		ned()->listFree(kNedList);
		_game->closeNaf(kNaf);
	}
	return 0;
}

// 0x45d580: on to scene 2 (8730) or back up to scene 5 (8744)
int Scene5Washroom::unload(int toScene, int toSub) {
	if (toScene == 2)
		_game->media().playClipWithMusic(1, 0x1e);
	else if (toScene == 5)
		_game->media().playClipWithMusic(1, 0x2c);
	return 0;
}

// 0x45da10: does nothing (the room doesn't scroll)
void Scene5Washroom::xform(void *user, int &x, int &y) {
}

// The hatch opening (inlined in 0x45d6d0; 0x45d5d0 is an uncalled copy): once zapped its
// map 2 goes and it plays to its last step
void Scene5Washroom::hatchTick(uint32 now) {
	if (_hatchState == 1) {
		removeMap(map(kMap2));
		targetRemove(kHatch);
		_hatchState++;
	}
	if (_hatchState != 2)
		return;
	Slot &s = slot(kHatch);
	if (s.deadline < now) {
		s.step++;
		if (s.step < s.sequenceLength) {
			int d = place(kHatch, kHatch);
			s.deadline = now + (int16)d * kMsPerStep;
		}
	} else {
		place(kHatch, kHatch);
	}
	if (s.step == s.sequenceLength - 1)
		_hatchState++;
}

// 0x45d6d0 (no throttle of its own; run at 41 ms here)
int Scene5Washroom::tick(uint32 input, uint32 now) {
	if (now <= _lastDraw + kMsPerStep) {
		g_system->delayMillis(MIN<uint32>(_lastDraw + kMsPerStep + 1 - now, 10));
		return 0;
	}
	nedTick(input, now, _scrollX, _scrollY);
	hatchTick(now);
	_game->display().setCollisionOverlay(overlayMaps(), Common::Point(0, 0));
	_game->display().draw();
	_lastDraw = now;
	debugC(3, kDebugGraphics, "Scene 5:1: input %x Ned %d,%d state %d slot %d hatch %d",
		input, _nedPos.x, _nedPos.y, ned()->getState(), ned()->getCurSlot(), _hatchState);
	int i = nedInRect(_rects, 2);
	if (i == 0)
		switchTo(5, -1);
	else if (i == 1)
		switchTo(2, -1);
	return 0;
}

// 0x45d970 (+0x00)
int Scene5Washroom::hookUse(void *user, int x, int y, int16 *state) {
	Scene5Washroom *s = (Scene5Washroom *)user;
	int16 dir = 0x2f, found = -1;
	int32 rect[4] = { 0, 0, 0, 0 };
	s->findTarget(dir, found, rect, 0, 0);
	s->ned()->setUseSlot(dir);
	return 1;
}

// 0x45d920 (+0x04): region 9999 plays vision 5 once the toilet hall's ladder was taken
int Scene5Washroom::hookRegion(void *user, int32 id, uint32 flags, int x, int y, int16 *state) {
	Scene5Washroom *s = (Scene5Washroom *)user;
	if (id == 9999 && s->_hintArmed)
		s->hint(5, s->_nedPos.x, s->_nedPos.y, xform);
	return 0;
}

// 0x45d9e0 (+0x08)
void Scene5Washroom::hookFindTarget(void *user, int x, int y, int16 *a, int16 *b, int32 rect[4]) {
	Scene5Washroom *s = (Scene5Washroom *)user;
	*a = 6;
	int16 dir = 6;
	s->findTarget(dir, *b, rect, 0, 0);
}

// 0x45d9c0 (+0x0c): zapping the hatch opens it
int Scene5Washroom::hookHitTarget(void *user, int x, int y, int16 *a, int16 *b, const int32 rect[4]) {
	Scene5Washroom *s = (Scene5Washroom *)user;
	if (*b == kHatch && s->_hatchState == 0)
		s->_hatchState = 1;
	return 1;
}

} // End of namespace NightmareNed
