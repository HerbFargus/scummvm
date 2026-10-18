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
#include "nightmarened/rdx.h"
#include "nightmarened/scene1.h"
#include "nightmarened/scene1_enemies1.h"
#include "nightmarened/scene1_enemies2.h"

namespace NightmareNed {

enum {
	kMapPumpkin = 0x5206a8,     // cbix 6 of list 0x4a2d90
	kPumpkinBitmaps = 0x4df2f8, // DAT_004df2f8[sub]: area-10 bitmaps 2..10
	kPatchList = 0x4b5290,      // 0x34 entries, scene 15
	kPatchCount = 0x34
};

// 0x4885a0: the distance estimate max + min / 2
static int distance(int x1, int y1, int x2, int y2) {
	int dx = ABS(x2 - x1);
	int dy = ABS(y2 - y1);
	int lo = MIN(dx, dy);
	return dy - (lo >> 1) + dx;
}

// sin(1.9634954084375) (the double at 0x4951e0), the seeds' rise per step
static const double kSeedSin = 0.92387953251128674;

Scene1Enemies1::Scene1Enemies1(Scene1 *scene) : _s(scene) {
	const ExeImage &exe = _s->game()->exe();

	_bigSlot = -1;
	_bigTimer = 0;
	_hits = 0;
	_pair1On = _pair2On = _pair3On = _pair4On = 0;
	_p1a = _p1b = _p2a = _p2b = _p3a = _p3b = _p4a = _p4b = 0;
	_r1a = _r1b = _r2a = _r2b = _r3a = _r3b = _r4a = _r4b = 0;
	_bigRegion = 0;
	_t1 = _t2 = _t3 = _t4 = 0;
	_nearA = _nearB = 0;
	_crow = 0;
	for (int i = 0; i < 4; i++)
		_biteRect[i] = _spitRect[i] = 0;
	_seed1 = (int16)exe.readUint16(0x4a1c60);
	_seed2 = (int16)exe.readUint16(0x4a1c64);
	_face = (int16)exe.readUint16(0x4a1c68);
	_shot = (int16)exe.readUint16(0x4a1c6c);
	_amp1 = _amp2 = 0;
	_shotX = _shotY = 0;
	_shotHold = 0;
	_shotDrop = _shotVy = _shotDecay = _shotFloor = _shotMax = 0;
	_expLoaded = _expShown = _p1Loaded = _p1Shown = _p2Loaded = _p2Shown = 0;
	_p3Loaded = _p3Shown = _p4Loaded = _p4Shown = 0;
	_crowALoaded = _crowAShown = _crowBLoaded = _crowBShown = 0;
	_bigLoaded = _bigShown = _artLoaded = _artShown = 0;
	_patchAsked = 0;
	_riseTime = 0;
	_x94 = _x90 = _x85 = _x82 = 0;
	_mapDx = _mapDy = 0;
	_unk660 = _unk664 = 0;

	_right = (int16)exe.readUint16(0x4a1c70);
	_rightIdle = (int16)exe.readUint16(0x4a1c74);
	_left = (int16)exe.readUint16(0x4a1c78);
	_leftIdle = (int16)exe.readUint16(0x4a1c7c);
	_mirror = exe.readSint32(0x4a1c80);
	_caught = _unk84c = 0;
	_rightHit = _leftHit = _unk8e8 = 0;
	_leftX = _rightX = _rightNext = _leftNext = 0;
	_region = 0;
	_leftCount = _rightCount = 0;
	_bellX = _bellY = 0;

	_coffinX = 0;
	_coffinTime = exe.readUint32(0x4a1c8c);
}

Scene1Enemies1::~Scene1Enemies1() {
}

// ---- Helpers ----

Slot &Scene1Enemies1::sl(int id) {
	return _s->slot(id);
}

uint32 Scene1Enemies1::now() const {
	return _s->game()->now();
}

int16 Scene1Enemies1::sw(int newSlot, int oldSlot) {
	return _s->switchSlot(newSlot, oldSlot);
}

int Scene1Enemies1::put(int id, int ref, bool noMirror, int *frame) {
	return _s->place(id, ref, noMirror, Scene1::xformPlain, _s, frame);
}

int Scene1Enemies1::putRel(int id, int ref, bool noMirror, int *frame) {
	return _s->placeAt(GraveyardRoom::kArea, id, ref, noMirror, Scene1::xformPlain, _s, frame, true);
}

void Scene1Enemies1::setDeadline(int id, uint32 t, int duration) {
	sl(id).deadline = t + (int16)duration * kMsPerStep;
}

int Scene1Enemies1::screenX(int id) {
	Slot &s = sl(id);
	return s.sprite ? s.sprite->getPosition().x : 0;
}

void Scene1Enemies1::applyFlip(int id) {
	Slot &s = sl(id);
	if (s.sprite)
		s.flip = s.sprite->applyEffect(kFxHorizFlip);
}

void Scene1Enemies1::setZ(int id, int z) {
	Slot &s = sl(id);
	s.z = z;
	if (s.sprite)
		s.sprite->setDrawOrder(z);
}

void Scene1Enemies1::drawOrder(int id, int z) {
	Slot &s = sl(id);
	if (s.sprite)
		s.sprite->setDrawOrder(z);
}

void Scene1Enemies1::loopSound(int id) {
	Slot &s = sl(id);
	Game *game = _s->game();
	game->stopSlotSound(s);
	if (s.sound.size() < 18)
		return;
	uint32 pcm = 18 + READ_LE_UINT16(s.sound.data() + 16);
	if (pcm >= s.sound.size())
		return;
	game->playVoice(s.voice, s.sound.data(), s.sound.data() + pcm, s.sound.size() - pcm, 0, 0, true);
}

void Scene1Enemies1::regionMove(int32 id, int dx, int dy) {
	CollisionMap::Region *r = GraveyardRoom::regionFind(&_s->userMap(), id);
	if (!r)
		return;
	r->rect.left += dx;
	r->rect.top += dy;
	r->rect.right += dx;
	r->rect.bottom += dy;
}

void Scene1Enemies1::nedSpriteBox(int32 out[4]) {
	int16 id = _s->ned()->getCurSlot();
	int frame = _s->currentFrame(GraveyardRoom::kNedArea, id);
	_s->boxRect(GraveyardRoom::kNedArea, id, frame, _s->ned()->getFacing() != 0 ? 0 : 3, out);
	Slot &n = _s->slotAt(GraveyardRoom::kNedArea, id);
	Common::Point p = n.sprite ? n.sprite->getPosition() : Common::Point(0, 0);
	out[0] += p.x;
	out[2] += p.x;
	out[1] += p.y;
	out[3] += p.y;
}

void Scene1Enemies1::spriteBox(int id, int frame, int k, int32 out[4]) {
	_s->boxRect(GraveyardRoom::kArea, id, frame, k, out);
	Slot &s = sl(id);
	Common::Point p = s.sprite ? s.sprite->getPosition() : Common::Point(0, 0);
	out[0] += p.x;
	out[2] += p.x;
	out[1] += p.y;
	out[3] += p.y;
}

void Scene1Enemies1::playNed(int action, int sx, int sy) {
	_s->ned()->playAction(action, _s->_nedPos, sx, sy);
}

// 0x43c880's resets of this module's globals
void Scene1Enemies1::reset() {
	_right = -1;      // DAT_004a1c70
	_rightIdle = -1;  // DAT_004a1c74
	_left = -1;       // DAT_004a1c78
	_leftIdle = -1;   // DAT_004a1c7c
}

// 0x438870
void Scene1Enemies1::init() {
	_hits = 0;
	_pair1On = 1;
	_pair2On = 1;
	_pair3On = 1;
	_pair4On = 1;
}

// ==== The Pumpkin (record 2) ====

// 0x47c6a0
void Scene1Enemies1::pumpkinBitmap(int sub) {
	if (!_s->enemyLoaded(2))
		return;
	BitmapRec *rec = _s->bmp(kPumpkinBitmaps + 4 * sub);
	if (!rec)
		return;
	int x = rec->x, y = rec->y;
	_s->toScreen(x, y);
	if (rec->object)
		rec->object->setPosition(x, y);
}

// 0x47c700
void Scene1Enemies1::pumpkinsPlace() {
	for (int sub = 2; sub <= 10; sub++)
		pumpkinBitmap(sub);
}

// 0x47c980 (the same as 0x47c700; nothing calls it)
void Scene1Enemies1::pumpkinsPlace2() {
	for (int sub = 2; sub <= 10; sub++)
		pumpkinBitmap(sub);
}

// 0x47cc00 (olist_foreach callback over the scene's placements)
void Scene1Enemies1::pumpkinApply(const Placement &p) {
	if (p.kind != 0)
		return;
	if (p.area > 0) {
		int t = (int16)_s->game()->exe().readUint16(0x4afc60 + 2 * p.area);
		BitmapRec *rec = _s->bmp(0x4ddb88 + 4 * ((t * 15 + p.slot) * 100 + p.sub));
		if (rec && p.slot == 0xf && 1 < p.sub && p.sub < 0xb) {
			rec->x = p.x;
			rec->y = p.y;
			rec->z = p.z;
			// +0x0c ([10]), +0x0e/+0x10 ([7], [8]) and +0x16 ([12]) have no BitmapRec field
		}
	}
	if (p.area == 99 && p.slot == 0xf && p.sub == 0xc) {
		_mapDx = p.x;
		_mapDy = p.y;
	}
}

// 0x47cd80 (nothing calls it)
void Scene1Enemies1::pumpkinBitmapShow(int sub) {
	BitmapRec *rec = _s->bmp(kPumpkinBitmaps + 4 * sub);
	if (rec && rec->object) {
		rec->object->setDrawOrder(0xb);
		rec->object->setVisibility(true);
	}
	pumpkinBitmap(sub);
}

// 0x47ce00
void Scene1Enemies1::pumpkinShow() {
	for (const Placement &p : _s->placements())
		pumpkinApply(p);
	_x94 = sl(0x5e).x;
	_x90 = sl(0x5a).x;
	_x85 = sl(0x55).x;
	_x82 = sl(0x52).x;
	CollisionMap *m = _s->map(kMapPumpkin);
	if (m) {
		int x0 = m->bounds.left, y0 = m->bounds.top, x1 = m->bounds.right, y1 = m->bounds.bottom;
		_s->world().addMap(m, x0, y0, x1, y1);
		Common::Rect r;
		r.left = x0 + _mapDx;
		r.top = y0 + _mapDy;
		r.right = x1 + _mapDx;
		r.bottom = y1 + _mapDy;
		_s->world().moveMap(m, r);
		GraveyardRoom::regionShift(m, _mapDx, _mapDy);
	}
	for (int sub = 2; sub <= 10; sub++) {
		BitmapRec *rec = _s->bmp(kPumpkinBitmaps + 4 * sub);
		if (rec && rec->object) {
			rec->object->setDrawOrder(0xb);
			rec->object->setVisibility(true);
		}
		pumpkinBitmap(sub);
	}
	_unk660 = _unk664 = (int32)0xffff8001;
	_s->_riseLeft = 0;
	_s->_rise = 0;
	_patchAsked = 0;
}

// 0x47d180
void Scene1Enemies1::pumpkinHide() {
	CollisionMap *m = _s->map(kMapPumpkin);
	if (m)
		_s->world().removeMap(m);
	sl(0x5e).x = _x94;
	sl(0x5a).x = _x90;
	sl(0x55).x = _x85;
	sl(0x52).x = _x82;
	if (_expLoaded) {
		_expLoaded = _expShown = 0;
		_s->ned()->listFree(8);
		_s->release(0x4b5440, 2);
	}
	if (_p1Loaded) {
		_p1Loaded = _p1Shown = 0;
		_s->release(0x4b54ac, 2);
	}
	if (_p2Loaded) {
		_p2Loaded = _p2Shown = 0;
		_s->release(0x4b553c, 2);
	}
	if (_p3Loaded) {
		_p3Loaded = _p3Shown = 0;
		_s->release(0x4b55cc, 2);
	}
	if (_p4Loaded) {
		_p4Loaded = _p4Shown = 0;
		_s->release(0x4b5584, 2);
	}
	if (_crowALoaded) {
		_crowALoaded = _crowAShown = 0;
		_s->release(0x4b54f4, 2);
	}
	if (_crowBLoaded) {
		_crowBLoaded = _crowBShown = 0;
		_s->release(0x4b5614, 2);
	}
	if (_bigLoaded) {
		_bigLoaded = _bigShown = 0;
		_s->release(0x4b565c, 1);
	}
	if (_artLoaded) {
		_artLoaded = _artShown = 0;
		_s->release(0x4b5680, 0x16);
	}
}

// 0x47d330: Ned climbing the vine raises (or lowers) the view
void Scene1Enemies1::pumpkinRise(uint32 t) {
	int dx = 0, dy = 0;
	int16 state = _s->ned()->getState();
	int side = _s->ned()->getFacing() != 0 ? -1 : 1;
	if (state == 6) {
		if (_riseTime + 0x18 < t)
			dy = -3;
	} else if (state == 5) {
		if (_riseTime + 0x18 < t)
			dy = 3;
	} else if (state == 7) {
		if (_riseTime + 0xc < t) {
			dy = 1;
			dx = side;
		}
	} else if (state == 8) {
		if (_riseTime + 0xc < t) {
			dx = side;
			dy = -1;
		}
	} else if (_s->_rise != 0 && state != 9 && state != 10) {
		// TODO 0x450240 (Ned's sprite position for his world position) is private to Ned;
		// the sprite Ned's tick placed is the same point after the xform
		int y = _s->ned()->getSpritePosition(_s->ned()->getCurSlot()).y;
		if (y + 0x78 < 0x168)
			dy = 1;
		else if (y + 0x78 >= 0x19b)
			dy = -1;
	}
	if (dy + _s->_rise < 0)
		dy = -_s->_rise;
	if (dy == 0)
		return;
	_s->_rise += dy;
	_s->riseBy(dx, dy);
	pumpkinsPlace();
	_riseTime = now();
	pumpkinScroll(dx, dy);
	_s->enemies2()->zombieScroll(dx, dy);
}

// 0x47d700
void Scene1Enemies1::pumpkinTick(uint32 t) {
	if (!_s->enemyLoaded(2))
		return;
	pumpkinUpdate(t);
	pumpkinRise(t);
	if (_s->ned()->getState() == 5 && !_patchAsked) {
		_patchAsked = 1;
		_expLoaded = _p1Loaded = _p2Loaded = _p3Loaded = _p4Loaded = 0;
		_crowALoaded = _crowBLoaded = _bigLoaded = _artLoaded = 0;
		_expShown = _p1Shown = _p2Shown = _p3Shown = _p4Shown = 0;
		_crowAShown = _crowBShown = _bigShown = _artShown = 0;
		_s->ned()->listLoad(8);
		// The list's completion callbacks 0x47c610..0x47c690 set the loaded flags; the
		// loader is synchronous, so they have all run when submit returns
		if (_s->submit(kPatchList, kPatchCount, 0xf) == 0) {
			_expLoaded = 1;    // 0x47c610
			_p1Loaded = 1;     // 0x47c620
			_p2Loaded = 1;     // 0x47c630
			_p3Loaded = 1;     // 0x47c640
			_p4Loaded = 1;     // 0x47c650
			_crowALoaded = 1;  // 0x47c660
			_crowBLoaded = 1;  // 0x47c670
			_bigLoaded = 1;    // 0x47c680
			_artLoaded = 1;    // 0x47c690
		}
	}
}

// 0x47d7e0
bool Scene1Enemies1::pumpkinsDone() const {
	return 3 < _hits;
}

// 0x434a60
void Scene1Enemies1::pumpkinRegions(int dx, int dy) {
	if (_p1Shown) {
		regionMove(_r1a, dx, dy);
		regionMove(_r1b, dx, dy);
	}
	if (_p2Shown) {
		regionMove(_r2a, dx, dy);
		regionMove(_r2b, dx, dy);
	}
	if (_p3Shown) {
		regionMove(_r3a, dx, dy);
		regionMove(_r3b, dx, dy);
	}
	if (_p4Shown) {
		regionMove(_r4a, dx, dy);
		regionMove(_r4b, dx, dy);
	}
	if (_bigShown)
		regionMove(0x125, dx, dy);
}

// 0x434b70
void Scene1Enemies1::pumpkinScroll(int dx, int dy) {
	if (_bigShown && _bigSlot != -1)
		_s->adj(_bigSlot, dx, dy);
	if (_artShown) {
		if (_seed1 != -1)
			_s->adj(0x1c9, dx, dy);
		if (_seed2 != -1)
			_s->adj(0x1e2, dx, dy);
		if (_face != -1)
			_s->adj(_face, dx, dy);
	}
	if (_crowBShown)
		_s->adj(99, dx, dy);
	if (_crowAShown)
		_s->adj(0x61, dx, dy);
	if (_expShown) {
		_s->adj(0x1be, dx, dy);
		_s->adj(0x122, dx, dy);
	}
	if (_p4Shown) {
		_s->adj(0x5d, dx, dy);
		_s->adj(0x5e, dx, dy);
	}
	if (_p3Shown) {
		_s->adj(0x5a, dx, dy);
		_s->adj(0x59, dx, dy);
	}
	if (_p2Shown) {
		_s->adj(0x55, dx, dy);
		_s->adj(0x56, dx, dy);
	}
	if (_p1Shown) {
		_s->adj(0x51, dx, dy);
		_s->adj(0x52, dx, dy);
	}
	if (_shot != -1)
		_s->adj(_shot, dx, dy);
	pumpkinRegions(dx, dy);
}

// 0x434de0: the big pumpkin's animations (list 0x4b5680) come in
bool Scene1Enemies1::bigArtShow() {
	if (!_artLoaded || _artShown)
		return _artShown != 0;
	_artShown = 1;
	if (3 < _hits) {
		setZ(299, 10);
		_s->attachList(0x4b5710, 1);
		_bigTimer = 0x7fffffff;
		_bigSlot = sw(299, -1);
		put(299, 0x125, true);
		sl(299).deadline = 0;
		_face = -1;
		_shot = -1;
		return true;
	}
	_s->attachList(0x4b5680, 0x18);
	static const int16 z10[] = { 0x128, 0x126, 0x129, 0x12a, 299, 0x127, 0x199 };
	for (int16 id : z10)
		setZ(id, 10);
	static const int16 z9[] = { 0x1d9, 0x1d8, 0x1d7, 0x1db, 0x1da, 0x198, 0x1dd, 0x1dc, 0x1b5,
		0x1df, 0x1de, 0x1c6, 0x1e1, 0x1e0, 0x1c8, 0x1c9, 0x1e2 };
	for (int16 id : z9)
		setZ(id, 9);
	CollisionMap *m = _s->map(kMapPumpkin);
	if (m) {
		for (const CollisionMap::Region &r : m->regions) {
			if (r.id == 6000) {
				_biteRect[1] = r.rect.top - 1;
				_biteRect[0] = r.rect.left - 1;
				_biteRect[2] = r.rect.right + 1;
				_biteRect[3] = r.rect.bottom + 1;
			} else if (r.id == 0x1771) {
				_spitRect[1] = r.rect.top - 1;
				_spitRect[0] = r.rect.left - 1;
				_spitRect[2] = r.rect.right + 1;
				_spitRect[3] = r.rect.bottom + 1;
			}
		}
	}
	_hits = 0;
	_seed1 = -1;
	_seed2 = -1;
	return true;
}

// 0x435220: the big pumpkin (0x125) comes in
bool Scene1Enemies1::bigShow() {
	if (!_bigLoaded || _bigShown)
		return _bigShown != 0;
	int32 rect[4] = { 0, 0, 0, 0 };
	_bigShown = 1;
	if (_hits < 4) {
		_s->attachList(0x4b565c, 1);
		setZ(0x125, 10);
		_bigTimer = 0x7fffffff;
		_s->targetAdd(GraveyardRoom::kArea, 0x125, (sl(0x125).flags & 4) != 0);
		_bigSlot = sw(0x125, -1);
		put(0x125, 0x125, true);
		sl(0x125).deadline = 0;
		_face = -1;
		_shot = -1;
		_s->regionAdd(0, 0x125, rect, &_bigRegion);
		return true;
	}
	_bigSlot = -1;
	return true;
}

// The four small pumpkins: 0x435a30 (0x51/0x52), 0x435860 (0x55/0x56), 0x435690 (0x59/0x5a),
// 0x4354c0 (0x5d/0x5e)
bool Scene1Enemies1::pairShow(int &loaded, int &shown, int on, uint32 list, int a, int b, int flipSlot,
		int16 &slotA, int16 &slotB, int32 &regionA, int32 &regionB) {
	if (!loaded || shown)
		return shown != 0;
	shown = 1;
	if (!on) {
		slotA = -1;
		slotB = -1;
		return true;
	}
	int32 rect[4] = { 0, 0, 0, 0 };
	_s->attachList(list, 2);
	_s->targetAdd(GraveyardRoom::kArea, a, (sl(a).flags & 4) != 0);
	_s->targetAdd(GraveyardRoom::kArea, b, (sl(b).flags & 4) != 0);
	applyFlip(flipSlot);
	setZ(a, 10);
	setZ(b, 10);
	sl(flipSlot).x += sl(flipSlot).w;
	slotA = sw(a, -1);
	slotB = sw(b, -1);
	sl(slotB).deadline = 0x7fffffff;
	_s->regionAdd(0, a, rect, &regionA);
	_s->regionAdd(0, b, rect, &regionB);
	return true;
}

// 0x435a30
bool Scene1Enemies1::pair1Show() {
	return pairShow(_p1Loaded, _p1Shown, _pair1On, 0x4b54ac, 0x51, 0x52, 0x52, _p1a, _p1b, _r1a, _r1b);
}

// 0x435860
bool Scene1Enemies1::pair2Show() {
	return pairShow(_p2Loaded, _p2Shown, _pair2On, 0x4b553c, 0x55, 0x56, 0x55, _p2a, _p2b, _r2a, _r2b);
}

// 0x435690
bool Scene1Enemies1::pair3Show() {
	return pairShow(_p3Loaded, _p3Shown, _pair3On, 0x4b55cc, 0x59, 0x5a, 0x5a, _p3a, _p3b, _r3a, _r3b);
}

// 0x4354c0
bool Scene1Enemies1::pair4Show() {
	return pairShow(_p4Loaded, _p4Shown, _pair4On, 0x4b5584, 0x5d, 0x5e, 0x5e, _p4a, _p4b, _r4a, _r4b);
}

// 0x435cb0: the two crows (0x61 and 99) take turns
void Scene1Enemies1::crowTick(int16 id, uint32 t) {
	Slot &s = sl(id);
	if (s.deadline < t) {
		s.step++;
		if (s.sequenceLength <= s.step) {
			s.step = 0;
			if (id == 99)
				_crow = 0x61;
			else if (id == 0x61)
				_crow = 99;
		}
		int duration = put(id, id, true);
		setDeadline(id, t, duration);
		int x = screenX(id);
		if (-0x280 < x && x < 0x500 && _s->_nedPos.y < 0x154 && s.step == 1)
			_s->playSound(id);
	} else {
		put(id, id, true);
	}
}

// 0x435dd0: the big pumpkin's face for its hit count
void Scene1Enemies1::faceTick() {
	int16 want = -1;
	if (!bigArtShow())
		return;
	static const int16 faces[3][4] = {
		{ 0x1d7, 0x1b5, 0x1c6, 0x1c8 },
		{ 0x1d8, 0x1dc, 0x1de, 0x1e0 },
		{ 0x1d9, 0x1dd, 0x1df, 0x1e1 }
	};
	if (0 <= _hits && _hits < 3) {
		switch (_bigSlot) {
		case 0x125:
		case 0x126:
			want = faces[_hits][0];
			break;
		case 0x128:
			want = faces[_hits][1];
			break;
		case 0x129:
			want = faces[_hits][2];
			break;
		case 0x12a:
			want = faces[_hits][3];
			break;
		default:
			break;
		}
	}
	if (want != _face)
		_face = sw(want, _face);
	if (_face != -1) {
		sl(_face).step = sl(_bigSlot).step;
		put(_face, 0x125, true);
	}
}

// 0x435f60: a piece flying off the hit pumpkin (placed relative to 0x125)
void Scene1Enemies1::seedTick(uint32 t, int16 &slot, int dx, int amp) {
	if (slot == -1)
		return;
	Slot &s = sl(slot);
	if (t <= s.deadline) {
		putRel(slot, 0x125, true);
		return;
	}
	s.step++;
	if (s.sequenceLength <= s.step)
		s.step = 0;
	s.x += dx;
	s.y += 0x14;
	s.y -= (int)(kSeedSin * amp);
	if (ABS(s.x) > 0x140) {
		s.x = 0;
		s.y = 0;
		slot = sw(-1, slot);
		return;
	}
	int duration = putRel(slot, 0x125, true);
	setDeadline(slot, t, duration);
}

// 0x436100: the seed the big pumpkin spits (0x199)
void Scene1Enemies1::shotTick(uint32 t) {
	if (_shot == -1)
		return;
	Slot &s = sl(_shot);
	if (t <= s.deadline) {
		put(_shot, 0x125, true);
		_s->adj(_shot, _shotX, _shotY);
		return;
	}
	s.step++;
	if (s.sequenceLength <= s.step)
		s.step = 0;
	int duration = put(_shot, 0x125, true);
	if (_shotHold < t) {
		_shotY += _shotVy;
		if (_shotMax < _shotY) {
			_shotVy = 0;
			_shotY = _shotMax;
			_shotDrop = 0xc;
		}
		_shotX -= _shotDecay + _shotDrop;
		_shotDecay--;
		if (_shotDecay < 0)
			_shotDecay = 0;
		if (_shotX < _shotFloor) {
			_shotFloor -= 100;
			_shotDrop = 0;
			_shotVy = 0xc;
			_shotMax = 0x140;
			_shotDecay = 0xc;
		}
	}
	_s->adj(_shot, _shotX, _shotY);
	setDeadline(_shot, t, duration);
	int y = s.sprite ? s.sprite->getPosition().y : 0;
	if (0x1ea < y) {
		_shot = sw(-1, _shot);
		return;
	}
	int32 box[4], ned[4];
	spriteBox(_shot, _s->currentFrame(GraveyardRoom::kArea, _shot), 0, box);
	nedSpriteBox(ned);
	if (GraveyardRoom::intersect(box, ned) && _s->ned()->getState() != 0xf) {
		playNed(0xc, _s->_riseLeft, _s->_rise);
		_shotHold = t + 500;
	}
}

// 0x4364d0: the big pumpkin
void Scene1Enemies1::bigTick(uint32 t) {
	if (_bigSlot == -1)
		return;
	const Common::Point &n = _s->_nedPos;
	if (_bigTimer < t) {
		_bigTimer = 0xffffffff;
		if (_bigSlot == 0x126 || _bigSlot == 0x125) {
			int16 next = -1;
			if (GraveyardRoom::inRect(_biteRect, n.x, n.y))
				next = 0x129;
			else if (GraveyardRoom::inRect(_spitRect, n.x, n.y))
				next = 0x127;
			if (next != -1) {
				_s->targetRemove(GraveyardRoom::kArea, _bigSlot);
				_bigSlot = _s->switchSlotPre(next, _bigSlot);
				_s->targetAdd(GraveyardRoom::kArea, _bigSlot, true);
			}
		}
	}
	if (t <= sl(_bigSlot).deadline) {
		put(_bigSlot, 0x125, true);
		faceTick();
	} else {
		sl(_bigSlot).step++;
		if (_bigSlot == 0x127 && sl(0x127).step == 4) {
			_shot = sw(0x199, _shot);
			put(_shot, 0x125, true);
			_shotFloor = -390;
			_shotMax = 0x96;
			_shotX = 0;
			_shotY = 0;
			_shotHold = 0;
			_shotDrop = 0;
			_shotVy = 0x18;
			_shotDecay = 0;
		}
		Slot &c = sl(_bigSlot);
		if (c.sequenceLength <= c.step) {
			switch (_bigSlot) {
			case 0x125:
				if (bigArtShow()) {
					_s->targetRemove(GraveyardRoom::kArea, _bigSlot);
					_bigSlot = sw(0x126, _bigSlot);
					_s->targetAdd(GraveyardRoom::kArea, _bigSlot, true);
				} else {
					sl(_bigSlot).step = 0;
				}
				break;
			case 0x126:
				c.step = 0;
				break;
			case 0x127:
				_s->targetRemove(GraveyardRoom::kArea, _bigSlot);
				_bigSlot = sw(0x126, _bigSlot);
				_bigTimer = now();
				_bigTimer += 2000 + (int)_s->game()->crtRandom() % 2000;
				_s->targetAdd(GraveyardRoom::kArea, _bigSlot, true);
				break;
			case 0x128:
			case 0x12a:
				_s->targetRemove(GraveyardRoom::kArea, _bigSlot);
				_bigSlot = sw(0x126, _bigSlot);
				_s->targetAdd(GraveyardRoom::kArea, _bigSlot, true);
				break;
			case 0x129:
				_s->targetRemove(GraveyardRoom::kArea, _bigSlot);
				_bigSlot = sw(0x126, _bigSlot);
				_bigTimer = now();
				_bigTimer += 1000 + (int)_s->game()->crtRandom() % 2000;
				_s->targetAdd(GraveyardRoom::kArea, _bigSlot, true);
				break;
			case 299:
				c.step = c.sequenceLength - 1;
				c.deadline = 0;
				break;
			default:
				break;
			}
		}
		int duration = put(_bigSlot, 0x125, true);
		faceTick();
		int x = screenX(_bigSlot);
		if (-0x280 < x && x < 0x500 && n.y < 0x154 && sl(_bigSlot).step == 0 &&
				(_bigSlot == 0x127 || _bigSlot == 0x126))
			_s->playSound(_bigSlot);
		if (_bigSlot == 0x129 && sl(0x129).step == 3)
			duration = (int)_s->game()->crtRandom() % 0x1e + 6;
		setDeadline(_bigSlot, t, duration);
	}
	if (_bigSlot != 0x129)
		return;
	// The bite: Ned is spat out at the left of region 6000
	int32 box[4], ned[4];
	spriteBox(0x129, _s->currentFrame(GraveyardRoom::kArea, 0x129), 1, box);
	nedSpriteBox(ned);
	if (!GraveyardRoom::intersect(box, ned) || _s->ned()->getCurSlot() == 0x107)
		return;
	_s->_nedPos.x = _biteRect[0] + 10;
	_s->_nedPos.y = _biteRect[1];
	while (_s->_nedPos.y <= _biteRect[3]) {
		if (_s->solidAt(kMapPumpkin, (int16)_s->_nedPos.x, (int16)_s->_nedPos.y))
			break;
		_s->_nedPos.y++;
	}
	playNed(0, _s->_riseLeft, _s->_rise);
}

void Scene1Enemies1::pairRegion(int16 slot, int ref, bool noMirror, int frame, int32 region) {
	if (slot == -1)
		return;
	int32 rect[4];
	int pos[2];
	_s->boxRect(GraveyardRoom::kArea, slot, frame, noMirror ? 0 : 3, rect);
	_s->attachPos(slot, ref, pos, noMirror);
	rect[0] += pos[0];
	rect[2] += pos[0];
	rect[1] += pos[1];
	rect[3] += pos[1];
	GraveyardRoom::regionSetRect(&_s->userMap(), region, rect);
}

// 0x436c80: a small pumpkin's first slot (0x51, 0x55, 0x59, 0x5d) or its explosion
void Scene1Enemies1::pairATick(uint32 t, int16 &slot, int ref, int32 &region) {
	if (slot == -1)
		return;
	bool noMirror = sl(slot).flip == nullptr;
	int frame = 0;
	if (t <= sl(slot).deadline) {
		put(slot, ref, noMirror, &frame);
		if (region != 0)
			pairRegion(slot, ref, noMirror, frame, region);
		return;
	}
	sl(slot).step++;
	Slot &s = sl(slot);
	if (s.step < s.sequenceLength) {
		int duration = put(slot, ref, noMirror, &frame);
		setDeadline(slot, t, duration);
		int x = screenX(slot);
		if (-0x280 < x && x < 0x500 && _s->_nedPos.y < 0x154) {
			int16 step = sl(slot).step;
			if ((slot == 0x51 && step == 0 && _nearA == 0x51) ||
					(slot == 0x55 && step == 6 && _nearA == 0x55) ||
					(slot == 0x59 && step == 7 && _nearA == 0x59) ||
					(slot == 0x5d && step == 9 && _nearA == 0x5d))
				_s->playSound(slot);
		}
	} else {
		switch (slot) {
		case 0x51:
		case 0x55:
		case 0x59:
		case 0x5d:
			// It holds; its partner (slot + 1) starts
			s.step = 0;
			sl(slot).deadline = 0x7fffffff;
			sl(slot + 1).step = -1;
			sl(slot + 1).deadline = 0;
			break;
		case 0x122:
		case 0x1be:
			if (s.sprite)
				s.sprite->setVisibility(false);
			slot = -1;
			break;
		default:
			break;
		}
		if (slot != -1)
			put(slot, ref, noMirror, &frame);
	}
	if (region != 0)
		pairRegion(slot, ref, noMirror, frame, region);
}

// 0x4370f0: the second slot (0x52, 0x56, 0x5a, 0x5e) or its explosion
void Scene1Enemies1::pairBTick(uint32 t, int16 &slot, int ref, int32 &region) {
	if (slot == -1)
		return;
	bool noMirror = sl(slot).flip == nullptr;
	int frame = 0;
	if (t <= sl(slot).deadline) {
		put(slot, ref, noMirror, &frame);
		if (region != 0)
			pairRegion(slot, ref, noMirror, frame, region);
		return;
	}
	sl(slot).step++;
	Slot &s = sl(slot);
	if (s.step < s.sequenceLength) {
		int duration = put(slot, ref, noMirror, &frame);
		setDeadline(slot, t, duration);
		int x = screenX(slot);
		if (-0x280 < x && x < 0x500 && _s->_nedPos.y < 0x154 && sl(slot).step == 0 && slot == _nearB)
			_s->playSound(slot);
	} else {
		uint32 *timer = nullptr;
		switch (slot) {
		case 0x52:
			timer = &_t1;
			break;
		case 0x56:
			timer = &_t2;
			break;
		case 0x5a:
			timer = &_t3;
			break;
		case 0x5e:
			timer = &_t4;
			break;
		case 0x122:
		case 0x1be:
			if (s.sprite)
				s.sprite->setVisibility(false);
			slot = -1;
			break;
		default:
			break;
		}
		if (timer) {
			s.step = 0;
			s.deadline = 0x7fffffff;
			*timer = t + ((int)_s->game()->crtRandom() % 3 + 2) * 1000;
		}
		if (slot != -1 && slot != 0x122)
			put(slot, ref, noMirror, &frame);
	}
	if (region != 0)
		pairRegion(slot, ref, noMirror, frame, region);
}

// 0x4374f0: which small pumpkin is nearest Ned (its sounds play)
void Scene1Enemies1::nearest() {
	const Common::Point &n = _s->_nedPos;
	int best = 0x7fff;
	struct Pair {
		int16 a, b;
		int ids[2];
	};
	const Pair pairs[4] = {
		{ _p1a, _p1b, { 0x51, 0x52 } },
		{ _p2a, _p2b, { 0x55, 0x56 } },
		{ _p3a, _p3b, { 0x59, 0x5a } },
		{ _p4a, _p4b, { 0x5d, 0x5e } }
	};
	for (int i = 0; i < 4; i++) {
		const Pair &p = pairs[i];
		if (p.a == -1 || p.b == -1)
			continue;
		int da = distance(sl(p.a).x, sl(p.a).y, n.x, n.y);
		int db = distance(sl(p.b).x, sl(p.b).y, n.x, n.y);
		if (i == 0) {
			best = MIN(da, db);
			_nearA = 0x51;
			_nearB = 0x52;
			continue;
		}
		if (da < best) {
			_nearA = p.ids[0];
			_nearB = p.ids[1];
			best = da;
		}
		if (db < best) {
			_nearA = p.ids[0];
			_nearB = p.ids[1];
			best = db;
		}
	}
}

// 0x4377a0
void Scene1Enemies1::pumpkinUpdate(uint32 t) {
	nearest();
	const Common::Point &n = _s->_nedPos;
	if (!GraveyardRoom::inRect(_biteRect, n.x, n.y) && !GraveyardRoom::inRect(_spitRect, n.x, n.y)) {
		_bigTimer = 0xffffffff;
	} else if (_bigTimer == 0xffffffff) {
		_bigTimer = now();
		_bigTimer += 2000 + (int)_s->game()->crtRandom() % 2000;
	}
	if (pair1Show()) {
		if (_t1 < t) {
			sl(0x51).deadline = 0;
			sl(0x51).step = -1;
			_t1 = 0x7fffffff;
		}
		pairATick(t, _p1a, 0x51, _r1a);
		pairBTick(t, _p1b, 0x52, _r1b);
	}
	if (pair2Show()) {
		if (_t2 < t) {
			sl(0x55).deadline = 0;
			sl(0x55).step = -1;
			_t2 = 0x7fffffff;
		}
		pairATick(t, _p2a, 0x55, _r2a);
		pairBTick(t, _p2b, 0x56, _r2b);
	}
	if (pair3Show()) {
		if (_t3 < t) {
			sl(0x59).deadline = 0;
			sl(0x59).step = -1;
			_t3 = 0x7fffffff;
		}
		pairATick(t, _p3a, 0x59, _r3a);
		pairBTick(t, _p3b, 0x5a, _r3b);
	}
	if (pair4Show()) {
		if (_t4 < t) {
			sl(0x5d).deadline = 0;
			sl(0x5d).step = -1;
			_t4 = 0x7fffffff;
		}
		pairATick(t, _p4a, 0x5d, _r4a);
		pairBTick(t, _p4b, 0x5e, _r4b);
	}
	if (_crowALoaded && !_crowAShown) {
		_crowAShown = 1;
		_s->attachList(0x4b54f4, 1);
		setZ(0x61, 10);
		sw(0x61, -1);
	}
	if (_crowAShown)
		crowTick(0x61, _crow == 0x61 ? t : 0);
	if (_crowBLoaded && !_crowBShown) {
		_crow = 99;
		_crowBShown = 1;
		_s->attachList(0x4b5614, 1);
		setZ(99, 10);
		sw(99, -1);
	}
	if (_crowBShown)
		crowTick(99, _crow == 99 ? t : 0);
	if (bigShow()) {
		bigTick(t);
		shotTick(t);
		seedTick(t, _seed1, 0x1e, _amp1);
		seedTick(t, _seed2, -0x1e, _amp2);
	}
	if (_expLoaded && !_expShown) {
		_s->ned()->listShow(8);
		_expShown = 1;
		applyFlip(0x1be);
		setZ(0x1be, 10);
		sl(0x1be).x += sl(0x1be).w;
		setZ(0x122, 10);
		_s->attachList(0x4b5440, 2);
	}
}

// 0x438580
int Scene1Enemies1::pumpkinRect(int16 found, int32 pos[2]) {
	int ref = found;
	bool noMirror;
	switch (found) {
	case 0x51:
	case 0x56:
	case 0x59:
	case 0x5d:
		noMirror = true;
		break;
	case 0x52:
	case 0x55:
	case 0x5a:
	case 0x5e:
		noMirror = false;
		break;
	case 0x129:
		ref = 0x125;
		noMirror = true;
		break;
	default:
		return 0;
	}
	int out[2];
	_s->attachPos(found, ref, out, noMirror);
	pos[0] = out[0];
	pos[1] = out[1];
	return 1;
}

// 0x4387a0 (no found slot: see the overload)
void Scene1Enemies1::pumpkinUse() {
}

// 0x4387a0: the use button at the big pumpkin makes it react (0x128)
void Scene1Enemies1::pumpkinUse(int16 found) {
	if ((found != 0x125 && found != 0x126) || _bigSlot == 0x128)
		return;
	_s->targetRemove(GraveyardRoom::kArea, _bigSlot);
	_bigSlot = sw(0x128, _bigSlot);
	int duration = put(_bigSlot, 0x125, true);
	setDeadline(_bigSlot, now(), duration);
	_s->targetAdd(GraveyardRoom::kArea, _bigSlot, true);
	faceTick();
}

void Scene1Enemies1::pairHit(int16 &slotA, int16 &slotB, int a, int b, bool aFirst, int32 &regionA,
		int32 &regionB) {
	_s->targetRemove(GraveyardRoom::kArea, slotA);
	_s->targetRemove(GraveyardRoom::kArea, slotB);
	_s->stopSound(a);
	_s->stopSound(b);
	int duration;
	if (aFirst) {
		slotA = sw(0x122, slotA);
		slotB = sw(0x1be, slotB);
		duration = put(0x122, a, true);
		setDeadline(0x122, now(), duration);
		duration = put(0x1be, b, false);
		setDeadline(0x1be, now(), duration);
	} else {
		slotA = sw(0x1be, slotA);
		slotB = sw(0x122, slotB);
		duration = put(0x1be, a, false);
		setDeadline(0x1be, now(), duration);
		duration = put(0x122, b, true);
		setDeadline(0x122, now(), duration);
	}
	_s->playSound(0x122);
	_s->regionRemove(regionA);
	regionA = 0;
	_s->regionRemove(regionB);
	regionB = 0;
}

// 0x437bf0: a zap reached a pumpkin
int Scene1Enemies1::pumpkinHit(int x, int y, int16 *a, int16 *b, const int32 rect[4]) {
	if (!_expLoaded)
		return 0;
	switch (*b) {
	case 0x51:
	case 0x52:
		pairHit(_p1a, _p1b, 0x51, 0x52, true, _r1a, _r1b);
		return 1;
	case 0x55:
	case 0x56:
		pairHit(_p2a, _p2b, 0x55, 0x56, false, _r2a, _r2b);
		return 1;
	case 0x59:
	case 0x5a:
		pairHit(_p3a, _p3b, 0x59, 0x5a, true, _r3a, _r3b);
		return 1;
	case 0x5d:
	case 0x5e:
		pairHit(_p4a, _p4b, 0x5d, 0x5e, true, _r4a, _r4b);
		return 1;
	case 0x126: {
		_s->targetRemove(GraveyardRoom::kArea, _bigSlot);
		_bigSlot = sw(0x128, _bigSlot);
		int duration = put(_bigSlot, 0x125, true);
		faceTick();
		setDeadline(_bigSlot, now(), duration);
		_s->targetAdd(GraveyardRoom::kArea, _bigSlot, true);
		return 1;
	}
	case 0x129: {
		if (_bigSlot != 0x129 || sl(0x129).step != 3)
			return 1;
		_hits++;
		_s->targetRemove(GraveyardRoom::kArea, 0x129);
		if (3 < _hits) {
			_bigSlot = sw(299, _bigSlot);
			_s->playSound(299);
			_s->playSound(0x12a);
		} else {
			_bigSlot = sw(0x12a, _bigSlot);
			_s->targetAdd(GraveyardRoom::kArea, _bigSlot, true);
			_s->playSound(0x12a);
			_seed1 = _s->switchSlotPre(0x1c9, _seed1);
			_seed2 = _s->switchSlotPre(0x1e2, _seed2);
			_amp1 = (int)_s->game()->crtRandom() % 0x14 + 5;
			_amp2 = (int)_s->game()->crtRandom() % 0x14 + 5;
			seedTick(0xffffffff, _seed1, 0x1e, _amp1);
			seedTick(0xffffffff, _seed2, -0x1e, _amp2);
		}
		int duration = put(_bigSlot, 0x125, true);
		faceTick();
		setDeadline(_bigSlot, now(), duration);
		return 1;
	}
	default:
		return 0;
	}
}

// 0x437b90: the bite and spit regions are claimed (and nothing happens)
int Scene1Enemies1::pumpkinRegion(int32 id, uint32 flags, int x, int y, int16 *state) {
	if (id == 6000 || id == 0x1771)
		return 1;
	return 0;
}

// ==== The Mourners (record 5) ====

// 0x43b440
void Scene1Enemies1::mournersShow() {
	int z = _s->ned()->getZ();
	drawOrder(0x4c, z + 1);
	drawOrder(0x120, z + 1);
	_bellX = sl(0x120).x;
	_bellY = sl(0x120).y;
	drawOrder(0x136, z - 1);
	drawOrder(0x137, z - 1);
	drawOrder(0x11b, z - 1);
	drawOrder(0x46, z - 2);
	drawOrder(0x47, z - 1);
	drawOrder(0x48, z - 1);
	drawOrder(0x49, z - 1);
	drawOrder(0x4a, z - 1);
	drawOrder(0x4b, z - 1);
	drawOrder(0x1b8, z - 1);
	applyFlip(0x1b8);
	drawOrder(0x1ba, z - 2);
	applyFlip(0x1ba);
	drawOrder(0x1bb, z - 1);
	applyFlip(0x1bb);
	drawOrder(0x1b9, z - 1);
	drawOrder(0x1bc, z - 1);
	drawOrder(0x1bd, z - 1);
	applyFlip(0x4b);
	mournersStart();
}

// 0x43a5a0
void Scene1Enemies1::mournersStart() {
	bool leftNoMirror = sl(0x11b).flip == nullptr;
	bool bellNoMirror = sl(0x120).flip == nullptr;
	bool rightNoMirror = sl(0x1b8).flip == nullptr;
	_caught = 0;
	_s->_bellSlot = -1;
	_leftIdle = -1;
	_left = -1;
	_rightIdle = -1;
	_right = -1;
	_unk84c = 0;
	_rightHit = 0;
	_leftHit = 0;
	if (_region != 0)
		_s->regionRemove(_region);
	_region = 0;
	_s->_grabbed = 0;
	_rightX = (int16)sl(0x1b8).x;
	_leftX = (int16)sl(0x11b).x;
	_mirror = 1;
	if ((int16)sl(0x1b8).x < _s->_nedPos.x) {
		_mirror = 0;
		applyFlip(0x48);
		applyFlip(0x49);
		applyFlip(0x1b9);
		applyFlip(0x1bc);
	}
	_rightCount = 0;
	_leftCount = 0;
	_s->_keeperTime = 0;
	CollisionMap *exits = _s->map(Scene1::kMapExits);
	GraveyardRoom::regionEnable(exits, 0xce4, true);
	GraveyardRoom::regionEnable(exits, 0xce5, true);
	_s->_bellSlot = sw(0x120, -1);
	put(0x120, 0x120, bellNoMirror);
	loopSound(0x120);
	_leftIdle = sw(0x46, -1);
	put(0x46, 0x11b, leftNoMirror);
	_left = sw(0x11b, -1);
	put(0x11b, 0x11b, leftNoMirror);
	_rightIdle = sw(0x1ba, -1);
	put(0x1ba, 0x1ba, rightNoMirror);
	_right = sw(0x1b8, -1);
	put(0x1b8, 0x1b8, rightNoMirror);
}

// 0x4388a0
void Scene1Enemies1::mournersHide() {
	_s->_bellSlot = -1;
	_leftIdle = -1;
	_left = -1;
	_rightIdle = -1;
	_right = -1;
	_caught = 0;
	_unk84c = 0;
	_rightHit = 0;
	_leftHit = 0;
	CollisionMap *exits = _s->map(Scene1::kMapExits);
	GraveyardRoom::regionEnable(exits, 0xce4, false);
	GraveyardRoom::regionEnable(exits, 0xce5, false);
	sl(0x11b).x = _leftX;
	sl(0x1b8).x = _rightX;
	if (_region != 0) {
		_s->regionRemove(_region);
		_region = 0;
	}
	if (_mirror == 0) {
		applyFlip(0x48);
		applyFlip(0x49);
		applyFlip(0x1b9);
		applyFlip(0x1bc);
	}
	applyFlip(0x1b8);
	applyFlip(0x1ba);
	applyFlip(0x1bb);
	applyFlip(0x4b);
}

// 0x438c20: the bell (0x120; 0x4c rings, 0x4b brings the mourners back)
void Scene1Enemies1::bellTick(uint32 t) {
	int16 &bell = _s->_bellSlot;
	bool noMirror = sl(0x120).flip == nullptr;
	if (bell <= 0)
		return;
	if (bell == 0x4b)
		noMirror = false;
	if (t <= sl(bell).deadline) {
		put(bell, 0x120, noMirror);
		return;
	}
	if (bell == 0x4b && sl(0x4b).step == 3) {
		_left = sw(-1, _left);
		_right = sw(-1, _right);
	}
	sl(bell).step++;
	if (bell == 0x4c && sl(0x4c).step == 1)
		_s->playSound(0x4c);
	Slot &b = sl(bell);
	if (b.sequenceLength <= b.step) {
		b.step = 0;
		if (bell == 0x4b) {
			_s->ned()->enable(true);
			if (_region != 0)
				_s->regionRemove(_region);
			_region = 0;
			bell = sw(0x120, bell);
			applyFlip(0x11b);
			_left = sw(0x47, _left);
			put(_left, 0x11b, false);
			_right = sw(0x1bb, _right);
			// (sic) the original places the left mourner's slot again, against 0x1b8
			put(_left, 0x1b8, true);
			GraveyardRoom::regionEnable(_s->map(Scene1::kMapExits), 1999, false);
			noMirror = false;
			_leftHit = 0;
			_rightHit = 0;
		} else if (bell == 0x4c) {
			bell = sw(0x120, bell);
			noMirror = false;
			_s->ned()->enable(true);
			_s->_nedPos.x = sl(0x120).x - 0x4a;
			int16 y = (int16)_s->_nedPos.y;
			while (y >= 0) {
				if (_s->solidAt(Scene1::kMapFloor, (int16)_s->_nedPos.x, y)) {
					_s->_nedPos.y = y;
					break;
				}
				y--;
			}
			_s->ned()->resetIdle();
		}
	}
	if (bell > 0) {
		int duration = put(bell, 0x120, noMirror);
		setDeadline(bell, t, duration);
	}
	if (_s->_keeperTime != 0 && _s->_keeperTime < t) {
		if (_region != 0)
			_s->regionRemove(_region);
		_region = 0;
		bell = sw(0x4b, bell);
		_s->playSound(bell);
		int duration = put(bell, 0x120, false);
		setDeadline(bell, t, duration);
		_s->_keeperTime = 0;
		sl(0x11b).x = _leftX;
		sl(0x1b8).x = _rightX;
	}
}

// 0x439050: walking into the mourners' regions
int Scene1Enemies1::mournersRegion(int32 id, uint32 flags, int x, int y, int16 *state) {
	if (id == _region) {
		// The pit the left mourner opened (0x4a): Ned is caught
		if (_s->_grabbed == 0) {
			sl(0x48).x = 0;
			sl(0x49).x = 0;
			sl(0x1b9).x = 0;
			sl(0x1bc).x = 0;
			sl(0x11b).x = _leftX;
			sl(0x1b8).x = _rightX;
			_caught = 1;
		}
		return 1;
	}
	if (id != 0xce4 && id != 0xce5) {
		if (id == 1999) {
			_s->_grabEnd = 0;
			GraveyardRoom::regionEnable(_s->map(Scene1::kMapExits), 1999, false);
		}
		return _s->_grabbed != 0;
	}
	// Ned comes near: both mourners rise
	_s->stopSound(0x120);
	loopSound(0x120);
	_s->targetRemoveSlot(_right);
	_right = sw(0x1bb, _right);
	_rightIdle = sw(-1, _rightIdle);
	_s->targetAddSlot(_right);
	int duration = put(_right, 0x1b8, false);
	setDeadline(_right, now(), duration);
	CollisionMap *exits = _s->map(Scene1::kMapExits);
	GraveyardRoom::regionEnable(exits, 0xce4, false);
	GraveyardRoom::regionEnable(exits, 0xce5, false);
	_s->targetRemoveSlot(_left);
	_left = sw(0x47, _left);
	_leftIdle = sw(-1, _leftIdle);
	_s->targetAddSlot(_left);
	duration = put(_left, 0x11b, _mirror != 0);
	setDeadline(_left, now(), duration);
	return 1;
}

// 0x4392f0: a zap reached a mourner
int Scene1Enemies1::mournersHit(int x, int y, int16 *a, int16 *b, const int32 rect[4]) {
	int16 found = *b;
	if (found == 0x1b9 || (0x1ba < found && found < 0x1bd)) {
		sl(0x1b8).x = _rightX;
		sl(0x136).x = 0;
		_s->targetRemoveSlot(_right);
		if (_rightHit == 0)
			_rightNext = 0x136;
		if (_leftHit != 0) {
			_rightNext = 0x137;
			_s->_keeperTime = now() + 9000;
		}
		_rightHit = 1;
		if (_mirror == 0 && _rightNext == 0x136)
			sl(0x1b8).x = _rightX - sl(0x136).w;
		_right = sw(_rightNext, _right);
		int id = 0x1b8;
		if (_right != 0x1b8) {
			sl(_right).x = _mirror == 0 ? 0 : -sl(0x1b8).w;
			id = _right;
		}
		int duration = putRel(id, 0x1b8, _mirror != 0);
		setDeadline(_right, now(), duration);
		return 1;
	}
	if (found < 0x47 || 0x49 < found)
		return 0;
	sl(0x11b).x = _leftX;
	sl(0x136).x = 0;
	_s->targetRemoveSlot(_left);
	if (_leftHit == 0)
		_leftNext = 0x136;
	if (_rightHit != 0) {
		_leftNext = 0x4a;
		_s->_keeperTime = now() + 9000;
	}
	_leftHit = 1;
	_left = sw(_leftNext, _left);
	int duration = put(_left, 0x11b, _mirror != 0);
	setDeadline(_left, now(), duration);
	return 1;
}

// 0x439590
int Scene1Enemies1::mournersRect(int16 found, int32 pos[2], int32 *noFlip) {
	int ref;
	bool flipped;
	*noFlip = _mirror;
	if (found == _right) {
		ref = 0x1b8;
		flipped = sl(0x1b8).flip != nullptr;
	} else if (found == _left) {
		ref = 0x11b;
		flipped = sl(0x11b).flip != nullptr;
	} else {
		return 0;
	}
	int out[2];
	_s->attachPos(found, ref, out, !flipped);
	pos[0] = out[0];
	pos[1] = out[1];
	return 1;
}

// 0x439600: the two waiting mourners' idle loops (0x46, 0x1ba)
void Scene1Enemies1::idleTick(uint32 t) {
	bool leftNoMirror = sl(0x46).flip == nullptr;
	bool rightNoMirror = sl(0x1ba).flip == nullptr;
	if (0 < _leftIdle) {
		Slot &s = sl(_leftIdle);
		if (s.deadline < t) {
			s.step++;
			if (s.sequenceLength <= s.step)
				s.step = 0;
			int duration = put(_leftIdle, 0x11b, leftNoMirror);
			setDeadline(_leftIdle, t, duration);
		} else {
			put(_leftIdle, 0x11b, leftNoMirror);
		}
	}
	if (0 < _rightIdle) {
		Slot &s = sl(_rightIdle);
		if (s.deadline < t) {
			s.step++;
			if (s.sequenceLength <= s.step)
				s.step = 0;
			int duration = put(_rightIdle, 0x1b8, rightNoMirror);
			setDeadline(_rightIdle, t, duration);
		} else {
			put(_rightIdle, 0x1b8, rightNoMirror);
		}
	}
}

// 0x4397e0: the two mourners
void Scene1Enemies1::mournTick(uint32 t) {
	int32 box[4], ned[4];
	if (0 < _left) {
		if (_left == 0x137)
			_left = sw(0x4a, 0x137);
		bool noMirror = _mirror != 0;
		if (_mirror == 0 && (_left == 0x11b || _left == 0x47 || _left == 0x136 || _left == 0x4a))
			noMirror = true;
		if (t <= sl(_left).deadline) {
			// 0x430710's relative flag is the time, but 0 for 0x11b and 0x47
			if (_left == 0x11b || _left == 0x47 || t == 0)
				put(_left, 0x11b, noMirror);
			else
				putRel(_left, 0x11b, noMirror);
		} else {
			if ((_left == 0x48 && sl(0x48).step == 4) || sl(0x48).step == 5) {
				// The left mourner's swing (0x48 steps 4, 5) hits Ned
				int frame = _s->currentFrame(GraveyardRoom::kArea, 0x48);
				spriteBox(0x48, frame, _mirror == 0 ? 4 : 1, box);
				_s->ned()->getBox(_s->ned()->getCurSlot(), _s->_nedPos, ned);
				int x0 = ned[0], y0 = ned[1], x1 = ned[2], y1 = ned[3];
				Scene1::xformWrap(_s, x0, y0);
				Scene1::xformWrap(_s, x1, y1);
				ned[0] = x0;
				ned[1] = y0;
				ned[2] = x1;
				ned[3] = y1;
				if (GraveyardRoom::intersect(ned, box) && 1 < ++_leftCount) {
					playNed(0x1d);
					_leftCount = 0;
				}
			}
			sl(_left).step++;
			if (_left == 0x4a && sl(0x4a).step == 2)
				_s->playSound(0x4a);
			if (_left == 0x136 && sl(0x136).step == 1)
				_s->playSound(0x4a);
			Slot &s = sl(_left);
			if (s.sequenceLength <= s.step) {
				if (_left == 0x4a)
					sl(0x4a).step = sl(0x4a).sequenceLength - 1;
				else
					s.step = 0;
				switch (_left) {
				case 0x47:
					sl(0x48).x = 0;
					sl(0x49).x = 0;
					sl(0x1b9).x = 0;
					sl(0x1bc).x = 0;
					if (_mirror == 0) {
						applyFlip(0x11b);
						applyFlip(0x1b8);
						_unk8e8 = 1;
						sl(0x1b8).x = _rightX;
						sl(0x11b).x = _leftX + 0xa0;
					}
					_s->targetRemoveSlot(_left);
					_left = sw(0x49, _left);
					_s->playSound(_left);
					_s->targetAddSlot(_left);
					break;
				case 0x48:
					_s->targetRemoveSlot(_left);
					_left = sw(0x49, _left);
					_s->playSound(_left);
					_s->targetAddSlot(_left);
					break;
				case 0x49:
					_s->targetRemoveSlot(_left);
					_left = sw(0x48, _left);
					_s->playSound(_left);
					_s->targetAddSlot(_left);
					break;
				case 0x136:
					_left = sw(-1, _left);
					break;
				default:
					break;
				}
			}
			if (0 < _left) {
				bool relative = _left != 0x11b && _left != 0x47;
				int frame = 0;
				int duration = relative ? putRel(_left, 0x11b, noMirror, &frame) : put(_left, 0x11b, noMirror, &frame);
				setDeadline(_left, t, duration);
				if (_left == 0x4a && sl(0x4a).step == sl(0x4a).sequenceLength - 1 && _region == 0) {
					// The pit opens where the mourner lies
					int32 rect[4];
					int pos[2];
					_s->boxRect(GraveyardRoom::kArea, 0x4a, frame, noMirror ? 0 : 3, rect);
					_s->attachPos(_left, 0x11b, pos, noMirror);
					rect[0] += pos[0];
					rect[2] += pos[0];
					rect[1] += pos[1];
					rect[3] += pos[1];
					_s->regionAdd(2, 0x4a, rect, &_region);
				}
			}
		}
	}
	if (_right <= 0)
		return;
	bool noMirror = _mirror != 0;
	if (_right == 0x1b8 || _right == 0x1bb)
		noMirror = false;
	if (_mirror == 0 && (_right == 0x137 || _right == 0x136))
		noMirror = true;
	if (t <= sl(_right).deadline) {
		if (_right == 0x1b8 || _right == 0x1bb) {
			put(_right, 0x1b8, noMirror);
			return;
		}
		bool relative = _right != 0x137 && !(_right == 0x136 && _mirror == 0);
		if (noMirror)
			sl(_right).x = -sl(0x1b8).w;
		if (relative)
			putRel(_right, 0x1b8, noMirror);
		else
			put(_right, 0x1b8, noMirror);
		sl(_right).x = 0;
		return;
	}
	if ((_right == 0x1b9 && sl(0x1b9).step == 4) || sl(0x1b9).step == 5) {
		// The right mourner's swing (0x1b9); (sic) the box frame is the current slot's step
		spriteBox(0x1b9, sl(_right).step, _mirror == 0 ? 4 : 1, box);
		_s->ned()->getBox(_s->ned()->getCurSlot(), _s->_nedPos, ned);
		int x0 = ned[0], y0 = ned[1], x1 = ned[2], y1 = ned[3];
		Scene1::xformWrap(_s, x0, y0);
		Scene1::xformWrap(_s, x1, y1);
		ned[0] = x0;
		ned[1] = y0;
		ned[2] = x1;
		ned[3] = y1;
		if (GraveyardRoom::intersect(ned, box) && 1 < ++_rightCount) {
			playNed(0x1d);
			_rightCount = 0;
		}
	}
	sl(_right).step++;
	if (_right == 0x137 && sl(0x137).step == 3)
		_s->playSound(0x137);
	if (_right == 0x136 && sl(0x136).step == 1)
		_s->playSound(0x1bd);
	Slot &s = sl(_right);
	if (s.sequenceLength <= s.step) {
		s.step = 0;
		switch (_right) {
		case 0x136:
			_right = sw(-1, _right);
			break;
		case 0x137:
			_right = sw(-1, _right);
			_left = 0x137;
			break;
		case 0x1b9:
			_s->targetRemoveSlot(_right);
			_right = sw(0x1bc, _right);
			_s->playSound(_right);
			_s->targetAddSlot(_right);
			break;
		case 0x1bb:
		case 0x1bc:
			_s->targetRemoveSlot(_right);
			_right = sw(0x1b9, _right);
			_s->playSound(_right);
			_s->targetAddSlot(_right);
			break;
		default:
			break;
		}
	}
	if (_right <= 0)
		return;
	int duration;
	if (_right == 0x1b8 || _right == 0x1bb) {
		duration = put(_right, 0x1b8, noMirror);
	} else {
		bool relative = _right != 0x137 && !(_right == 0x136 && _mirror == 0);
		if (noMirror)
			sl(_right).x = -sl(0x1b8).w;
		duration = relative ? putRel(_right, 0x1b8, noMirror) : put(_right, 0x1b8, noMirror);
		sl(_right).x = 0;
	}
	setDeadline(_right, t, duration);
}

// 0x43a900
void Scene1Enemies1::mournersTick(uint32 t) {
	if (_caught != 0) {
		playNed(0x1f);
		_caught = 0;
		_s->_grabbed = 1;
		_s->_keeperTime = 0;
		_s->_grabEnd = now() + 25000;
		if (_region != 0)
			_s->regionRemove(_region);
		_region = 0;
		GraveyardRoom::regionEnable(_s->map(Scene1::kMapExits), 1999, true);
	}
	if (0x22a < _s->_tile && _s->_tile < 0x28a)
		bellTick(t);
	idleTick(t);
	mournTick(t);
}

// 0x43a500
void Scene1Enemies1::mournersScroll(int dx, int dy) {
	if (0 < _s->_bellSlot)
		_s->adj(_s->_bellSlot, dx, dy);
	if (0 < _leftIdle)
		_s->adj(_leftIdle, dx, dy);
	if (0 < _left)
		_s->adj(_left, dx, dy);
	if (0 < _rightIdle)
		_s->adj(_rightIdle, dx, dy);
	if (0 < _right)
		_s->adj(_right, dx, dy);
}

// ==== The Coffin (record 7) ====

// 0x43a9c0
void Scene1Enemies1::coffinShow() {
	applyFlip(0xa1);
	applyFlip(0xa2);
	applyFlip(0xa3);
	_s->flipActive(GraveyardRoom::kArea, 0xa3, false);
	_s->flipActive(GraveyardRoom::kArea, 0xa2, false);
	_s->flipActive(GraveyardRoom::kArea, 0xa1, false);
	int z = _s->ned()->getZ();
	drawOrder(0xa1, z - 1);
	drawOrder(0xa2, z - 1);
	drawOrder(0xa3, z - 1);
	_coffinX = (int16)sl(0xa0).x;
	_s->_grabSlot = -1;
	CollisionMap *exits = _s->map(Scene1::kMapExits);
	GraveyardRoom::regionEnable(exits, 0x7d3, true);
	GraveyardRoom::regionEnable(exits, 0x7d2, true);
}

// 0x43ab50
void Scene1Enemies1::coffinHide() {
	_coffinTime = 0xffffffff;
	_s->_grabSlot = -1;
	_s->stopSound(0xa2);
}

// 0x43add0: Ned at the coffin (0x7d2 from one side, 0x7d3 from the other): the hands come out
int Scene1Enemies1::coffinRegion(int32 id, uint32 flags, int x, int y, int16 *state) {
	bool flip;
	if (id == 0x7d2) {
		flip = false;
		sl(0xa0).x = _coffinX;
		_s->_flipped = 1;
	} else if (id == 0x7d3) {
		flip = true;
		sl(0xa0).x = sl(0xa0).w + _coffinX;
		_s->_flipped = 0;
	} else {
		return 0;
	}
	_s->flipActive(GraveyardRoom::kArea, 0xa1, flip);
	_s->flipActive(GraveyardRoom::kArea, 0xa2, flip);
	_s->flipActive(GraveyardRoom::kArea, 0xa3, flip);
	GraveyardRoom::regionEnable(_s->map(Scene1::kMapExits), id, false);
	if (_s->_grabSlot == -1) {
		_s->_inputFree = 0;
		_s->_grabSlot = sw(0xa1, -1);
		_s->targetAddSlot(_s->_grabSlot);
		int duration = put(_s->_grabSlot, 0xa0, _s->_flipped != 0);
		setDeadline(_s->_grabSlot, now(), duration);
		_coffinTime = now() + 6000;
	}
	return 1;
}

// 0x43ab80
void Scene1Enemies1::coffinTick(uint32 t) {
	int16 &grab = _s->_grabSlot;
	// (sic) this tile test can never be true
	if (_s->_tile < 0x3f4 && 0x448 < _s->_tile)
		return;
	if (grab == -1)
		return;
	if (grab == 0xe) {
		// The hands were zapped: the puff plays out
		_s->puffTick(t, Scene1::xformPlain, _s);
		return;
	}
	Slot &s = sl(grab);
	if (t <= s.deadline) {
		put(grab, 0xa0, _s->_flipped != 0);
		return;
	}
	s.step++;
	if (s.sequenceLength <= s.step) {
		_s->targetRemoveSlot(grab);
		if (grab == 0xa1) {
			grab = sw(0xa2, 0xa1);
			_s->playSound(grab);
			_s->targetAddSlot(grab);
		} else if (grab == 0xa2) {
			grab = sw(0xa3, 0xa2);
			_s->targetAddSlot(grab);
		} else if (grab == 0xa3) {
			grab = sw(-1, 0xa3);
			_s->_inputFree = 1;
			return;
		}
	}
	if (grab != -1) {
		int duration = put(grab, 0xa0, _s->_flipped != 0);
		setDeadline(grab, t, duration);
	}
	if (grab == 0xa2 && sl(0xa2).step == 3)
		playNed(0x16);
}

// 0x43ad90
void Scene1Enemies1::coffinScroll(int dx, int dy) {
	if (_s->_grabSlot == -1)
		return;
	if (_s->_grabSlot == 0xe) {
		_s->puffScroll(dx, dy);
		return;
	}
	_s->adj(_s->_grabSlot, dx, dy);
}

} // End of namespace NightmareNed
