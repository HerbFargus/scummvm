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

#include "nightmarened/display.h"
#include "nightmarened/nightmarened.h"
#include "nightmarened/rdx.h"
#include "nightmarened/scene1.h"
#include "nightmarened/scene1_enemies2.h"
#include "nightmarened/scene1_graves.h"

namespace NightmareNed {

Scene1Sub4::Scene1Sub4(Game *game, Scene1 *top) : GraveRoom(game, 4, top), _entryX(0), _sofaLoaded(0),
		_listLoaded(0) {
	_state = (int16)_game->exe().readUint16(0x4a45a0);
}

Scene1Sub4::~Scene1Sub4() {
}

void Scene1Sub4::listCallback(uint32 cb) {
	if (cb == 0x444ce0)
		_sofaLoaded = 1;
	else if (cb == 0x444cf0)
		_listLoaded = 1;
}

// m2 0x444d20
int Scene1Sub4::load(int fromScene, int fromSub) {
	return (int16)submit(kVideoList, 1, 99);
}

// m3 0x444d40
int Scene1Sub4::enter(int fromScene, int fromSub) {
	top()->diggerVisible(false);
	_entryX = top()->_nedPos.x;
	ned()->setWrap1(false, 0x280, xform, this);
	loadPlacements(4, _sst);
	int result = (int16)submit(kList, kListCount, 6);
	if (result)
		return result;
	dropVideo();
	// do {} while (!DAT_004bfea8): the loader is synchronous
	shaftCreate();
	attachList(kList, kListCount);
	applyPaletteVA(kPalette, kPalStatic);
	result = (int16)submit(kSofaList, kSofaCount, 9);
	if (result)
		return result;
	fall();
	attachList(kSofaList, kSofaCount);
	setOrder(kArea, kSofa, 0x4b);
	_scrollX = 0;
	RdxObject *room = bkg(kBmpRoom);
	if (room)
		room->setDrawOrder(0x50);
	_scrollY = 0x1e0;
	if (room)
		room->setPosition(_scrollX, 0x1e0);
	int16 target = (int16)(0x1e0 - bmpHeight(kBmpRoom));
	int gridY = 0x1e0 - kShaftH;
	if (room)
		room->setVisibility(true);
	_sofaPos = Common::Point(slot(kSofa).x, slot(kSofa).y + 0x1e0);
	setPos(kSofa, _sofaPos.x, _sofaPos.y);
	visible(kSofa, true);
	Common::Point pos(slotAt(kNedArea, kNedBase).x, slotAt(kNedArea, kNedBase).y);
	applyPaletteVA(kPalette, kPalGame);
	top()->drawBackdrop();
	while (_scrollY > target && !_game->getEngine()->shouldQuit()) {
		uint32 now = _game->now();
		_scrollY -= 10;
		if (room)
			room->setPosition(_scrollX, _scrollY);
		gridY -= 10;
		if (_grid)
			_grid->setPosition(0, gridY);
		_sofaPos.y -= 10;
		setPos(kSofa, _sofaPos.x, _sofaPos.y);
		Slot &fall = slotAt(kNedArea, kNedFall);
		if (fall.deadline < now) {
			fall.step++;
			if (fall.step >= fall.sequenceLength)
				fall.step = 0;
			int dur = nedPlace(kNedFall, false, pos);
			fall.deadline = now + dur * kMsPerStep;
		}
		top()->drawBackdrop();
		frameWait(0x23, now);
	}
	_scrollY = target;
	_scrollX = 0;
	if (room)
		room->setPosition(0, target);
	_state = 2;
	top()->drawBackdrop();
	visibleAt(kNedArea, kNedFall, false);
	playSound(kSofaSound);
	return 0;
}

// m4 0x445250
int Scene1Sub4::leave(int toScene, int toSub) {
	return 0;
}

// m5 0x445260
int Scene1Sub4::unload(int toScene, int toSub) {
	if (toScene != 1) {
		ned()->show(false);
		ned()->resetIdle();
		_sst.clear();
		shaftFree();
		release(kList, kListCount);
		release(kSofaList, kSofaCount);
		top()->leave(toScene, toSub);
		top()->unload(toScene, toSub);
		return 0;
	}
	climbOut(1);
	top()->nedWrap();
	// DAT_00528084 as sub-scene 3 or 5 last saved it: this one never sets it
	ned()->setFacing(_savedFacing);
	visibleAt(kNedArea, kNedFall, false);
	visibleAt(kNedArea, kNedClimb, false);
	visibleAt(kNedArea, kNedSlot16, false);
	visible(wallSlot(0), false);
	visible(wallSlot(3), false);
	visible(wallSlot(6), false);
	Common::Point &nedPos = top()->_nedPos;
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
	slotAt(kNedArea, kNedSlot418).offsets[2] = 0x78;
	top()->exitsEnable();
	top()->enemies2()->reaperReset();
	regionEnable(map(Scene1::kMapExits), 0x898, true);
	return 0;
}

// m7 0x445410
int Scene1Sub4::tick(uint32 input, uint32 now) {
	sofaTick(now);
	draw();
	return 0;
}

// 0x442290: the short fall (no wall creature bites)
void Scene1Sub4::fall() {
	top()->layersVisible(false);
	RdxObject *topBmp = bkg(kBmpTop);
	int topY = 0x1e0;
	if (topBmp) {
		topBmp->setPosition(0, 0x1e0);
		topBmp->setVisibility(true);
		topBmp->setDrawOrder(0x50);
	}
	int videoY = 0;
	_wallX[1] = 0x17c;
	int gridY = (int16)(bmpHeight(kBmpTop) + 0x1e0);
	_wallX[0] = 100;
	_wallY[0] = gridY + 0xdc;
	_wallX[2] = 100;
	_wallY[1] = gridY + 0x1f4;
	_wallY[2] = gridY + 0x370;
	for (int i = 0; i < 9; i++)
		setOrder(kArea, wallSlot(i), 0x4b);
	if (_grid) {
		_grid->setPosition(0, gridY);
		_grid->setVisibility(true);
	}
	switchSlotAt(kNedArea, kNedFall, -1);
	visible(wallSlot(1), true);
	visible(wallSlot(4), true);
	visible(wallSlot(7), true);
	setPos(wallSlot(1), _wallX[0], _wallY[0]);
	setPos(wallSlot(3), _wallX[1], _wallY[1]);
	setPos(wallSlot(6), _wallX[1], _wallY[1]);
	flipActive(kNedArea, kNedFall, true);
	int nedY = -100;
	Common::Point pos;
	BitmapRec *video = bmp(kBmpVideo);
	while (gridY > 0x1e0 - kShaftH && !_game->getEngine()->shouldQuit()) {
		uint32 now = _game->now();
		if (nedY < 0x2c)
			nedY += 8;
		_wallY[0] -= 10;
		_wallY[1] -= 10;
		_wallY[2] -= 10;
		videoY -= 10;
		gridY -= 10;
		topY -= 10;
		if (topBmp)
			topBmp->setPosition(0, topY);
		if (video && video->object)
			video->object->setPosition(0, videoY);
		if (_grid)
			_grid->setPosition(0, gridY);
		setPos(wallSlot(1), _wallX[0], _wallY[0]);
		setPos(wallSlot(4), _wallX[1], _wallY[1]);
		setPos(wallSlot(7), _wallX[2], _wallY[2]);
		Slot &fall = slotAt(kNedArea, kNedFall);
		if (fall.deadline < now) {
			fall.step++;
			if (fall.step >= fall.sequenceLength)
				fall.step = 0;
			int dur = nedPlace(kNedFall, false, pos);
			fall.deadline = now + dur * kMsPerStep;
		}
		slotAt(kNedArea, kNedBase).x = 0x162;
		slotAt(kNedArea, kNedBase).y = nedY;
		nedPlace(kNedFall, false, pos);
		draw();
		frameWait(0x23, now);
	}
	stopAllSounds();
	release(kVideoList, 1);
	visible(wallSlot(1), false);
	visible(wallSlot(4), false);
	visible(wallSlot(7), false);
}

// 0x445650
void Scene1Sub4::sofaTick(uint32 now) {
	if (!_sofaLoaded)
		return;
	if (_state == 0) {
		Slot &s = slot(kSofa);
		s.step = 0;
		int frame = 0, duration = 0;
		seqStep(kArea, kSofa, s.step, frame, duration);
		int ox = 0, oy = 0;
		frameOffset(kArea, kSofa, frame, ox, oy);
		setImage(kArea, kSofa, frame);
		s.deadline = now + (int16)duration * kMsPerStep;
		setPos(kSofa, _sofaPos.x - ox, _sofaPos.y - oy);
		_state = 2;
	} else if (_state == 2) {
		visible(kSofa, true);
		sofaStep(now);
	}
}

// 0x445430
void Scene1Sub4::sofaStep(uint32 now) {
	Slot &s = slot(kSofa);
	if (!(s.deadline < now))
		return;
	s.step++;
	if (s.step == 0xe)
		playSound(kSofa);
	if (s.step == 0x2d) {
		stopSound(kSofaSound);
		playSound(kSofaSound2);
	}
	bool ended = s.step >= s.sequenceLength;
	if (ended)
		s.step = 0;
	int frame = 0, duration = 0;
	seqStep(kArea, kSofa, s.step, frame, duration);
	int ox = 0, oy = 0;
	frameOffset(kArea, kSofa, frame, ox, oy);
	setImage(kArea, kSofa, frame);
	s.deadline = now + (int16)duration * kMsPerStep;
	setPos(kSofa, _sofaPos.x - ox, _sofaPos.y - oy);
	if (!ended)
		return;
	// The original reads Ned's position into DAT_005231f0 and then overwrites it
	_climb = Common::Point(0x12c, 0x50);
	stopSound(kSofa);
	ned()->playAction(2, top()->_nedPos, _scrollX, _scrollY);
	exitScroll();
}

// 0x445050
void Scene1Sub4::exitScroll() {
	stopSound(kSofa);
	_sofaPos = spritePos(kArea, kSofa);
	if (_grid)
		_grid->setVisibility(true);
	RdxObject *room = bkg(kBmpRoom);
	if (room)
		room->setPosition(_scrollX, _scrollY);
	int gridY = -kShaftH;
	while (_scrollY < bmpHeight(kBmpRoom) && !_game->getEngine()->shouldQuit()) {
		uint32 now = _game->now();
		_scrollY += 0x10;
		_sofaPos.y += 0x10;
		setPos(kSofa, _sofaPos.x, _sofaPos.y);
		if (room)
			room->setPosition(_scrollX, _scrollY);
		gridY += 0x10;
		if (_grid)
			_grid->setPosition(0, gridY);
		climbStep(now, 6);
		top()->drawBackdrop();
		frameWait(0x23, now);
	}
	release(kSofaList, kSofaCount);
	_sofaLoaded = 0;
	switchTo(1, -1);
}

} // End of namespace NightmareNed
