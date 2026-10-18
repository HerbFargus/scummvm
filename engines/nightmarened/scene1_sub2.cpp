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
#include "common/system.h"

#include "nightmarened/detection.h"
#include "nightmarened/display.h"
#include "nightmarened/menus.h"
#include "nightmarened/nightmarened.h"
#include "nightmarened/rdx.h"
#include "nightmarened/scene1.h"
#include "nightmarened/scene1_sub2.h"

#include <math.h>

namespace NightmareNed {

Scene1Sub2::Scene1Sub2(Game *game, Scene1 *top) : GraveyardRoom(game, 2, top), _blockVision(0) {
	// The hook table is .bss: empty until 0x41e510 fills it
	_hooks.user = this;
	_hooks.use = nullptr;
	_hooks.region = nullptr;
	_hooks.findTarget = nullptr;
	_hooks.hitTarget = nullptr;
	_last = 0;
	_facing = 0;
	_topPos = Common::Point(0, 0);
	_boxSavedX = _boxSavedY = 0;
	_climbed = _failed = 0;
	_maleCarried = _femaleCarried = 0;
	_maleOpen = _femaleOpen = 0;
	_ladderOpen = 0;
	_femaleHp = _maleHp = 0;
	_scroll = Common::Point(0, 0);
	_nedPos = Common::Point(0, 0);
	_boxX = _boxY = _boxStop = 0;
	_scrollTime = 0;
	_box = _maleCoffin = _femaleCoffin = _lid = 0;
	_male = _female = _maleArm1 = _maleArm2 = _femaleArm1 = _femaleArm2 = 0;
	_carryFemale = _carryMale = 0;
	for (int i = 0; i < 9; i++)
		_arcMale1[i] = _arcMale2[i] = _arcFemale1[i] = _arcFemale2[i] = 0;
	_regionIds[0] = _regionIds[1] = 0;
	_testDone = false;
}

Scene1Sub2::~Scene1Sub2() {
}

// 0x41e490: world -> screen, y + the scroll
void Scene1Sub2::xform(void *user, int &x, int &y) {
	y += ((Scene1Sub2 *)user)->_scroll.y;
}

// 0x41e510
void Scene1Sub2::initHooks() {
	_hooks.user = this;
	_hooks.use = hookUse;
	_femaleHp = 2;
	_maleHp = 2;
	_hooks.region = hookRegion;
	_hooks.findTarget = hookFindTarget;
	_hooks.hitTarget = hookHitTarget;
}

// 0x4558e0: the slot's sound, looped
void Scene1Sub2::loopSound(int id) {
	Slot &s = slot(id);
	_game->stopSlotSound(s);
	if (s.sound.size() < 18)
		return;
	uint32 pcm = 18 + READ_LE_UINT16(s.sound.data() + 16);
	if (pcm >= s.sound.size())
		return;
	_game->playVoice(s.voice, s.sound.data(), s.sound.data() + pcm, s.sound.size() - pcm, 0, 0, true);
}

// ---- m2 / m3 / m4 / m5 ----

// m2 0x41e7b0
int Scene1Sub2::load(int fromScene, int fromSub) {
	return (int16)submit(kVideoList, 1, 99);
}

// 0x41e0b0: the graveyard hides, the box video plays, the cave loads
int Scene1Sub2::arrival() {
	ned()->show(false);
	top()->backdropVisible(false);
	top()->hillsVisible(false);
	visible(0x82, false);
	visible(0x65, false);
	visible(0x1e5, false);
	visible(0x191, false);
	int r = wait(kVideoList);
	if (r != 0)
		return r;
	BitmapRec *rec = bmp(kVideoBmp);
	if (rec && rec->object) {
		rec->object->setDestination(_game->display().getPrimary());
		rec->object->setDrawOrder(99);
	}
	ned()->listLoad(0xd);
	r = (int16)submit(kBoxList, 1, 0x1b);
	if (r != 0)
		return r;
	r = (int16)submit(kList, kListCount, 0x1b);
	if (r != 0)
		return r;

	MenuVideo video;
	int count = video.open(_game, 0x49) ? video.getFrameCount() : 0;
	uint32 next = 0;
	uint16 frame = 0;
	while ((int)frame < count && !_game->getEngine()->shouldQuit()) {
		uint32 t = _game->now();
		if (next < t) {
			top()->cloudsDrift(t);
			if (rec)
				video.decodeFrame(frame, rec->bitmap.surface);
			next = t + 0x42;
			frame++;
			draw();
		}
		t = _game->now();
		frameWait(t < next ? next - t : 0, t);
	}
	top()->layersVisible(false);
	top()->cloudsVisible(false);
	video.close();
	release(kVideoList, 1);
	return 0;
}

// 0x4870a0 over 99999007.SST with 0x41e550: sprites only when asked; the bitmaps
// (DAT_004ddb88 by NAF 99, scene 27, id) always
void Scene1Sub2::applyPlacements(bool sprites) {
	for (const Placement &p : _sst) {
		if (p.kind == 2) {
			if (sprites)
				applyPlacement(p);
		} else if ((p.kind == 0 || p.kind == 1) && p.area == 99 && p.slot == 27) {
			uint32 va = 0;
			switch (p.sub) {
			case 1: va = kBmpCave; break;
			case 2: va = kBmpFront; break;
			case 11: va = kBmpShaft; break;
			case 13: va = kBmp13; break;
			case 14: va = kBmp14; break;
			case 15: va = kBmp15; break;
			default: break;
			}
			BitmapRec *rec = va ? bmp(va) : nullptr;
			if (rec) {
				rec->x = p.x;
				rec->y = p.y;
				rec->z = p.z;
			}
		}
	}
}

// m3 0x41e7d0
int Scene1Sub2::enter(int fromScene, int fromSub) {
	testHook();
	_facing = ned()->getFacing();
	_topPos = top()->_nedPos;
	_blockVision = 0;
	_boxSavedX = slot(kBox).x;
	_boxSavedY = slot(kBox).y;
	_failed = 0;
	_climbed = 0;
	_targets.clear();
	_maleCarried = 0;
	_femaleCarried = 0;
	_scroll = Common::Point(0, 0);
	_boxY = -100;
	_boxX = 0xf0;
	ned()->setWrap1(true, 0x280, xform, this);
	slotAt(kNedArea, 4).offsets[0] = 6;
	int r = arrival();
	if (r != 0)
		return r;
	if ((r = wait(kBoxList)) != 0)
		return r;
	showSprite(kBoxList, ned()->getZ() + 1, true);
	_world = CollisionWorld();
	if ((r = wait(kList)) != 0)
		return r;
	addMap(kMapFloor);
	if ((r = wait(kList + 0x24)) != 0)
		return r;
	// The original adds map 1 again, with map 2's bounds raised by 0x50
	CollisionMap *top2 = map(kMapTop);
	int32 b[4] = { 0, 0, 0, 0 };
	if (top2) {
		b[0] = top2->bounds.left;
		b[1] = top2->bounds.top - 0x50;
		b[2] = top2->bounds.right;
		b[3] = top2->bounds.bottom - 0x50;
	}
	addMapBounds(kMapFloor, b[0], b[1], b[2], b[3]);
	_nedPos = Common::Point(0x136, b[1]);
	if ((r = wait(kList + 6 * 0x24)) != 0)
		return r;
	CollisionMap *regions = map(kMapRegions);
	if (regions)
		addMapBounds(kMapRegions, regions->bounds.left, regions->bounds.top - 0x50, regions->bounds.right,
			regions->bounds.bottom - 0x50);
	// 0x4871e0 and 0x487600: two empty regions on a new region map
	_userMap.regions.clear();
	static const int32 ids[2] = { 0x89, 0x84 };
	for (int i = 0; i < 2; i++) {
		CollisionMap::Region reg;
		reg.type = 0x80000;
		reg.id = ids[i];
		reg.rect = Common::Rect(0, 0, 0, 0);
		_userMap.regions.push_back(reg);
		_regionIds[i] = ids[i];
	}
	_world.setUser(&_userMap);
	if ((r = wait(0x49cd1c)) != 0)
		return r;
	Common::Array<byte> &pal = _palettes[kPaletteCave];
	pal.resize(256 * 3);
	ned()->copyPalette(pal.data());
	_game->display().applyPalette(pal.data(), kPalAll);
	staticPalette();
	_game->display().applyPalette(top()->_palette524d80, kPalStatic);
	ned()->listShow(0xd);
	int z = ned()->getZ();
	showSprite(0x49cd64, z + 5, false);
	showSprite(0x49cd88, z + 5, false);
	showSprite(0x49cdac, z + 3, false);
	showSprite(0x49cdd0, z + 1, false);
	showSprite(0x49cdf4, z + 2, false);
	showSprite(0x49ce18, z + 3, false);
	showSprite(0x49ce3c, z + 3, false);
	showSprite(0x49ce60, z + 3, false);
	showSprite(0x49ce84, z + 3, false);
	for (uint32 e = 0x49cea8; e <= 0x49cfa4; e += 0x24)
		showSprite(e, z + 2, false);
	showBitmap(0x49cd1c, 2000, true);
	if ((r = wait(0x49cd40)) != 0)
		return r;
	showBitmap(0x49cd40, 2, true);
	if ((r = wait(0x49ccf8)) != 0)
		return r;
	showBitmap(0x49ccf8, 2000, true);
	if ((r = wait(0x49cc8c)) != 0)
		return r;
	showBitmap(0x49cc8c, 2, true);
	if ((r = wait(0x49ccb0)) != 0)
		return r;
	showBitmap(0x49ccb0, 2, true);
	if ((r = wait(0x49ccd4)) != 0)
		return r;
	showBitmap(0x49ccd4, 2, true);
	if (!loadSst(0, 999, 7, _sst))
		return 1;

	if (_femaleHp == 2 && _maleHp == 2) {
		// Both zombies whole: everything from the start
		applyPlacements(true);
		_maleCarried = 0;
		_femaleCarried = 0;
		_maleOpen = 0;
		_femaleOpen = 0;
		_ladderOpen = 0;
		regionEnable(regions, 1, false);
		regionEnable(regions, 2, false);
		regionEnable(regions, 3, false);
		_maleCoffin = switchSlot(kMaleCoffin, -1);
		_femaleCoffin = switchSlot(kFemaleCoffin, -1);
		_lid = switchSlot(kLid, -1);
		_male = switchSlot(-1, -1);
		_female = switchSlot(-1, -1);
		_maleArm1 = switchSlot(-1, -1);
		_maleArm2 = switchSlot(-1, -1);
		_femaleArm1 = switchSlot(-1, -1);
		_femaleArm2 = switchSlot(-1, -1);
	} else {
		// Back after beating them: they stay as they were, the coffins open
		applyPlacements(false);
		_ladderOpen = 1;
		showSprite(0x49d034, z + 2, false);
		showSprite(0x49cfec, z + 2, false);
		_maleCoffin = switchSlot(_maleCoffin, -1);
		_femaleCoffin = switchSlot(_femaleCoffin, -1);
		_lid = switchSlot(_lid, -1);
		_male = switchSlot(_male, -1);
		_female = switchSlot(_female, -1);
		_maleArm1 = switchSlot(_maleArm1, -1);
		_maleArm2 = switchSlot(_maleArm2, -1);
		_femaleArm1 = switchSlot(_femaleArm1, -1);
		_femaleArm2 = switchSlot(_femaleArm2, -1);
		slot(kMaleCoffin).step = slot(kMaleCoffin).sequenceLength - 1;
		slot(kFemaleCoffin).step = slot(kFemaleCoffin).sequenceLength - 1;
	}
	static const uint32 bitmaps[] = { kBmpFront, kBmpCave, kBmpShaft, kBmp13, kBmp14, kBmp15 };
	for (uint32 va : bitmaps) {
		BitmapRec *rec = bmp(va);
		if (rec && rec->object)
			rec->object->setPosition(rec->x, rec->y);
	}
	_carryFemale = switchSlotAt(kNedArea, -1, -1);
	_carryMale = switchSlotAt(kNedArea, -1, -1);
	slot(kBox).y = -100;
	slot(kBox).x = 0xf0;
	_box = switchSlot(kBox, -1);
	for (int y = 0x59f; y >= 0; y--) {
		if (solidAt(kMapFloor, _nedPos.x, (int16)y)) {
			_boxStop = y - 0x3d3;
			break;
		}
	}
	ned()->resetIdle();
	ned()->setCurSlot(1);
	return wait(kLastEntry);
}

// m4 0x420e30
int Scene1Sub2::leave(int toScene, int toSub) {
	// 0x45a5f0 is called without its list argument: nothing to free
	_targets.clear();
	release(kList, kListCount);
	release(kBoxList, 1);
	_world = CollisionWorld();
	_sst.clear();
	// The original frees Ned's list 4, not the list 0xd it loaded
	ned()->listFree(4);
	slotAt(kNedArea, kCarryMale).x = 0;
	slotAt(kNedArea, kCarryMale).y = 0;
	slotAt(kNedArea, kCarryFemale).x = 0;
	slotAt(kNedArea, kCarryFemale).y = 0;
	return 0;
}

// m5 0x420ec0
int Scene1Sub2::unload(int toScene, int toSub) {
	slot(kBox).x = _boxSavedX;
	slot(kBox).y = _boxSavedY;
	if (toScene != 1) {
		ned()->show(false);
		ned()->resetIdle();
		top()->leave(toScene, toSub);
		top()->unload(toScene, toSub);
		return 0;
	}
	top()->backdropVisible(true);
	top()->hillsVisible(true);
	top()->cloudsVisible(true);
	visible(0x82, true);
	visible(0x65, true);
	visible(0x1e5, true);
	visible(0x191, true);
	_game->display().applyPalette(top()->_palette524d80, kPalStatic);
	loopSound(0x82);
	ned()->enable(true);
	Common::Point &pos = top()->_nedPos;
	pos.x = _facing == 0 ? _topPos.x - 100 : _topPos.x + 100;
	pos.y = 0;
	do {
		if (solidAt(Scene1::kMapFloor, (int16)pos.x, (int16)pos.y))
			break;
		pos.y++;
	} while (pos.y < 0x1e0);
	ned()->resetIdle();
	top()->exitsEnable();
	return 0;
}

// ---- m7 ----

// m7 0x420c80
int Scene1Sub2::tick(uint32 input, uint32 now) {
	if (now < _last + kMsPerStep) {
		g_system->delayMillis(MIN<uint32>(_last + kMsPerStep - now, 10));
		return 0;
	}
	_last = now;
	uint32 in = sink() ? input : 0;
	if (_box > -1)
		place(kBox, kBox, true, xform, this);
	maleCoffinTick(now);
	femaleCoffinTick(now);
	zombieTick(false, now);
	zombieTick(true, now);
	armTick(_maleArm1, kMaleArm1, kArmLand1, _arcMale1, kFemaleArm1, now);
	armTick(_maleArm2, kMaleArm2, kArmLand2, _arcMale2, kFemaleArm2, now);
	armTick(_femaleArm1, kFemaleArm1, kArmLand3, _arcFemale1, kFemaleArm1, now);
	armTick(_femaleArm2, kFemaleArm2, kArmLand4, _arcFemale2, kFemaleArm2, now);
	carryTick(_carryFemale, 0x28, false, now);
	carryTick(_carryMale, -0x28, true, now);
	top()->puffTick(now, xform, this);
	if (_climbed)
		in = kInputUp;
	int f = ned()->getFacing();
	ned()->tick(_world, in, f, _nedPos, now, _scroll.x, _scroll.y, &_hooks);
	if (ned()->getState() == 5)
		_climbed = 1;
	climbScroll(now);
	draw();
	if (_climbed && ned()->getState() == 4) {
		switchTo(1, -1);
	} else if (_failed) {
		_femaleHp = 2;
		_maleHp = 2;
		_hooks.use = hookUse;
		_hooks.region = hookRegion;
		_hooks.findTarget = hookFindTarget;
		_hooks.hitTarget = hookHitTarget;
		switchTo(1, -1);
	}
	return 0;
}

void Scene1Sub2::adjBitmaps(int dy) {
	static const uint32 bitmaps[] = { kBmp13, kBmp14, kBmp15, kBmpShaft, kBmpCave, kBmpFront };
	for (uint32 va : bitmaps) {
		BitmapRec *rec = bmp(va);
		if (rec && rec->object)
			rec->object->adjPosition(0, dy);
	}
}

// 0x41e2c0: the box sinks with Ned, the view follows it down, then it sinks away and
// the coffins open; true once Ned may move
bool Scene1Sub2::sink() {
	if (_climbed != 0)
		return true;
	int dy = 0;
	bool done = true;
	if (_boxY < _boxStop) {
		_boxY += 6;
		_nedPos.y += 6;
		done = false;
	} else if (_scroll.y < -0x3bf) {
		_boxY += 6;
		done = 0x5a0 < _boxY;
		if (done && _box != -1) {
			_box = switchSlot(-1, _box);
			_maleOpen = 1;
			_femaleOpen = 1;
		}
	} else {
		dy = _scroll.y - 6 < -0x3c0 ? -0x3c0 - _scroll.y : -6;
		_boxY -= dy;
		_nedPos.y -= dy;
		done = false;
	}
	_scroll.y += dy;
	// 0x487fc0 on map 2, which the world does not hold (map 1 went in with its bounds)
	CollisionMap *m = map(kMapTop);
	if (m) {
		Common::Rect r = m->bounds;
		r.top += dy;
		r.bottom += dy;
		_world.moveMap(m, r);
	}
	adjBitmaps(dy);
	slot(kBox).y = _boxY;
	return done;
}

// 0x420940: the male's coffin; its own deadline is never set, so it steps every tick
void Scene1Sub2::maleCoffinTick(uint32 now) {
	if (_maleCoffin < 0)
		return;
	if (slot(_maleCoffin).deadline < now) {
		Slot &c = slot(kMaleCoffin);
		if (_maleOpen == 0) {
			if (_maleCarried != 0) {
				if (c.step == c.sequenceLength - 1)
					playSound(kMaleCoffin);
				c.step--;
				if (c.step < 0) {
					_failed = 1;
					c.step = 0;
				}
			}
		} else {
			c.step++;
			if (c.step == 1)
				playSound(kMaleCoffin);
			if (c.sequenceLength <= c.step)
				c.step = c.sequenceLength - 1;
			if (c.step == 1)
				_male = switchSlot(kMale, _male);
		}
		if (_lid != -1) {
			slot(kLid).step = c.step == 0 ? (_failed != 0 ? 1 : 0) : 1;
			int dur = place(kLid, kLid, true, xform, this);
			slot(kLid).deadline = now + (int16)dur * kMsPerStep;
		}
		if (_maleCoffin == -1)
			return;
	}
	place(kMaleCoffin, kMaleCoffin, true, xform, this);
}

// 0x420b00: the female's coffin; opening it hides the male's slot (the original passes
// DAT_004baf38 as the old slot)
void Scene1Sub2::femaleCoffinTick(uint32 now) {
	if (_femaleCoffin < 0)
		return;
	if (slot(_femaleCoffin).deadline < now) {
		Slot &c = slot(kFemaleCoffin);
		if (_femaleOpen == 0) {
			if (_femaleCarried != 0) {
				if (c.step == c.sequenceLength - 1)
					playSound(kFemaleCoffin);
				c.step--;
				if (c.step < 0) {
					_failed = 1;
					c.step = 0;
				}
			}
		} else {
			if (c.step == 1)
				playSound(kFemaleCoffin);
			c.step++;
			if (c.sequenceLength <= c.step)
				c.step = c.sequenceLength - 1;
			if (c.step == 1)
				_female = switchSlot(kFemale, _male);
		}
		int dur = place(kFemaleCoffin, kFemaleCoffin, true, xform, this);
		c.deadline = now + (int16)dur * kMsPerStep;
		return;
	}
	place(kFemaleCoffin, kFemaleCoffin, true, xform, this);
}

// Inside 0x41fa40 / 0x41f420: both zombies armless, the ladder regions and maps come on
void Scene1Sub2::openLadder() {
	_ladderOpen = 1;
	CollisionMap *regions = map(kMapRegions);
	regionEnable(regions, 1, true);
	regionEnable(regions, 2, true);
	regionEnable(regions, 3, true);
	addMap(kMapLadder3);
	addMap(kMapLadder4);
	addMap(kMapLadder5);
	addMap(kMapLadder6);
}

// 0x41fa40 (male) / 0x41f420 (female): climbs out, walks at Ned, grabs him on touch
void Scene1Sub2::zombieTick(bool male, uint32 now) {
	static const Zombie zm = { kMale, kMaleWalk, kMaleGrab, kMale, kMaleOneArm, kMaleNoArms, 3, 0, 7, kCarryMale };
	static const Zombie zf = { kFemale, kFemaleWalk, kFemaleGrab, kFemale, kFemaleOneArm, kFemaleNoArms, -3, 1, 10, kCarryFemale };
	const Zombie &d = male ? zm : zf;
	int16 &cur = male ? _male : _female;
	int16 other = male ? _female : _male;
	int16 otherNoArms = male ? kFemaleNoArms : kMaleNoArms;
	int16 &carry = male ? _carryMale : _carryFemale;
	if (cur < 0)
		return;
	if (now <= slot(cur).deadline) {
		place(cur, d.ref, true, xform, this);
		return;
	}
	if (cur == d.walk) {
		slot(d.ref).x += d.dx;
	} else if (cur == d.noArms && _ladderOpen == 0) {
		if (other == otherNoArms || other == -1)
			openLadder();
	} else if (cur == d.grab) {
		if (slot(d.grab).step == d.grabStep) {
			_blockVision = 1;
			ned()->show(false);
			ned()->enable(false);
			playSound(d.grab);
		} else if (slot(d.grab).step == d.carryStep) {
			carry = switchSlotAt(kNedArea, d.carrySlot, carry);
			slotAt(kNedArea, d.carrySlot).x = slot(d.grab).x;
			slotAt(kNedArea, d.carrySlot).y = slot(d.grab).y;
		}
	}
	slot(cur).step++;
	if (slot(cur).step >= slot(cur).sequenceLength) {
		targetRemove(kArea, cur);
		bool add = true;
		if (cur == d.walk) {
			slot(d.walk).step = 0;
			playSound(d.walk);
		} else if (cur == d.grab) {
			cur = switchSlot(d.walk, cur);
			add = false;
		} else if (cur == d.emerge) {
			cur = switchSlot(d.walk, cur);
		} else if (cur == d.oneArm || cur == d.noArms) {
			slot(cur).step = 0;
		} else {
			add = false;
		}
		if (add)
			targetAdd(kArea, cur, true);
	}
	int frame = 0;
	int dur = place(cur, d.ref, true, xform, this, &frame);
	slot(cur).deadline = now + (int16)dur * kMsPerStep;
	if (cur == d.walk && ned()->isEnabled()) {
		Slot &w = slot(d.walk);
		int32 r[4];
		boxRect(kArea, d.walk, frame, 1, r);
		Common::Point p = w.sprite ? w.sprite->getPosition() : Common::Point(0, 0);
		r[0] += p.x;
		r[2] += p.x;
		r[1] += p.y;
		r[3] += p.y;
		int32 n[4];
		ned()->getBox(ned()->getCurSlot(), _nedPos, n);
		n[3] += _scroll.y;
		n[1] += _scroll.y;
		if (intersect(n, r)) {
			targetRemove(kArea, cur);
			cur = switchSlot(d.grab, cur);
			dur = place(cur, d.ref, true, xform, this);
			slot(cur).deadline = now + (int16)dur * kMsPerStep;
		}
	}
}

// 0x420060 (male: Ned carried left) / 0x420170 (female: right); off screen, that
// zombie's coffin shuts
void Scene1Sub2::carryTick(int16 &cur, int dx, bool male, uint32 now) {
	if (cur < 0)
		return;
	Slot &s = slotAt(kNedArea, cur);
	if (s.deadline >= now)
		return;
	s.step++;
	if (s.sequenceLength <= s.step)
		s.step = 0;
	s.x += dx;
	int dur = placeAt(kNedArea, cur, cur, true, xform, this);
	s.deadline = now + (int16)dur * kMsPerStep;
	int x = s.sprite ? s.sprite->getPosition().x : 0;
	if (male && x < -0x78) {
		_maleCarried = 1;
		_maleOpen = 0;
		cur = switchSlotAt(kNedArea, -1, cur);
	} else if (!male && 0x2f8 < x) {
		_femaleCarried = 1;
		_femaleOpen = 0;
		cur = switchSlotAt(kNedArea, -1, cur);
	}
}

// 0x421260: an arm's flight from slot from's position to slot to's (to's y less half
// its width); +0x10 is never set, so it only keeps the largest |dy|
void Scene1Sub2::arcStart(int32 *arc, int16 from, int16 to) {
	arc[0] = slot(from).x;
	arc[1] = slot(from).y;
	arc[2] = slot(to).x;
	arc[3] = slot(to).y - slot(to).w / 2;
	if (arc[4] < 0)
		arc[4] = -arc[4];
	arc[5] = arc[1] - arc[3];
	if (arc[5] < 0)
		arc[5] = -arc[5];
	if (arc[4] < arc[5])
		arc[4] = arc[5];
	arc[8] = 1;
	arc[6] = arc[4] / 7;
	arc[7] = arc[4] / 3;
}

// 0x420280 / 0x420430 / 0x4205e0 / 0x420790: a flying arm follows its arc, then lands
void Scene1Sub2::armTick(int16 &cur, int16 arm, int16 land, int32 *arc, int16 soundSlot, uint32 now) {
	if (cur < 0)
		return;
	if (slot(cur).deadline < now) {
		Slot &s = slot(cur);
		s.step++;
		if (s.sequenceLength <= s.step)
			s.step = 0;
		if (cur == arm) {
			if (arc[8] < arc[6]) {
				slot(arm).x = (arc[2] - arc[0]) * arc[8] / arc[6] + arc[0];
				int y = (arc[3] - arc[1]) * arc[8] / arc[6] + arc[1];
				slot(arm).y = y;
				double a = sin((double)arc[8] * 3.1415926535 / (double)arc[6]) * (double)arc[7];
				slot(arm).y = y - (int)a;
				arc[8]++;
			} else {
				stopSound(soundSlot);
				cur = switchSlot(land, cur);
			}
		}
		int dur = place(cur, cur, true, xform, this);
		slot(cur).deadline = now + (int16)dur * kMsPerStep;
		return;
	}
	place(cur, cur, true, xform, this);
}

// 0x421870: while Ned climbs (state 5) the view comes back up 2 px every 20 ms
void Scene1Sub2::climbScroll(uint32 now) {
	if (ned()->getState() != 5 || now <= _scrollTime + 0x14)
		return;
	int dy = 0;
	if (_scroll.y < 0) {
		int v = _scroll.y + 2;
		dy = 2;
		_scroll.y = v;
		if (0 < v) {
			_scroll.y = 0;
			dy = v;
		}
	}
	adj(_lid, 0, dy);
	adj(_maleCoffin, 0, dy);
	adj(_femaleCoffin, 0, dy);
	adj(_box, 0, dy);
	adj(_male, 0, dy);
	adj(_female, 0, dy);
	adj(_maleArm1, 0, dy);
	adj(_maleArm2, 0, dy);
	adj(_femaleArm1, 0, dy);
	adj(_femaleArm2, 0, dy);
	static const uint32 bitmaps[] = { kBmpFront, kBmpCave, kBmpShaft, kBmp13, kBmp14, kBmp15 };
	for (uint32 va : bitmaps) {
		BitmapRec *rec = bmp(va);
		if (rec && rec->object)
			rec->object->adjPosition(0, dy);
	}
	ned()->adjPosition(0, dy);
	_scrollTime = _game->now();
}

// ---- Hooks ----

// 0x421020: the zombie in front of Ned and its box 0, in screen terms
int Scene1Sub2::findTargetRect(int16 &dir, int16 &found, int32 rect[4]) {
	found = -1;
	int unused = 0;
	if (!ned()->findTarget(ned()->getFacing(), ned()->getCurSlot(), _targets, dir, unused, found))
		return 1;
	// The original leaves the offset uninitialised for other slots (none are targets)
	int pos[2] = { 0, 0 };
	int16 ref = -1;
	switch (found) {
	case kMaleWalk:
	case kMaleOneArm:
	case kMaleNoArms:
		ref = kMale;
		break;
	case kFemaleWalk:
	case kFemaleOneArm:
	case kFemaleNoArms:
		ref = kFemale;
		break;
	default:
		break;
	}
	if (ref != -1)
		attachPos(found, ref, pos, true);
	int frame = currentFrame(kArea, found);
	boxRect(kArea, found, frame, 0, rect);
	rect[0] += pos[0];
	rect[1] += pos[1];
	rect[3] += pos[1];
	rect[2] += pos[0];
	return 1;
}

// 0x421210: state 0's slot aims at the target
int Scene1Sub2::hookUse(void *user, int x, int y, int16 *state) {
	Scene1Sub2 *s = (Scene1Sub2 *)user;
	int16 dir = -1, found = -1;
	int32 rect[4];
	s->findTargetRect(dir, found, rect);
	s->ned()->setUseSlot(dir);
	return 1;
}

// 0x421200: any region (the ladder) puts Ned in state 0xe
int Scene1Sub2::hookRegion(void *user, int32 id, uint32 flags, int x, int y, int16 *state) {
	*state = 0xe;
	return 1;
}

// 0x421830: a is always 2 (the direction goes to a copy)
void Scene1Sub2::hookFindTarget(void *user, int x, int y, int16 *a, int16 *b, int32 rect[4]) {
	*a = 2;
	int16 dir = 2;
	((Scene1Sub2 *)user)->findTargetRect(dir, *b, rect);
}

// 0x421320
int Scene1Sub2::hookHitTarget(void *user, int x, int y, int16 *a, int16 *b, const int32 rect[4]) {
	return ((Scene1Sub2 *)user)->hitTarget(*b);
}

// 0x421320: a zap knocks an arm off (it flies to the floor), the third leaves a puff
int Scene1Sub2::hitTarget(int16 found) {
	int z = ned()->getZ();
	switch (found) {
	case kMaleWalk:
		arcStart(_arcMale1, kMale, kArmLand1);
		showSprite(0x49cfc8, z + 2, false);
		targetRemove(kArea, _male);
		_maleArm1 = switchSlotPre(kMaleArm1, -1);
		loopSound(kFemaleArm1);
		_male = switchSlotPre(kMaleOneArm, _male);
		zombieTick(true, 0xffffffff);
		armTick(_maleArm1, kMaleArm1, kArmLand1, _arcMale1, kFemaleArm1, 0xffffffff);
		targetAdd(kArea, _male, true);
		_maleHp--;
		return 1;
	case kFemaleWalk:
		arcStart(_arcFemale1, kFemale, kArmLand3);
		showSprite(0x49d010, z + 2, false);
		targetRemove(kArea, _female);
		_femaleArm1 = switchSlotPre(kFemaleArm1, -1);
		loopSound(kFemaleArm1);
		_female = switchSlotPre(kFemaleOneArm, _female);
		zombieTick(false, 0xffffffff);
		armTick(_femaleArm1, kFemaleArm1, kArmLand3, _arcFemale1, kFemaleArm1, 0xffffffff);
		targetAdd(kArea, _female, true);
		_femaleHp--;
		return 1;
	case kMaleOneArm:
		arcStart(_arcMale2, kMale, kArmLand2);
		showSprite(0x49cfec, z + 2, false);
		targetRemove(kArea, _male);
		_maleArm2 = switchSlotPre(kMaleArm2, -1);
		loopSound(kFemaleArm2);
		_male = switchSlotPre(kMaleNoArms, _male);
		targetAdd(kArea, _male, true);
		zombieTick(true, 0xffffffff);
		armTick(_maleArm2, kMaleArm2, kArmLand2, _arcMale2, kFemaleArm2, 0xffffffff);
		_maleHp--;
		return 1;
	case kMaleNoArms:
		targetRemove(kArea, _male);
		_male = switchSlot(-1, _male);
		top()->puffStart(kArea, kMale, z + 2, _game->now(), xform, this);
		return 1;
	case kFemaleOneArm:
		arcStart(_arcFemale2, kFemale, kArmLand4);
		showSprite(0x49d034, z + 2, false);
		targetRemove(kArea, _female);
		_femaleArm2 = switchSlotPre(kFemaleArm2, -1);
		loopSound(kFemaleArm2);
		_female = switchSlotPre(kFemaleNoArms, _female);
		zombieTick(false, 0xffffffff);
		armTick(_femaleArm2, kFemaleArm2, kArmLand4, _arcFemale2, kFemaleArm2, 0xffffffff);
		targetAdd(kArea, _female, true);
		_femaleHp--;
		return 1;
	case kFemaleNoArms:
		targetRemove(kArea, _female);
		_female = switchSlot(-1, _female);
		top()->puffStart(kArea, kFemale, z + 2, _game->now(), xform, this);
		return 1;
	default:
		return 1;
	}
}

// ---- Test hook ----

// The cave's bitmaps use colours 0..9 and 246..255 from DAT_00524d80, which holds the
// palette of the top's bitmap 0:24 (entry 0x4a2858). Started directly in the sub-scene
// the top has not filled it: take it from that bitmap here.
void Scene1Sub2::staticPalette() {
	byte *pal = top()->_palette524d80;
	for (int i = 0; i < 256 * 3; i++) {
		if (pal[i])
			return;
	}
	const uint32 entry = 0x4a2858;
	bool own = !top()->palette(0x524d80);
	if (own)
		submit(entry, 1, 0);
	const byte *src = palette(0x524d80);
	if (src)
		memcpy(pal, src, 256 * 3);
	if (own)
		release(entry, 1);
	debugC(1, kDebugLoading, "Graveyard sub 2: static colours from bitmap 0:24 (direct start)");
}

// nightmarened_s1_sub2=0: whole zombies (as 0x41e510 leaves them); =1: both beaten
// (armless, arms on the floor, coffins open, ladder on), as when Ned comes back
void Scene1Sub2::testHook() {
	if (_testDone || !ConfMan.hasKey("nightmarened_s1_sub2"))
		return;
	_testDone = true;
	initHooks();
	if (ConfMan.getInt("nightmarened_s1_sub2") != 1)
		return;
	Common::Array<Placement> sst;
	if (loadSst(0, 999, 7, sst)) {
		for (const Placement &p : sst) {
			if (p.kind == 2)
				applyPlacement(p);
		}
	}
	_femaleHp = _maleHp = 0;
	_maleOpen = _femaleOpen = 1;
	_maleCoffin = kMaleCoffin;
	_femaleCoffin = kFemaleCoffin;
	_lid = kLid;
	_male = kMaleNoArms;
	_female = kFemaleNoArms;
	_maleArm1 = kArmLand1;
	_maleArm2 = kArmLand2;
	_femaleArm1 = kArmLand3;
	_femaleArm2 = kArmLand4;
	debugC(1, kDebugLoading, "Graveyard sub 2: test start, both zombies beaten");
}

} // End of namespace NightmareNed
