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
#include "common/textconsole.h"

#include "nightmarened/detection.h"
#include "nightmarened/display.h"
#include "nightmarened/nightmarened.h"
#include "nightmarened/rdx.h"
#include "nightmarened/scene1.h"
#include "nightmarened/scene1_enemies2.h"
#include "nightmarened/scene1_graves.h"

namespace NightmareNed {

Scene1Sub3::Scene1Sub3(Game *game, Scene1 *top) : GraveRoom(game, 3, top), _entryX(0), _exiting(0),
		_unused78(0), _lampLit(0), _listLoaded(0), _pitLoaded(0), _exitSound(0), _readerState(0) {
	_lampState = (int16)_game->exe().readUint16(0x4a3df8);
	_hooks.use = hookUse;
	_hooks.region = hookRegion;
	_hooks.findTarget = hookFindTarget;
	_hooks.hitTarget = hookHitTarget;
}

Scene1Sub3::~Scene1Sub3() {
}

void Scene1Sub3::listCallback(uint32 cb) {
	if (cb == 0x4414f0)
		_listLoaded = 1;
	else if (cb == 0x441500)
		_pitLoaded = 1;
}

// m2 0x444040
int Scene1Sub3::load(int fromScene, int fromSub) {
	ned()->listLoad(9);
	return (int16)submit(kVideoList, 1, 99);
}

// m3 0x444070
int Scene1Sub3::enter(int fromScene, int fromSub) {
	Common::Point &nedPos = top()->_nedPos;
	_entryX = nedPos.x;
	_exitSound = 0;
	ned()->setWrap1(false, 0x280, xform, this);
	loadPlacements(2, _sst);
	_savedPos = nedPos;
	_savedFacing = ned()->getFacing();
	_lampLit = 0;
	top()->diggerFree();
	ned()->show(false);
	nedPos = Common::Point(0x1e0, 0x1df);
	_listLoaded = 0;
	int result = (int16)submit(kList, kListCount, 7);
	if (result)
		return result;
	dropVideo();
	// do {} while (!DAT_004bfe90): the loader is synchronous
	ned()->listShow(9);
	attachList(kList, kListCount);
	applyPaletteVA(kPalette, kPalStatic);
	shaftCreate();
	result = (int16)submit(kPitList, kPitCount, 8);
	if (result)
		return result;
	fallShaft();
	attachList(kPitList, kPitCount);

	static const int16 parlour[11] = {
		kReader, kReaderFlinch, kReaderSwat, kReaderReach, kLamp, kLampIdle, kLampHopA, kLampHopB,
		kStoolClimb, kStool, kLampLit
	};
	setOrder(kArea, kReaderSpark, 0x49);
	for (int i = 0; i < 11; i++) {
		setOrder(kArea, parlour[i], 0x4b);
		setPos(parlour[i], slot(parlour[i]).x, slot(parlour[i]).y);
	}
	RdxObject *pit = bkg(kBmpPit);
	if (pit)
		pit->setDrawOrder(0x50);
	_scrollX = 0;
	_scrollY = 0x1e0;
	if (pit) {
		pit->setPosition(0, 0x1e0);
		pit->setVisibility(true);
	}
	setOrder(kArea, kReaderSwat, ned()->getZ() - 1);
	int y;
	if (floorAt(kMapPit, nedPos.x, 799, y))
		nedPos.y = y;

	_readerPos = Common::Point(slot(kReader).x, slot(kReader).y);
	int readerY = slot(kReader).y + 0x1e0;
	_lampPos = Common::Point(slot(kLamp).x, slot(kLamp).y);
	int lampY = slot(kLamp).y + 0x1e0;
	int stoolY = slot(kStool).y + 0x1e0;
	_stoolPos = Common::Point(slot(kStool).x, slot(kStool).y);
	visible(kReader, true);
	visible(kLamp, true);
	visible(kStool, true);
	if (_grid)
		_grid->setVisibility(true);

	// The parlour scrolls up into view
	_scrollY = 0x1e0;
	int target = (int16)(0x1e0 - bmpHeight(kBmpPit));
	int gridY = 0x1e0 - kShaftH;
	applyPaletteVA(kPalette, kPalGame);
	Common::Point pos = _fallEnd;
	while (_scrollY > target && !_game->getEngine()->shouldQuit()) {
		uint32 now = _game->now();
		Slot &fall = slotAt(kNedArea, kNedFall);
		if (fall.deadline < now) {
			fall.step++;
			slotAt(kNedArea, kNedBase).y--;
			if (fall.step >= fall.sequenceLength)
				fall.step = 0;
			int dur = nedPlace(kNedFall, false, pos);
			fall.deadline = now + dur * kMsPerStep;
		}
		nedPlace(kNedFall, false, pos);
		_scrollY -= 8;
		gridY -= 8;
		readerY -= 8;
		setPos(kReader, _readerPos.x, readerY);
		lampY -= 8;
		setPos(kLamp, _lampPos.x, lampY);
		stoolY -= 8;
		setPos(kStool, _stoolPos.x, stoolY);
		_wallY[2] -= 8;
		setPos(wallSlot(6), _wallX[2], _wallY[2]);
		if (pit)
			pit->setPosition(_scrollX, _scrollY);
		if (_grid)
			_grid->setPosition(0, gridY);
		draw();
		frameWait(0x23, now);
	}
	// Ned drops to the floor
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

	_unused78 = -1;
	_exiting = 0;
	ned()->setFacing(0);
	slotAt(kNedArea, kNedSlot4).offsets[0] = 6;
	// 0x487250 + 0x487400(0) + 0x4872a0
	_world = CollisionWorld();
	_world.setUser(nullptr);
	addMapBounds(kMapPit, 0, 0, 0x27f, 0x31f);
	slotAt(kNedArea, kNedSlot4).offsets[0] = 6;
	_lampState = 0xb;
	_readerState = 0;
	_targets.clear();
	targetAdd(kArea, kReader, true);
	targetAdd(kArea, kLampIdle, true);
	visibleAt(kNedArea, kNedFall, false);
	ned()->playAction(0x34, nedPos, 0, 0);
	slotAt(kNedArea, kNedBase).x = 0;
	slotAt(kNedArea, kNedBase).y = 0;
	draw();

	// Test hook: nightmarened_s1_grave3=lamp|exit|swat|flinch starts that event
	if (ConfMan.hasKey("nightmarened_s1_grave3")) {
		Common::String what = ConfMan.get("nightmarened_s1_grave3");
		int16 a = 2, b = (what == "lamp") ? (int16)kLampIdle : (int16)kReader;
		int32 rect[4] = { 0, 0, 0, 0 };
		int16 state = 0;
		if (what == "lamp" || what == "flinch")
			hookHitTarget(this, 0, 0, &a, &b, rect);
		else if (what == "swat")
			hookRegion(this, 3, 0, 0, 0, &state);
		else if (what == "exit")
			_exiting = 1;
		debugC(1, kDebugLoading, "Graveyard grave 3: test hook %s", what.c_str());
	}
	return 0;
}

// m4 0x4447d0
int Scene1Sub3::leave(int toScene, int toSub) {
	// 0x455ca0: the loader queues are empty here
	ned()->listFree(9);
	return 0;
}

// m5 0x4447f0
int Scene1Sub3::unload(int toScene, int toSub) {
	if (toScene != 1) {
		ned()->show(false);
		ned()->resetIdle();
		_targets.clear();
		_sst.clear();
		shaftFree();
		release(kList, kListCount);
		release(kPitList, kPitCount);
		top()->leave(toScene, toSub);
		top()->unload(toScene, toSub);
		return 0;
	}
	climbOut(0);
	top()->nedWrap();
	_targets.clear();
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
	_sst.clear();
	shaftFree();
	release(kList, kListCount);
	_exiting = 0;
	slotAt(kNedArea, kNedSlot418).offsets[2] = 0x78;
	top()->exitsEnable();
	top()->enemies2()->reaperReset();
	regionEnable(map(Scene1::kMapExits), 0x898, true);
	return 0;
}

// m7 0x444ad0
int Scene1Sub3::tick(uint32 input, uint32 now) {
	if (!_pitLoaded)
		return 0;
	if (_exiting) {
		if (!_exitSound) {
			playSound(kStoolClimb);
			_exitSound = 1;
		}
		exitStep(now);
		draw();
		return 0;
	}
	lampTick(now);
	readerTick(now);
	nedTick(input, now);
	draw();
	return 0;
}

// 0x443090
int16 Scene1Sub3::lampTick(uint32 now) {
	switch (_lampState) {
	case 6:
		visible(kLampIdle, false);
		visible(kLampLit, false);
		visible(kLampHopA, true);
		lampHopAStep(now);
		break;
	case 7:
		visible(kLampIdle, false);
		visible(kLampLit, false);
		visible(kLampHopA, false);
		visible(kLampHopB, true);
		lampHopBStep(now);
		break;
	case 10:
		visible(kLampIdle, false);
		visible(kLampLit, true);
		lampLitPlace();
		break;
	case 11:
		visible(kLamp, false);
		visible(kLampLit, false);
		visible(kLampHopA, false);
		visible(kLampHopB, false);
		visible(kLampIdle, true);
		lampIdleStep(now);
		break;
	default:
		break;
	}
	return _lampState;
}

// 0x443200
int16 Scene1Sub3::readerTick(uint32 now) {
	switch (_readerState) {
	case 0:
		visible(kReaderFlinch, false);
		visible(kReaderSwat, false);
		visible(kReaderReach, false);
		visible(kReader, true);
		break;
	case 1:
		visible(kReader, false);
		visible(kReaderFlinch, true);
		readerFlinchStep(now);
		break;
	case 2:
		visible(kReader, false);
		visible(kReaderReach, false);
		visible(kReaderSwat, true);
		readerSwatStep(now);
		break;
	case 3:
		visible(kReader, false);
		visible(kReaderReach, true);
		readerReachStep(now);
		break;
	default:
		break;
	}
	return _readerState;
}

void Scene1Sub3::anchoredPlace(int id, int ref, int frame, const Common::Point &refPos) {
	int ax = 0, ay = 0, rx = 0, ry = 0, ox = 0, oy = 0;
	anchor(kArea, id, ax, ay);
	anchor(kArea, ref, rx, ry);
	frameOffset(kArea, id, frame, ox, oy);
	setPos(id, ax - ox - rx + refPos.x, ay - oy - ry + refPos.y + _scrollY);
}

bool Scene1Sub3::anchoredStep(int id, int ref, const Common::Point &refPos, uint32 now, int *outFrame) {
	Slot &s = slot(id);
	if (!(s.deadline < now))
		return false;
	int frame = 0, duration = 0;
	seqStep(kArea, id, s.step, frame, duration);
	setImage(kArea, id, frame);
	s.deadline = now + (int16)duration * kMsPerStep;
	anchoredPlace(id, ref, frame, refPos);
	s.step++;
	if (outFrame)
		*outFrame = frame;
	return true;
}

// 0x443320: the lamp's first hop, held on its last step
void Scene1Sub3::lampHopAStep(uint32 now) {
	if (!anchoredStep(kLampHopA, kLamp, _lampPos, now))
		return;
	Slot &s = slot(kLampHopA);
	if (s.step >= s.sequenceLength)
		s.step = s.sequenceLength - 1;
}

// 0x443450: the lit lamp is placed once and the skeleton reaches for it
void Scene1Sub3::lampLitPlace() {
	if (_lampLit)
		return;
	int frame = 0, duration = 0;
	seqStep(kArea, kLampLit, slot(kLampLit).step, frame, duration);
	anchoredPlace(kLampLit, kLampLit, frame, _lampPos);
	slot(kReaderReach).step = 0;
	_readerState = 3;
	_lampLit = 1;
}

// 0x443550: the second hop, then the lamp goes idle (and the way out closes)
void Scene1Sub3::lampHopBStep(uint32 now) {
	if (!anchoredStep(kLampHopB, kLampLit, _lampPos, now))
		return;
	Slot &s = slot(kLampHopB);
	if (s.step >= s.sequenceLength) {
		s.step = 0;
		_lampState = 0xb;
		_lampLit = 0;
	}
}

// 0x4436a0: the idle lamp loops, with its sound each lap
void Scene1Sub3::lampIdleStep(uint32 now) {
	if (!anchoredStep(kLampIdle, kLamp, _lampPos, now))
		return;
	Slot &s = slot(kLampIdle);
	if (s.step >= s.sequenceLength) {
		playSound(kLampIdle);
		s.step = 0;
	}
}

// 0x443950: the skeleton reaches for the lit lamp (a spark, 415, from step 3)
void Scene1Sub3::readerReachStep(uint32 now) {
	int frame = 0;
	if (!anchoredStep(kReaderReach, kReader, _readerPos, now, &frame))
		return;
	Slot &s = slot(kReaderReach);
	if (s.step == 5) {
		_lampState = 6;
		playSound(kLampHopA);
	}
	if (s.step == 3) {
		visible(kReaderSpark, true);
		slot(kReaderSpark).step = 0;
	}
	if (s.step > 2) {
		// The spark takes the skeleton's frame
		setImage(kArea, kReaderSpark, frame);
		anchoredPlace(kReaderSpark, kReader, frame, _readerPos);
		Slot &k = slot(kReaderSpark);
		k.step++;
		if (k.step >= k.sequenceLength) {
			k.step = 0;
			visible(kReaderSpark, false);
		}
	}
	if (s.step == 0x28) {
		_lampState = 7;
		playSound(kLampHopB);
	}
	if (s.step >= s.sequenceLength) {
		s.step = 0;
		_readerState = 0;
	}
	if (s.step == 2) {
		playSound(kReaderReach);
		setOrder(kArea, kStool, 0x48);
	}
	if (s.step == 0x29) {
		setOrder(kArea, kStool, 0x4b);
		playSound(kReader, kReaderReach);
	}
}

// 0x443c30
void Scene1Sub3::readerFlinchStep(uint32 now) {
	if (!anchoredStep(kReaderFlinch, kReader, _readerPos, now))
		return;
	Slot &s = slot(kReaderFlinch);
	if (s.step >= s.sequenceLength) {
		s.step = 0;
		_readerState = 0;
	}
}

// 0x443d70: the swat; Ned lands back by the shaft and gets up
void Scene1Sub3::readerSwatStep(uint32 now) {
	if (!anchoredStep(kReaderSwat, kReader, _readerPos, now))
		return;
	Slot &s = slot(kReaderSwat);
	if (s.step >= s.sequenceLength) {
		visible(kReaderSwat, false);
		s.step = 0;
		_readerState = 0;
		_exiting = 0;
	}
	Common::Point &nedPos = top()->_nedPos;
	if (s.step == 3) {
		playSound(kReaderSwat);
		ned()->enable(false);
		nedPos.x = _readerPos.x + 0x122;
		int y;
		if (floorAt(kMapPit, nedPos.x, 0x31f, y))
			nedPos.y = y;
	}
	if (s.step == 6) {
		ned()->enable(true);
		ned()->playAction(3, nedPos, _scrollX, _scrollY);
	}
}

// 0x4437f0: Ned climbs the stool (42); then the parlour scrolls away
void Scene1Sub3::exitStep(uint32 now) {
	Slot &s = slot(kStoolClimb);
	if (!(s.deadline < now))
		return;
	visible(kStoolClimb, true);
	visible(kStool, false);
	ned()->adjPosition(0, s.step >= 2 ? -8 : 0x10);
	int frame = 0, duration = 0;
	seqStep(kArea, kStoolClimb, s.step, frame, duration);
	int ox = 0, oy = 0;
	frameOffset(kArea, kStoolClimb, frame, ox, oy);
	setImage(kArea, kStoolClimb, frame);
	s.deadline = now + (int16)duration * kMsPerStep;
	setPos(kStoolClimb, _stoolPos.x - ox, _stoolPos.y - oy + _scrollY);
	s.step++;
	if (s.step < s.sequenceLength)
		return;
	visible(kStoolClimb, false);
	visible(kStool, true);
	s.step = 0;
	_climb = spritePos(kNedArea, kNedBase);
	ned()->playAction(2, top()->_nedPos, _scrollX, _scrollY);
	_climb.y -= 0x10;
	exitScroll();
}

// 0x442680
void Scene1Sub3::exitScroll() {
	static const int16 ids[9] = {
		kReader, kReaderReach, kLamp, kReaderSpark, kLampHopA, kStool, kLampLit, kLampHopB, kLampIdle
	};
	Common::Point p[9];
	for (int i = 0; i < 9; i++)
		p[i] = spritePos(kArea, ids[i]);
	if (_grid)
		_grid->setVisibility(true);
	int gridY = -kShaftH;
	RdxObject *pit = bkg(kBmpPit);
	if (pit)
		pit->setPosition(_scrollX, _scrollY);
	while (_scrollY < 0x1e0 && !_game->getEngine()->shouldQuit()) {
		uint32 now = _game->now();
		_scrollY += 0x10;
		gridY += 0x10;
		for (int i = 0; i < 9; i++) {
			p[i].y += 0x10;
			setPos(ids[i], p[i].x, p[i].y);
		}
		if (pit)
			pit->setPosition(_scrollX, _scrollY);
		if (_grid)
			_grid->setPosition(0, gridY);
		climbStep(now, 6);
		draw();
		frameWait(0x1e, now);
	}
	release(kPitList, kPitCount);
	_pitLoaded = 0;
	switchTo(1, -1);
}

// 0x444be0
int Scene1Sub3::hookUse(void *user, int x, int y, int16 *state) {
	Scene1Sub3 *s = (Scene1Sub3 *)user;
	int16 dir = 0x2f, found = -1;
	int32 rect[4] = { 0, 0, 0, 0 };
	s->zapTarget(&dir, &found, rect);
	s->ned()->setUseSlot(dir);
	return 1;
}

// 0x444b90
int Scene1Sub3::hookRegion(void *user, int32 id, uint32 flags, int x, int y, int16 *state) {
	Scene1Sub3 *s = (Scene1Sub3 *)user;
	if (id != 3) {
		*state = 0xe;
		return 1;
	}
	if (s->_lampLit) {
		s->_exiting = 1;
		return 0;
	}
	if (s->_readerState != 2)
		s->_readerState = 2;
	return 0;
}

// 0x444c90
void Scene1Sub3::hookFindTarget(void *user, int x, int y, int16 *a, int16 *b, int32 rect[4]) {
	Scene1Sub3 *s = (Scene1Sub3 *)user;
	*a = 2;
	// The original passes the address of its own argument as the direction (discarded)
	int16 dir = 2;
	s->zapTarget(&dir, b, rect);
}

// 0x444c30
int Scene1Sub3::hookHitTarget(void *user, int x, int y, int16 *a, int16 *b, const int32 rect[4]) {
	Scene1Sub3 *s = (Scene1Sub3 *)user;
	if (*b == kReader) {
		s->_readerState = 1;
		s->playSound(kReaderFlinch);
	} else if (*b == kLampIdle) {
		s->_lampState = 0xa;
		s->stopSound(kLampIdle);
	}
	return 1;
}

} // End of namespace NightmareNed
