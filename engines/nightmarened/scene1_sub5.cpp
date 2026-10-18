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

#include "common/config-manager.h"
#include "common/debug.h"

#include "nightmarened/detection.h"
#include "nightmarened/display.h"
#include "nightmarened/media.h"
#include "nightmarened/nightmarened.h"
#include "nightmarened/rdx.h"
#include "nightmarened/scene1.h"
#include "nightmarened/scene1_enemies2.h"
#include "nightmarened/scene1_graves.h"

namespace NightmareNed {

Scene1Sub5::Scene1Sub5(Game *game, Scene1 *top) : GraveRoom(game, 5, top), _entryX(0), _risen(0),
		_soundsDone(0), _lampOn(0), _skeletonHit(0), _toAttic(0), _rugFall(0), _roomLoaded(0),
		_listLoaded(0), _stoveOpen(0), _smorph(nullptr), _smorphMode(0), _visionSlot(0), _unused08(0),
		_unused0c(0), _unused34(0), _visionEnd(0), _unused58(0), _riseTime(0), _lampSlot(0),
		_riseSlowTime(0), _riseState(0), _sinkTime(0), _hold(0), _frozen(false) {
	// Initialised .data (all 0 in the exe)
	const ExeImage &exe = _game->exe();
	_risen = exe.readSint32(0x4ba578);
	_soundsDone = exe.readSint32(0x4ba57c);
	_lampOn = exe.readSint32(0x4ba580);
	_skeletonHit = exe.readSint32(0x4ba584);
	_toAttic = exe.readSint32(0x4ba588);
	_rugFall = exe.readSint32(0x4ba58c);
	_roomLoaded = exe.readSint32(0x4ba598);
	_listLoaded = exe.readSint32(0x4ba59c);
	memset(_rows, 0, sizeof(_rows));
	_hooks.use = hookUse;
	_hooks.region = hookRegion;
	_hooks.findTarget = hookFindTarget;
	_hooks.hitTarget = hookHitTarget;
}

Scene1Sub5::~Scene1Sub5() {
}

void Scene1Sub5::listCallback(uint32 cb) {
	if (cb == 0x401000)
		_roomLoaded = 1;
	else if (cb == 0x401010)
		_listLoaded = 1;
}

// m2 0x4012b0
int Scene1Sub5::load(int fromScene, int fromSub) {
	ned()->listLoad(6);
	return (int16)submit(kVideoList, 1, 99);
}

// m3 0x401f30
int Scene1Sub5::enter(int fromScene, int fromSub) {
	Common::Point &nedPos = top()->_nedPos;
	_entryX = nedPos.x;
	_lampOn = _skeletonHit = _rugFall = _soundsDone = _risen = 0;
	_unused08 = 0;
	_visionSlot = 0;
	_riseTime = _riseSlowTime = _sinkTime = _visionEnd = 0;
	_frozen = false;
	_hold = ConfMan.hasKey("nightmarened_s1_smorph") ? ConfMan.getInt("nightmarened_s1_smorph") : 0;
	ned()->setWrap1(false, 0x280, xform, this);
	loadPlacements(5, _sst5);
	loadPlacements(6, _sst6);
	top()->diggerFree();
	ned()->show(false);
	_savedPos = nedPos;
	_savedFacing = ned()->getFacing();
	// Typing TEAM in the graveyard (DAT_005245d0..d3) puts the team photo in the painting
	bool team = top()->_typed[0] == 'T' && top()->_typed[1] == 'E' && top()->_typed[2] == 'A' &&
		top()->_typed[3] == 'M';
	if (ConfMan.hasKey("nightmarened_s1_team") && ConfMan.getBool("nightmarened_s1_team"))
		team = true;
	_listLoaded = 0;
	int result = (int16)submit(kList, kListCount, 5);
	if (result)
		return result;
	dropVideo();
	// do {} while (!DAT_004ba59c): the loader is synchronous
	ned()->listShow(6);
	attachList(kList, kListCount);
	// 0x450210: Ned's colours into the room's palette
	if (_palettes.contains(kPalette))
		ned()->copyPalette(_palettes[kPalette].data());
	applyPaletteVA(kPalette, kPalStatic);
	shaftCreate();
	if (team)
		result = (int16)submitWithId(kRoomList, kRoomCount, 10, kTeamEntry, 6);
	else
		result = (int16)submit(kRoomList, kRoomCount, 10);
	if (result)
		return result;
	fallShaft();
	// do {} while (!DAT_004ba598)
	attachList(kRoomList, kRoomCount);
	_unused0c = 0;
	_unused34 = 0;
	_lampSlot = 0;
	RdxObject *room = bkg(kBmpRoom);
	if (room)
		room->setDrawOrder(0x50);
	_scrollX = 0;
	_lampSlot = kLamp;
	_scrollY = 0x1e0 - bmpHeight(kBmpRoom);
	if (room) {
		room->setPosition(0, _scrollY);
		room->setVisibility(true);
	}
	nedPos = Common::Point(0x1e0, 0x1df);
	int y;
	if (floorAt(kMapRoom, nedPos.x, bmpHeight(kBmpRoom), y))
		nedPos.y = y;
	int lampY = slot(kLamp).y + 0x1e0, lampX = slot(kLamp).x;
	int rugY = slot(kRug).y + 0x1e0, rugX = slot(kRug).x;
	setOrder(kArea, kLamp, 0x4b);
	setOrder(kArea, kRug, 0x4b);
	switchSlot(kRug, -1);
	visible(kLamp, true);
	if (_grid)
		_grid->setVisibility(true);

	// The room scrolls up into view
	int16 target = (int16)(0x1e0 - bmpHeight(kBmpRoom));
	_scrollY = 0x1e0;
	int gridY = 0x1e0 - kShaftH;
	applyPaletteVA(kPalette, kPalGame);
	Common::Point pos;
	while (_scrollY > target && !_game->getEngine()->shouldQuit()) {
		uint32 now = _game->now();
		slotAt(kNedArea, kNedBase).y--;
		Slot &fall = slotAt(kNedArea, kNedFall);
		if (fall.deadline < now) {
			fall.step++;
			if (fall.step >= fall.sequenceLength)
				fall.step = 0;
			int dur = nedPlace(kNedFall, false, pos);
			fall.deadline = now + dur * kMsPerStep;
		}
		nedPlace(kNedFall, false, pos);
		_scrollY -= 8;
		lampY -= 8;
		setPos(kLamp, lampX, lampY);
		gridY -= 8;
		rugY -= 8;
		setPos(kRug, rugX, rugY);
		if (room)
			room->setPosition(_scrollX, _scrollY);
		if (_grid)
			_grid->setPosition(0, gridY);
		draw();
		frameWait(0x2a, now);
	}
	_scrollY = target;
	if (room)
		room->setPosition(_scrollX, target);
	// Ned drops to the floor
	pos = Common::Point(slotAt(kNedArea, kNedBase).x, slotAt(kNedArea, kNedBase).y);
	int guard = 0;
	while (pos.y + slotAt(kNedArea, kNedFall).offsets[1] < nedPos.y + target - 0x48 &&
			!_game->getEngine()->shouldQuit() && guard++ < 1000) {
		uint32 now = _game->now();
		pos.y -= 8;
		slotAt(kNedArea, kNedBase).y = pos.y;
		Slot &fall = slotAt(kNedArea, kNedFall);
		if (fall.deadline < now) {
			fall.step++;
			if (fall.step >= fall.sequenceLength)
				fall.step = 0;
			int dur = nedPlace(kNedFall, false, pos);
			fall.deadline = now + dur * kMsPerStep;
		}
		nedPlace(kNedFall, false, pos);
		draw();
		frameWait(0x23, now);
	}

	setOrder(kArea, kSkeleton, ned()->getZ() + 1);
	setOrder(kArea, kSkeleton, ned()->getZ() + 1);
	setOrder(kArea, kStove, ned()->getZ() + 0xb);
	// 0x487250 + 0x487400(0) + two maps with their own bounds
	_world = CollisionWorld();
	_world.setUser(nullptr);
	addMap(kMapRoom);
	addMap(kMapRoom2);
	slotAt(kNedArea, kNedSlot4).offsets[0] = 6;
	_targets.clear();
	draw();
	targetAdd(kArea, kLamp, true);
	visibleAt(kNedArea, kNedFall, false);
	ned()->setFacing(0);
	ned()->playAction(0x34, nedPos, 0, 0);
	regionEnable(map(kMapRoom), 0x6f, false);
	slotAt(kNedArea, kNedBase).x = 0;
	slotAt(kNedArea, kNedBase).y = 0;
	ned()->setFinishCallback(Scene1::actionDone, top());

	// Test hooks: nightmarened_s1_grave5=lamp|rug|stove|attic starts that event;
	// nightmarened_s1_smorph=<n> lights the lamp and stops the zoom at step n (1..30 in, 31..60 out)
	Common::String what = ConfMan.hasKey("nightmarened_s1_grave5") ? ConfMan.get("nightmarened_s1_grave5") : "";
	if (_hold > 0)
		what = "lamp";
	int16 a = 2, b = 0, state = 0;
	int32 rect[4] = { 0, 0, 0, 0 };
	if (what == "lamp") {
		b = kLamp;
		hookHitTarget(this, 0, 0, &a, &b, rect);
	} else if (what == "stove") {
		b = kStove;
		hookHitTarget(this, 0, 0, &a, &b, rect);
	} else if (what == "rug") {
		hookRegion(this, 3, 0, 0, 0, &state);
	} else if (what == "attic") {
		_toAttic = 1;
	}
	if (!what.empty())
		debugC(1, kDebugLoading, "Graveyard grave 5: test hook %s (zoom hold %d)", what.c_str(), _hold);
	return 0;
}

// m4 0x4012e0
int Scene1Sub5::leave(int toScene, int toSub) {
	stopAllSounds();
	// 0x455ca0: the loader queues are empty here
	_riseSlowTime = 0;
	_riseTime = 0;
	_sinkTime = 0;
	release(kRoomList, kRoomCount);
	_smorph = nullptr; // went with the painting
	_sst5.clear();
	_sst6.clear();
	ned()->setWrap1(false, 0, nullptr, nullptr);
	ned()->show(false);
	_targets.clear();
	ned()->listFree(6);
	if (toScene != 1 && toScene == 4) {
		ned()->show(false);
		ned()->resetIdle();
		shaftFree();
		release(kList, kListCount);
		top()->leave(4, toSub);
		top()->unload(4, toSub);
		// 0x479ff0 + 0x47a3e0("8706tv01.smk", z 1, clip 6)
		_game->media().loadClip(6);
		_game->media().playClip(6, 1);
	}
	return 0;
}

// m5 0x402690
int Scene1Sub5::unload(int toScene, int toSub) {
	if (toScene == 4)
		return 0;
	if (toScene != 1) {
		ned()->show(false);
		ned()->resetIdle();
		shaftFree();
		release(kList, kListCount);
		top()->leave(toScene, toSub);
		top()->unload(toScene, toSub);
		return 0;
	}
	climbOut(0);
	top()->nedWrap();
	Common::Point &nedPos = top()->_nedPos;
	nedPos = _savedPos;
	ned()->setFacing(_savedFacing);
	visibleAt(kNedArea, kNedFall, false);
	visibleAt(kNedArea, kNedClimb, false);
	visibleAt(kNedArea, kNedSlot16, false);
	visible(wallSlot(0), false);
	visible(wallSlot(3), false);
	visible(wallSlot(6), false);
	nedPos = Common::Point(_entryX, 300);
	ned()->playAction(0x22, nedPos, 0, 0);
	slotAt(kNedArea, kNedSlot418).offsets[3] = 0;
	_entryX += ned()->getFacing() ? 0x78 : -0x78;
	int y;
	if (floorAt(Scene1::kMapFloor, (int16)_entryX, 0x1e0, y))
		nedPos.y = y;
	shaftFree();
	release(kList, kListCount);
	slotAt(kNedArea, kNedSlot418).offsets[3] = 0;
	slotAt(kNedArea, kNedSlot418).offsets[2] = 0x78;
	top()->exitsEnable();
	top()->enemies2()->reaperReset();
	regionEnable(map(Scene1::kMapExits), 0x898, true);
	return 0;
}

// The DirectSound end callbacks, polled once a tick
void Scene1Sub5::soundEnd(int cb) {
	if (cb == kCbSoundsStart)
		playSoundCallback(kVisionFirst, kCbSoundsDone); // 0x401640
	else if (cb == kCbSoundsDone)
		_soundsDone = 1;                                 // 0x401660
}

// m7 0x401670
int Scene1Sub5::tick(uint32 input, uint32 now) {
	if (_frozen) {
		draw();
		return 0;
	}
	int cb = pollSoundEnd();
	if (cb)
		soundEnd(cb);
	bool noMirrorWall = slot(kWall).flip == nullptr;
	if (!_roomLoaded)
		return 0;
	if (_toAttic) {
		_game->_optionsAllowed = false;
		switchTo(4, -1);
		_toAttic = 0;
		return 0;
	}
	lampTick(now);
	if (_frozen)
		return 0;

	bool noMirror = slot(kSkeleton).flip == nullptr;
	if (_skeletonHit && slot(kSkeleton).deadline < now) {
		Slot &s = slot(kSkeleton);
		s.step++;
		if (s.step >= s.sequenceLength) {
			s.step = 0;
			_skeletonHit = 0;
			_sinkTime = now + 0x32;
			switchSlot(-1, kWall);
		}
		int dur = place(kSkeleton, kSkeleton, noMirror, xform, this);
		slot(kSkeleton).deadline = now + (int16)dur * kMsPerStep;
	}
	noMirror = slot(kStove).flip == nullptr;
	if (_stoveOpen && slot(kStove).deadline < now) {
		Slot &s = slot(kStove);
		s.step++;
		if (s.step >= s.sequenceLength) {
			s.step = s.sequenceLength - 1;
			_stoveOpen = 0;
		}
		int dur = place(kStove, kStove, noMirror, xform, this);
		slot(kStove).deadline = now + (int16)dur * kMsPerStep;
	}

	RdxObject *wall = bkg(kBmpWall), *wall2 = bkg(kBmpWall2);
	// The wall rises
	if (_riseTime > 0) {
		wallAnim(now);
		if (now > _riseTime) {
			_wall.y -= 8;
			if (wall)
				wall->setPosition(_wall.x, _wall.y);
			slot(kWall).x = (0x280 - slot(kWall).w) / 2;
			slot(kWall).y = _wall.y + 0xa8;
			place(kWall, kWall, noMirrorWall, xform, this);
			if (slot(kSkeleton).y > _skeleton0.y) {
				slot(kSkeleton).y -= 8;
				place(kSkeleton, kSkeleton, noMirrorWall, xform, this);
			}
			int top2 = 0x1e0 - bmpHeight(kBmpWall2);
			if (_wall2.y > top2)
				_wall2.y -= 8;
			else
				_wall2.y = top2;
			if (wall2)
				wall2->setPosition(_wall2.x, _wall2.y);
			_riseTime = _game->now() + 0x32;
			if (_wall.y < 100) {
				_riseTime = 0;
				_soundsDone = 0;
				_riseSlowTime = _game->now() + 200;
			}
		}
	}
	// The sounds are over: Ned can move, in costume 3
	if (_soundsDone) {
		_soundsDone = 0;
		setOrder(kNedArea, kNedBase, ned()->getZ());
		ned()->enable(true);
		ned()->setCostume(3);
		ned()->playAction(0x29, top()->_nedPos, _scrollX, _scrollY);
		top()->_inputFree = 1;
		_risen = 1;
	}
	// The wall creeps up a pixel every 200 ms
	if (_riseSlowTime > 0) {
		wallAnim(now);
		if (now > _riseSlowTime) {
			_wall.y--;
			if (wall)
				wall->setPosition(_wall.x, _wall.y);
			slot(kWall).x = (0x280 - slot(kWall).w) / 2;
			slot(kWall).y = _wall.y + 0xa8;
			_riseSlowTime = _game->now() + 200;
			place(kWall, kWall, noMirrorWall, xform, this);
		}
		if (_wall.y < -0x32) {
			_sinkTime = _game->now() + 0x32;
			_riseSlowTime = 0;
			targetAdd(kArea, kLamp, true);
			switchSlot(-1, kWall);
		}
	}
	// The wall goes back down
	if (_sinkTime > 0 && now > _sinkTime) {
		noMirror = slot(kSkeleton).flip == nullptr;
		_wall.y += 8;
		_wall2.y += 8;
		slot(kSkeleton).y += 8;
		place(kSkeleton, kSkeleton, noMirror, xform, this);
		if (wall)
			wall->setPosition(_wall.x, _wall.y);
		if (wall2)
			wall2->setPosition(_wall2.x, _wall2.y);
		_sinkTime = _game->now() + 0x32;
		if (_wall.y > 0xf0 && _risen) {
			_risen = 0;
			ned()->playAction(0x18, top()->_nedPos, _scrollX, _scrollY);
			ned()->setCostume(1);
		}
		if (_wall.y > 0x1e0) {
			switchSlot(-1, kSkeleton);
			switchSlot(-1, kWallTop);
			_riseState = 2;
			slot(kSkeleton).x = _skeleton0.x;
			slot(kSkeleton).y = _skeleton0.y;
			_sinkTime = 0;
			_riseSlowTime = 0;
			_riseTime = 0;
		}
	}
	// The vision in the painting
	if (_visionEnd > 0) {
		if (now > _visionEnd) {
			zoomOut();
			if (_frozen)
				return 0;
			stopSound(kHumSound);
			playSoundCallback(kWall, kCbSoundsStart);
			_visionEnd = 0;
		} else {
			visionTick(now);
		}
	}
	if (_rugFall)
		rugTick(now);
	if (!top()->_inputFree)
		input &= kInputButton2;
	nedTick(input, now);
	draw();
	return 0;
}

// 0x401050: up out of the rug, the room scrolls down and away
void Scene1Sub5::climbUp() {
	Common::Point lamp = spritePos(kArea, kLamp), stove = spritePos(kArea, kStove), rug = spritePos(kArea, kRug);
	if (_grid)
		_grid->setVisibility(true);
	RdxObject *room = bkg(kBmpRoom);
	if (room)
		room->setPosition(_scrollX, _scrollY);
	int gridY = -kShaftH;
	while (_scrollY < bmpHeight(kBmpRoom) && !_game->getEngine()->shouldQuit()) {
		uint32 now = _game->now();
		_scrollY += 0x10;
		lamp.y += 0x10;
		setPos(kLamp, lamp.x, lamp.y);
		stove.y += 0x10;
		setPos(kStove, stove.x, stove.y);
		rug.y += 0x10;
		setPos(kRug, rug.x, rug.y);
		if (room)
			room->setPosition(_scrollX, _scrollY);
		gridY += 0x10;
		if (_grid)
			_grid->setPosition(0, gridY);
		_climb = spritePos(kNedArea, kNedClimb);
		climbStep(now, 6);
		top()->drawBackdrop();
		frameWait(0x23, now);
	}
	_roomLoaded = 0;
	ned()->enable(true);
	switchTo(1, -1);
}

// 0x4014e0: the wall (273, scrolled) and its top (272, fixed) loop
void Scene1Sub5::wallAnim(uint32 now) {
	bool noMirror = slot(kWall).flip == nullptr;
	Slot &w = slot(kWall);
	if (w.deadline < now) {
		w.step++;
		if (w.step >= w.sequenceLength)
			w.step = 0;
		int dur = place(kWall, kWall, noMirror, xform, this);
		slot(kWall).deadline = now + (int16)dur * kMsPerStep;
	} else {
		place(kWall, kWall, noMirror, xform, this);
	}
	Slot &t = slot(kWallTop);
	if (t.deadline < now) {
		t.step++;
		if (t.step >= t.sequenceLength)
			t.step = 0;
		int dur = place(kWallTop, kWallTop, noMirror, nullptr, nullptr);
		slot(kWallTop).deadline = now + (int16)dur * kMsPerStep;
	} else {
		place(kWallTop, kWallTop, noMirror, nullptr, nullptr);
	}
}

// 0x402840: the lamp plays out once zapped, then the painting comes alive
void Scene1Sub5::lampTick(uint32 now) {
	bool noMirror = slot(kLamp).flip == nullptr;
	int16 id = _lampSlot;
	if (id <= 0)
		return;
	Slot &s = slot(id);
	if (!(s.deadline < now)) {
		place(id, kLamp, noMirror, xform, this);
		return;
	}
	if (_lampOn)
		s.step++;
	if (s.step >= s.sequenceLength) {
		s.step = 0;
		_lampOn = 0;
		targetRemove(kArea, kLamp);
		zoomIn();
		_visionEnd = _game->now() + 6000;
		if (_frozen)
			return;
	}
	if (id > 0) {
		int dur = place(id, kLamp, noMirror, xform, this);
		slot(id).deadline = now + (int16)dur * kMsPerStep;
	}
}

// 0x402970: the rug swallows Ned (51), then he climbs out
void Scene1Sub5::rugTick(uint32 now) {
	bool noMirror = slot(kRugFall).flip == nullptr;
	Slot &s = slot(kRugFall);
	if (!(s.deadline < now)) {
		place(kRugFall, kRug, noMirror, xform, this);
		return;
	}
	s.step++;
	Common::Point &nedPos = top()->_nedPos;
	if (s.step == 10) {
		nedPos = Common::Point(0x10e, 0x110);
		ned()->playAction(2, nedPos, _scrollX, _scrollY);
	}
	if (s.step > 10)
		nedPos.y -= 4;
	if (s.step >= s.sequenceLength) {
		s.step = 0;
		switchSlot(kRug, kRugFall);
		climbUp();
		_climb = spritePos(kNedArea, kNedClimb);
		_rugFall = 0;
		return;
	}
	int dur = place(kRugFall, kRug, noMirror, xform, this);
	slot(kRugFall).deadline = now + (int16)dur * kMsPerStep;
}

// 0x402ed0: the nine slots of the vision (302 holds on its last step)
void Scene1Sub5::visionTick(uint32 now) {
	bool noMirror = slot(kVisionFirst).flip == nullptr;
	for (int i = 0; i < kVisionCount; i++) {
		int16 id = (int16)(kVisionFirst + i);
		Slot &s = slot(id);
		if (!(s.deadline < now))
			continue;
		s.step++;
		if (s.step >= s.sequenceLength) {
			if (i == 1)
				slot(kVisionFirst + 1).step = slot(kVisionFirst + 1).sequenceLength - 1;
			else
				s.step = 0;
		}
		int dur = place(id, id, noMirror, nullptr, nullptr);
		slot(id).deadline = now + (int16)dur * kMsPerStep;
	}
}

// One step of 0x403430 / 0x402fa0: FX_SMORPH squeezes the 640 x 480 painting into w x h
void Scene1Sub5::zoomStep(int16 step, int16 w, int16 h) {
	RdxObject *zoom = bkg(kBmpZoom);
	if (!zoom)
		return;
	if (_smorph) {
		zoom->removeEffect(_smorph);
		_smorph = nullptr;
	}
	int rows = h >= 0x1e0 ? 0x1e0 : h;
	double du = 640.0 / w;
	double dv = 480.0 / h;
	double v = 0.0;
	if (rows > 0) {
		int32 duFixed = (int32)(du * 65536.0);
		for (int i = 0; i < rows; i++) {
			_rows[i * 4 + 0] = 0;
			_rows[i * 4 + 1] = (int32)(v * 65536.0);
			_rows[i * 4 + 2] = duFixed;
			_rows[i * 4 + 3] = 0;
			v += dv;
		}
	}
	_smorph = zoom->applyEffect(kFxSmorph, _smorphMode, _rows, kRows);
	int x, y;
	if (step == 0) {
		x = 0xf8;
		y = 0x37;
	} else {
		y = 0x1e - step;
		x = y * 8;
	}
	zoom->setDestRect(x, y, w, h);
	zoom->setPosition(x, y);
}

// 0x403430: the painting zooms up to the whole screen, then the vision starts
void Scene1Sub5::zoomIn() {
	bool noMirror = slot(kLamp).flip == nullptr;
	top()->_inputFree = 0;
	ned()->playAction(0x33, top()->_nedPos, _scrollX, _scrollY);
	_smorphMode = 0;
	RdxObject *zoom = bkg(kBmpZoom);
	if (zoom) {
		zoom->setDrawOrder((uint32)(ned()->getZ() - 1));
		zoom->setVisibility(true);
	}
	int16 w = 0x140, h = 0xf0;
	for (int16 step = 0; step < kSmorphSteps && !_game->getEngine()->shouldQuit(); step++) {
		uint32 start = _game->now();
		zoomStep(step, w, h);
		h += 8;
		w += 10;
		frameWait(0x28, start);
		draw();
		if (_hold > 0 && _hold <= kSmorphSteps && step + 1 == _hold) {
			debugC(1, kDebugLoading, "Graveyard grave 5: zoom in held at step %d", _hold);
			_frozen = true;
			return;
		}
	}
	if (zoom) {
		if (_smorph) {
			zoom->removeEffect(_smorph);
			_smorph = nullptr;
		}
		zoom->setDestRect(0, 0, 0x280, 0x1e0);
		zoom->setPosition(0, 0);
	}
	loopSound(kHumSound);
	_visionSlot = kVisionFirst;
	for (int i = 0; i < kVisionCount; i++) {
		int16 id = (int16)(kVisionFirst + i);
		setOrder(kArea, id, ned()->getZ() - 2);
		switchSlot(id, -1);
		int dur = place(id, id, noMirror, nullptr, nullptr);
		slot(id).deadline = _game->now() + (int16)dur * kMsPerStep;
	}
	draw();
}

// 0x402fa0: the painting zooms back; the wall and the skeleton start to rise
void Scene1Sub5::zoomOut() {
	bool noMirror = slot(kLamp).flip == nullptr;
	for (int i = 0; i < kVisionCount; i++)
		switchSlot(-1, kVisionFirst + i);
	_smorphMode = 0;
	int16 w = 0x280, h = 0x1e0;
	for (int16 step = kSmorphSteps; step > 0 && !_game->getEngine()->shouldQuit(); step--) {
		uint32 start = _game->now();
		zoomStep(step, w, h);
		w -= 10;
		h -= 8;
		draw();
		frameWait(0x28, start);
		if (_hold > kSmorphSteps && kSmorphSteps - step + 1 + kSmorphSteps == _hold) {
			debugC(1, kDebugLoading, "Graveyard grave 5: zoom out held at step %d", _hold);
			_frozen = true;
			return;
		}
	}
	// The original leaves the effect on the hidden painting
	RdxObject *zoom = bkg(kBmpZoom);
	if (zoom)
		zoom->setVisibility(false);
	_wall = Common::Point(0, 0x1e0);
	BitmapRec *wall = bmp(kBmpWall), *wall2 = bmp(kBmpWall2);
	if (wall && wall->object) {
		wall->object->setPosition(0, 0x1e0);
		wall->object->setVisibility(true);
		wall->object->setDrawOrder((uint32)(ned()->getZ() + 10));
		wall->object->setBitmap(&wall->bitmap.surface, 10); // hbmpSetTransparencyColor(10)
	}
	_wall2 = Common::Point(0, 0);
	if (wall2 && wall2->object) {
		wall2->object->setPosition(0, 0);
		wall2->object->setVisibility(true);
		wall2->object->setDrawOrder(1);
		wall2->object->setBitmap(&wall2->bitmap.surface, 10);
	}
	_skeleton0 = Common::Point(slot(kSkeleton).x, slot(kSkeleton).y);
	slot(kSkeleton).y = 0x1e0 - _scrollY;
	setOrder(kArea, kSkeleton, ned()->getZ() + 1);
	switchSlot(kSkeleton, -1);
	place(kSkeleton, kSkeleton, noMirror, xform, this);
	slot(kWallTop).x = (0x280 - slot(kWallTop).w) / 2;
	slot(kWallTop).y = 0;
	setOrder(kArea, kWallTop, 0x4f);
	switchSlot(kWallTop, -1);
	place(kWallTop, kWallTop, noMirror, nullptr, nullptr);
	_riseState = 1;
	_unused58 = 1;
	_riseTime = _game->now() + 0x32;
	_wall = Common::Point(0, 0x1e0);
	slot(kWall).x = 0;
	slot(kWall).y = 0x27e;
	setOrder(kArea, kWall, ned()->getZ() + 1);
	switchSlot(kWall, -1);
	int dur = place(kWall, kWall, noMirror, nullptr, nullptr);
	slot(kWall).deadline = _game->now() + (int16)dur * kMsPerStep;
	setOrder(kNedArea, kNedBase, ned()->getZ() + 0x14);
	targetAdd(kArea, kSkeleton, true);
}

// 0x401d90
int Scene1Sub5::hookUse(void *user, int x, int y, int16 *state) {
	Scene1Sub5 *s = (Scene1Sub5 *)user;
	int16 dir = 0x2f;
	if (s->_risen) {
		dir = 0x101;
	} else {
		int16 found = -1;
		int32 rect[4] = { 0, 0, 0, 0 };
		s->zapTarget(&dir, &found, rect);
	}
	s->ned()->setUseSlot(dir);
	return 1;
}

// 0x401d10
int Scene1Sub5::hookRegion(void *user, int32 id, uint32 flags, int x, int y, int16 *state) {
	Scene1Sub5 *s = (Scene1Sub5 *)user;
	*state = s->_risen ? 4 : 0xe;
	if (id == 3) {
		s->switchSlot(kRugFall, kRug);
		s->playSound(kRugFall);
		s->_rugFall = 1;
		s->ned()->enable(false);
	} else if (id == 0x6f) {
		s->_toAttic = 1;
	}
	return 1;
}

// 0x401ee0
void Scene1Sub5::hookFindTarget(void *user, int x, int y, int16 *a, int16 *b, int32 rect[4]) {
	Scene1Sub5 *s = (Scene1Sub5 *)user;
	*a = 2;
	// The original passes the address of its own argument as the direction (discarded)
	int16 dir = 2;
	s->zapTarget(&dir, b, rect);
}

// 0x401e00
int Scene1Sub5::hookHitTarget(void *user, int x, int y, int16 *a, int16 *b, const int32 rect[4]) {
	Scene1Sub5 *s = (Scene1Sub5 *)user;
	bool noMirror = s->slot(kStove).flip == nullptr;
	if (*b == kLamp) {
		s->_lampOn = 1;
	} else if (*b == kStove) {
		regionEnable(s->map(kMapRoom), 0x6f, true);
		s->_stoveOpen = 1;
	} else if (*b == kSkeleton) {
		s->_skeletonHit = 1;
		s->switchSlot(kStove, -1);
		s->place(kStove, kStove, noMirror, xform, s);
		s->targetAdd(kArea, kStove, true);
	}
	return 1;
}

} // End of namespace NightmareNed
