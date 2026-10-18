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
#include "common/ptr.h"
#include "common/system.h"
#include "common/textconsole.h"

#include "nightmarened/attic.h"
#include "nightmarened/detection.h"
#include "nightmarened/display.h"
#include "nightmarened/game.h"
#include "nightmarened/media.h"
#include "nightmarened/ned.h"
#include "nightmarened/nightmarened.h"
#include "nightmarened/rdx.h"

namespace NightmareNed {

AtticScene::AtticScene(Game *game) : Scene(game, 4) {
	_room = -1;
	clearState();
	// The room table is .data: read once, then changed at run time like the original
	const ExeImage &exe = _game->exe();
	for (int r = 0; r < kRoomCount; r++) {
		uint32 va = kRoomTable + r * kRoomSize;
		Room &room = _rooms[r];
		room.stages = exe.readSint32(va);
		room.polled = exe.readSint32(va + 4);
		for (int i = 0; i < 4; i++) {
			room.exits[i] = exe.readSint32(va + 0x08 + 4 * i);
			room.rect[i] = exe.readSint32(va + 0x18 + 4 * i);
			room.view[i] = exe.readSint32(va + 0x28 + 4 * i);
		}
		for (int i = 0; i < 6; i++)
			room.fn[i] = exe.readUint32(va + 0x38 + 4 * i);
		room.flags = exe.readUint16(va + 0x50);
	}
	_hooks.user = this;
	_hooks.use = hookUse;
	_hooks.region = hookRegion;
	_hooks.findTarget = hookFindTarget;
	_hooks.hitTarget = hookHitTarget;
	memset(_palette, 0, sizeof(_palette));
	memset(_paletteDim, 0, sizeof(_paletteDim));
	memset(_saveBuffer, 0, sizeof(_saveBuffer));
}

AtticScene::~AtticScene() {
	freeAll();
}

// ---- Resources ----

Slot &AtticScene::slot(int id) {
	return _game->res().getSlot(kArea, id);
}

void AtticScene::visible(int id, bool on) {
	if (id >= 0 && id < kAreaSlots && slot(id).sprite)
		slot(id).sprite->setVisibility(on);
}

BitmapRec *AtticScene::bmp(uint32 va) {
	return _bitmaps.contains(va) ? _bitmaps[va] : nullptr;
}

void AtticScene::bmpVisible(uint32 va, bool on) {
	BitmapRec *rec = bmp(va);
	if (rec && rec->object)
		rec->object->setVisibility(on);
}

// objSetPosition(rec[6], scroll + rec position)
void AtticScene::bmpPlace(uint32 va) {
	BitmapRec *rec = bmp(va);
	if (rec && rec->object)
		rec->object->setPosition(_scrollX + rec->x, rec->y + _scrollY);
}

void AtticScene::bmpSet(uint32 va, int x, int y) {
	BitmapRec *rec = bmp(va);
	if (rec) {
		rec->x = x;
		rec->y = y;
	}
}

CollisionMap *AtticScene::map(uint32 va) {
	return _maps.contains(va) ? _maps[va] : nullptr;
}

// 0x4586e0 (synchronous): every entry loads now; completion callbacks run on the next room update
int AtticScene::submit(uint32 va, int count, int scene) {
	NafArchive *naf = _game->getNaf(kNafAttic);
	if (!naf)
		return 5;
	const ExeImage &exe = _game->exe();
	for (int i = 0; i < count; i++) {
		uint32 e = va + i * 0x24;
		if (_entries.contains(e))
			continue;
		Common::Array<Request> one = _game->readList(e, 1);
		Request &req = one[0];
		req.scene = scene;
		if (_game->res().load(naf, req)) {
			if (req.bitmap)
				_bitmaps[req.destVA] = req.bitmap;
			if (req.collision)
				_maps[req.destVA] = req.collision;
			if (req.hasPalette && req.paletteVA == kPaletteAttic)
				memcpy(_palette, req.palette, sizeof(_palette));
			_entries[e] = req;
		}
		uint32 cb = exe.readUint32(e + 0x1c);
		if (cb)
			_pendingCallbacks.push_back(cb);
	}
	return 0;
}

// 0x458dc0: frees backwards
void AtticScene::release(uint32 va, int count) {
	for (int i = count - 1; i >= 0; i--) {
		uint32 e = va + i * 0x24;
		if (!_entries.contains(e))
			continue;
		Common::Array<Request> one;
		one.push_back(_entries[e]);
		if (one[0].bitmap)
			_bitmaps.erase(one[0].destVA);
		if (one[0].collision)
			_maps.erase(one[0].destVA);
		_game->freeList(one);
		_entries.erase(e);
	}
}

// 0x459170
int AtticScene::resWait(uint32 va) {
	return _entries.contains(va) ? 0 : 5;
}

void AtticScene::runCallbacks() {
	while (!_pendingCallbacks.empty()) {
		uint32 cb = _pendingCallbacks.front();
		_pendingCallbacks.remove_at(0);
		listCallback(cb);
	}
}

void AtticScene::freeAll() {
	_pendingCallbacks.clear();
	Common::Array<uint32> keys;
	for (EntryMap::iterator it = _entries.begin(); it != _entries.end(); ++it)
		keys.push_back(it->_key);
	Common::sort(keys.begin(), keys.end());
	for (int i = (int)keys.size() - 1; i >= 0; i--)
		release(keys[i], 1);
}

// ---- Display helpers (area 3) ----

// 0x430890
int16 AtticScene::slotSwitch(int newSlot, int oldSlot) {
	return (int16)_game->display().slotSwitch(kArea, (int16)newSlot, (int16)oldSlot);
}

// 0x430930
int16 AtticScene::slotSwitchPre(int newSlot, int oldSlot) {
	return (int16)_game->display().slotSwitch(kArea, (int16)newSlot, (int16)oldSlot, true);
}

// 0x430850 with one of the attic's scroll xforms (0x404ef0 and its copies): returns the step's duration
int AtticScene::place(int id, int ref, bool noMirror, int *outFrame) {
	int frame = 0, duration = 0;
	if (id < 0 || id >= kAreaSlots || ref < 0 || ref >= kAreaSlots)
		return 0;
	_game->display().slotPlace(kArea, id, ref, &frame, &duration, noMirror, Common::Point(_scrollX, _scrollY));
	if (outFrame)
		*outFrame = frame;
	return duration;
}

void AtticScene::setDeadline(int id, uint32 deadline) {
	if (id >= 0 && id < kAreaSlots)
		slot(id).deadline = deadline;
}

void AtticScene::setStep(int id, int16 step) {
	if (id >= 0 && id < kAreaSlots)
		slot(id).step = step;
}

// 0x430a30
void AtticScene::nudge(int id, int dx, int dy) {
	if ((int16)id == -1 || id < 0 || id >= kAreaSlots)
		return;
	if (slot(id).sprite)
		slot(id).sprite->adjPosition(dx, dy);
}

// 0x4038f0
void AtticScene::showSlot(int id, int z, bool on) {
	Slot &s = slot(id);
	if (s.sprite) {
		s.sprite->setVisibility(on);
		s.sprite->setDestination(_game->display().getPrimary());
		s.sprite->setDrawOrder((int16)z);
	}
	s.z = (int16)z;
}

// 0x403960: the bitmap record DAT_004ddb88[(area * 15 + scene) * 100 + id]
void AtticScene::showBitmap(int area, int scene, int id, int z, bool on) {
	BitmapRec *rec = bmp(kBitmapTable + 4 * ((area * 15 + scene) * 100 + id));
	if (!rec)
		return;
	if (rec->object) {
		rec->object->setVisibility(on);
		rec->object->setDestination(_game->display().getPrimary());
		rec->object->setDrawOrder((int16)z);
	}
	rec->z = (int16)z;
}

// 0x430440 slot_attach_pos: where slot would be placed against ref's anchor
void AtticScene::attachPos(int id, int ref, int out[2], bool noMirror) {
	Slot &s = slot(id), &r = slot(ref);
	int frame = 0, duration = 0;
	if (s.sequence)
		s.sequence->getStep(s.step, frame, duration);
	int sax = 0, say = 0, rax = 0, ray = 0, fox = 0, foy = 0;
	if (s.boxes) {
		sax = s.boxes->anchorX;
		say = s.boxes->anchorY;
		if (frame >= 0 && frame < (int)s.boxes->frames.size()) {
			fox = s.boxes->frames[frame].offX;
			foy = s.boxes->frames[frame].offY;
		}
	}
	if (r.boxes) {
		rax = r.boxes->anchorX;
		ray = r.boxes->anchorY;
	}
	int16 d = (int16)(rax + fox - sax);
	out[0] = r.x + (noMirror ? -d : d - s.w);
	out[1] = r.y + (say - ray - foy);
}

// 0x486930 + 0x4860d0 on a slot's current step
void AtticScene::stepRect(int id, int k, int32 out[4], int *outFrame) {
	Slot &s = slot(id);
	int frame = 0, duration = 0;
	if (s.sequence)
		s.sequence->getStep(s.step, frame, duration);
	if (s.boxes)
		s.boxes->getRawRect(frame, k, out);
	else
		out[0] = out[1] = out[2] = out[3] = -0x7fff;
	if (outFrame)
		*outFrame = frame;
}

// ---- Sound ----

// 0x455a50 snd_play_at through the attic xform DAT_004c6140 (0x404ef0): pan and volume by screen position
void AtticScene::playAt(int id, int stop) {
	if (stop >= 0)
		_game->stopSlotSound(slot(stop));
	if (id < 0)
		return;
	Slot &s = slot(id);
	_game->stopSlotSound(s);
	int pan = 0, volume = 0;
	if (_soundXform) {
		int x = s.x + _scrollX - 0x140 + s.w / 2;
		int y = s.y + _scrollY - 0xf0 + s.h / 2;
		if (x < 1)
			pan = x > -0x3c1 ? x * 5 : -5000;
		else
			pan = x < 0x3c1 ? x * 5 : 5000;
		int d = MAX(ABS(x), ABS(y));
		volume = d < 0x3c1 ? d * -2 : -0x1d4c;
	}
	if (s.sound.size() < 18)
		return;
	uint32 pcm = 18 + READ_LE_UINT16(s.sound.data() + 16);
	if (pcm >= s.sound.size())
		return;
	_game->playVoice(s.voice, s.sound.data(), s.sound.data() + pcm, s.sound.size() - pcm, volume, pan, false);
}

// 0x455b70: snd_play_at with an end-of-sound callback (slot +0x60), checked each tick
void AtticScene::playAtNotify(int id, int stop, uint32 callback) {
	playAt(id, stop);
	_soundEndSlot = id;
	_soundEndCallback = callback;
}

// 0x455930
void AtticScene::stopSound(int id) {
	if (id >= 0 && id < kAreaSlots)
		_game->stopSlotSound(slot(id));
}

// 0x455880 snd_play
void AtticScene::playSound(int id, int stop) {
	_game->playSlotSound(slot(id), stop >= 0 ? &slot(stop) : nullptr);
}

void AtticScene::checkSoundEnd() {
	if (_soundEndSlot < 0 || _game->isVoicePlaying(slot(_soundEndSlot).voice))
		return;
	uint32 cb = _soundEndCallback;
	_soundEndSlot = -1;
	_soundEndCallback = 0;
	if (cb == 0x40ebb0)
		r7SoundEnd();
}

// ---- Collision ----

// 0x487f50 + 0x4872a0
void AtticScene::addMap(CollisionMap *m) {
	if (m)
		_world.addMap(m, m->bounds.left, m->bounds.top, m->bounds.right, m->bounds.bottom);
}

void AtticScene::removeMap(CollisionMap *m) {
	if (m)
		_world.removeMap(m);
}

// 0x403840
void AtticScene::addBaseMap(int i) {
	if (!_mapAdded[i]) {
		addMap(map(kBaseMaps + 4 * i));
		_mapAdded[i] = true;
	}
}

// 0x403800
void AtticScene::removeBaseMap(int i) {
	if (_mapAdded[i]) {
		removeMap(map(kBaseMaps + 4 * i));
		_mapAdded[i] = false;
	}
}

// 0x487600 bc_add_rect on the scene's own region map; returns the new region's id
int32 AtticScene::addRegion(const int32 rect[4], uint32 type, int32 id) {
	CollisionMap::Region r;
	r.type = type;
	r.id = id;
	r.rect.left = rect[0];
	r.rect.top = rect[1];
	r.rect.right = rect[2];
	r.rect.bottom = rect[3];
	_userMap.regions.push_back(r);
	return id;
}

// 0x4876d0 bc_del_rect, then 0x487400 world_set_user
void AtticScene::removeRegion(int32 &id) {
	for (uint i = 0; i < _userMap.regions.size(); i++) {
		if (_userMap.regions[i].id == id) {
			_userMap.regions.remove_at(i);
			break;
		}
	}
	_world.setUser(&_userMap);
	id = 0;
}

// 0x487780: moves a region and sets its type
void AtticScene::setRegion(int32 id, const int32 rect[4], uint32 type) {
	for (uint i = 0; i < _userMap.regions.size(); i++) {
		if (_userMap.regions[i].id == id) {
			Common::Rect &r = _userMap.regions[i].rect;
			r.left = rect[0];
			r.top = rect[1];
			r.right = rect[2];
			r.bottom = rect[3];
			_userMap.regions[i].type = type;
			return;
		}
	}
}

bool AtticScene::solidAt(uint32 mapVA, int x, int y) {
	CollisionMap *m = map(mapVA);
	return m && m->isSolid((int16)x, (int16)y);
}

// ---- Targets (DAT_00525924) ----

// 0x4309c0 olist_add_sprite
void AtticScene::targetAdd(int id, bool noFlip) {
	Placement p;
	memset(&p, 0, sizeof(p));
	p.area = kArea;
	p.slot = (int16)id;
	p.sub = 1;
	p.kind = 2;
	p.flip = !noFlip;
	_targets.push_back(p);
}

// 0x486f50 olist_remove (its index loop skips the entry after each removal)
void AtticScene::targetRemove(int id) {
	for (uint i = 0; i < _targets.size(); i++) {
		if (_targets[i].area == kArea && _targets[i].slot == (int16)id && _targets[i].sub == 1)
			_targets.remove_at(i);
	}
}

// ---- Ned helpers ----

Ned *AtticScene::ned() {
	return _game->ned();
}

int AtticScene::facing() {
	return ned()->getFacing();
}

void AtticScene::setFacing(int f) {
	ned()->setFacing(f);
}

void AtticScene::playAction(int action) {
	ned()->playAction(action, _nedPos, _scrollX, _scrollY);
}

// 0x4038b0: walk Ned to x with input bits, input and camera locked
void AtticScene::autoWalk(int input, int facingRight, int x, int lockCamera) {
	_autoFacing = facingRight;
	_autoTargetX = x;
	_autoInput = input;
	_autoWalk = 1;
	_inputLocked = 1;
	_cameraLocked = lockCamera;
}

// 0x4093b0: scales Ned's colours (11..41) by |f|
void AtticScene::dimPalette(byte *pal, double f) {
	if (f < 0.0)
		f = -f;
	for (int i = 11; i < 42; i++) {
		for (int c = 0; c < 3; c++)
			pal[i * 3 + c] = (byte)(int)(pal[i * 3 + c] * f);
	}
}

// 0x404ef0 / 0x4039e0 and their copies: world -> screen
void AtticScene::xform(void *user, int &x, int &y) {
	AtticScene *a = (AtticScene *)user;
	x += a->_scrollX;
	y += a->_scrollY;
}

// ---- Scene methods ----

// The slot variables and map flags 0x404090 (m0) and 0x403d60 (m1) reset
void AtticScene::clearState() {
	static int16 AtticScene::*const kSlots[] = {
		&AtticScene::_r2s4, &AtticScene::_r2sC9, &AtticScene::_r2s9, &AtticScene::_r2sCD, &AtticScene::_r2s14,
		&AtticScene::_r3s2D, &AtticScene::_r3sEB, &AtticScene::_r4sB3, &AtticScene::_r4s21, &AtticScene::_r4sBall,
		&AtticScene::_r4s24, &AtticScene::_r5s31, &AtticScene::_r5s86, &AtticScene::_r5s8A, &AtticScene::_r5s3B,
		&AtticScene::_r5sC2, &AtticScene::_r5s2F, &AtticScene::_r7s45, &AtticScene::_r7s46, &AtticScene::_r8sBF,
		&AtticScene::_r8sCh, &AtticScene::_r9s2B, &AtticScene::_r9s82, &AtticScene::_r10s1C, &AtticScene::_r10sA,
		&AtticScene::_r10s1D, &AtticScene::_r10sB, &AtticScene::_r12s32, &AtticScene::_r12s36, &AtticScene::_r12s95,
		&AtticScene::_r12s39, &AtticScene::_r13s47, &AtticScene::_r13sBall, &AtticScene::_r17s61, &AtticScene::_r17s59,
		&AtticScene::_r17s5C, &AtticScene::_r18s5E, &AtticScene::_r18s5D, &AtticScene::_r18s62, &AtticScene::_r19s67,
		&AtticScene::_r19s65, &AtticScene::_r19s6D, &AtticScene::_r19sAny, &AtticScene::_r19sEF, &AtticScene::_r21sB0,
		&AtticScene::_r21s208, &AtticScene::_r21s218, &AtticScene::_r22s183, &AtticScene::_r25s184
	};
	for (int16 AtticScene::*m : kSlots)
		this->*m = -1;
	_r4Index = -1;
	for (int i = 0; i < 9; i++)
		_mapAdded[i] = false;
	_r2MapB0Added = _r2MapB4Added = _r7MapAdded = 0;
	_r12MapAAdded = _r12MapBAdded = _r12MapCAdded = 0;
	_r18MapAdded = _r19MapAdded = _r22MapAAdded = _r22MapBAdded = 0;
	_r8Channel = _r2HitsC9 = _r2Hits4 = _r7Plays = _r5Done = _r5Hit = _r18Done = _r19Done = 0;
	_r2Open = _r21Done = _r10Done = _hint3Shown = _r7Shown = _r12Escaped = _hint5Shown = 0;
	_r19Climbed = _hint4Shown = _hint0Shown = 0;
}

// m0 0x404090: also allocates the 0x4c-byte save buffer
int AtticScene::init() {
	clearState();
	_hasSaveBuffer = true;
	return 0;
}

// m1 0x403d60
void AtticScene::shutdown() {
	clearState();
	_r5Hits31 = _r5Hits86 = _r5Hits8A = 0;
}

// m6 0x403ad0: frees the save buffer
void AtticScene::suspend() {
	_hasSaveBuffer = false;
}

// m9 0x403b00: the 19 values that outlive a visit
void AtticScene::saveState(int32 *out) {
	int32 v[19] = { _r4Index, _r8Channel, _r2HitsC9, _r2Hits4, _r7Plays, _r5Done, _r5Hit, _r18Done, _r19Done,
		_r2Open, _r21Done, _r10Done, _hint3Shown, _r7Shown, _r12Escaped, _hint5Shown, _r19Climbed, _hint4Shown,
		_hint0Shown };
	memcpy(_saveBuffer, v, sizeof(v));
	memcpy(out, _saveBuffer, sizeof(v));
}

// m10 0x403c30
void AtticScene::restoreState(const int32 *in, int size) {
	int32 *const v[19] = { &_r4Index, &_r8Channel, &_r2HitsC9, &_r2Hits4, &_r7Plays, &_r5Done, &_r5Hit, &_r18Done,
		&_r19Done, &_r2Open, &_r21Done, &_r10Done, &_hint3Shown, &_r7Shown, &_r12Escaped, &_hint5Shown,
		&_r19Climbed, &_hint4Shown, &_hint0Shown };
	bool ok = size == 0x4c && _hasSaveBuffer;
	for (int i = 0; i < 19; i++)
		*v[i] = ok ? in[i] : 0;
	if (!ok)
		_r4Index = -1;
}

// m2 0x403f60
int AtticScene::load(int fromScene, int fromSub) {
	// ned.ini [Ned Prefs] InitialAtticScene, 0 when absent; rooms without a start map fall back to room 2
	_initialRoom = 0;
	_startMap = _game->exe().readSint32(kStartMaps + _initialRoom * 4);
	if (_startMap == -1) {
		_initialRoom = 2;
		_startMap = 0;
	}
	_inputLocked = 0;
	_autoWalk = 0;
	_skipDraw = 0;
	_cameraLocked = 0;
	_cameraJump = 0;
	_viewRoomX = -1;
	_viewRoomY = -1;
	_pendingScene = -1;
	_r12Exit = 0;
	_toRoom25 = 0;
	_toRoom9 = 0;
	_r10Enter = 0;
	_r10Back = 0;
	if (!_game->getNaf(kNafAttic))
		return 5;
	ned()->listLoad(kNedList1);
	ned()->listLoad(kNedList2);
	submit(kBaseList, kBaseListCount, 0);
	if (fromScene == 0 || fromScene == 2)
		_game->media().loadClip(2);
	return 0;
}

// 0x404ef0 into the slot-sound pan (0x455a40)
int AtticScene::enter(int fromScene, int fromSub) {
	ned()->setFinishCallback(actionFinished, this);
	_soundXform = true;
	ned()->listShow(kNedList1);
	ned()->listShow(kNedList2);
	_world = CollisionWorld();         // 0x487250
	_userMap = CollisionMap();         // 0x4871e0
	_userMap.bounds = Common::Rect();

	// The collision maps of list 0x497680; 3 and 7 are the alternates for later states
	if (resWait(kBaseList))
		return 5;
	addBaseMap(0);
	if (resWait(kBaseList + 0x24))
		return 5;
	addBaseMap(1);
	if (resWait(kBaseList + 0x48))
		return 5;
	addBaseMap(2);
	if (_r5Done) {
		if (resWait(kBaseList + 0x6c))
			return 5;
		r5AddMap3(); // 0x40cba0
	}
	if (resWait(kBaseList + 0x90))
		return 5;
	addBaseMap(4);
	if (resWait(kBaseList + 0xb4))
		return 5;
	addBaseMap(5);
	if (!_r10Done) {
		if (resWait(kBaseList + 0xd8))
			return 5;
		addBaseMap(6);
		_r10Open = 0;
		_rooms[2].exits[0] = -1;
	} else {
		if (resWait(kBaseList + 0xfc))
			return 5;
		addBaseMap(7);
		_r10Open = 1;
		_rooms[2].exits[0] = 13;
	}
	_rooms[10].exits[1] = _rooms[2].exits[0];
	if (resWait(kBaseList + 0x120))
		return 5;
	addBaseMap(8);

	// The start room's view, and Ned on the floor below its top edge
	const Room &start = _rooms[_initialRoom];
	_scrollX = _initialRoom == 2 ? -0xd8d : -0x189 - start.view[0];
	_game->res().getSlot(1, 4).offsets[0] = 6;
	_game->res().getSlot(1, 0x126).offsets[0] = 3;
	_scrollY = -start.view[1];
	_nedPos.x = -0x48 - _scrollX;
	for (int16 y = (int16)start.view[3]; y >= start.view[1]; y--) {
		if (solidAt(kBaseMaps + 4 * _startMap, _nedPos.x, y)) {
			_nedPos.y = y;
			break;
		}
	}
	setFacing(1);
	ned()->resetIdle();
	for (uint32 e = kBitmapList; e < kBitmapListEnd; e += 0x24) {
		// 0x459100: z 3, hidden, on the primary surface
		if (_entries.contains(e) && _entries[e].bitmap && _entries[e].bitmap->object) {
			RdxBackground *obj = _entries[e].bitmap->object;
			obj->setDrawOrder(3);
			obj->setDestination(_game->display().getPrimary());
			obj->setVisibility(false);
		}
	}
	if (bmp(0x4df480) && bmp(0x4df480)->object)
		bmp(0x4df480)->object->setDrawOrder(6);

	// 0x45a560(1, 999, 1): 98999001.SST, applied by 0x404490
	_sst.clear();
	Common::String sstName = Common::String::format("%02d%03d0%02d.sst", (int16)_game->exe().readUint16(kSstAreas + 2), 999, 1);
	Common::ScopedPtr<Common::SeekableReadStream> sst(_game->getEngine()->openFile(sstName));
	if (!sst || !loadPlacements(*sst, _sst))
		warning("Can't read %s", sstName.c_str());
	applyPlacements();

	ned()->setWrap1(false, 0x1e58, xform, this);
	_targets.clear();
	roomsEnter(_initialRoom);
	if (fromScene == 0 || fromScene == 4 || fromScene == 2)
		_game->media().playClip(2, 1);
	static const uint32 kShown[] = { 0x4df484, 0x4df45c, 0x4df480, 0x4df47c, 0x4df460, 0x4df474, 0x4df470,
		0x4df46c, 0x4df468, 0x4df464 };
	for (uint32 va : kShown)
		bmpVisible(va, true);
	if (resWait(kBitmapList))
		return 5;
	ned()->copyPalette(_palette);
	_game->display().applyPalette(_palette, kPalGame);
	for (int n = 0; !(_rooms[_initialRoom].flags & kRoomActive); n++) {
		if (n == kWaitLimit || _game->getEngine()->shouldQuit()) {
			warning("Attic: start room %d never became active (flags %x)", _initialRoom, _rooms[_initialRoom].flags);
			break;
		}
		roomsUpdate(_game->now());
	}

	// Ned walks in from the left of room 2
	_autoTargetX = _rooms[2].view[0] + 500;
	_autoWalk = 1;
	_autoFacing = 1;
	_autoInput = kInputRight;
	_inputLocked = 1;
	_cameraLocked = 1;
	_game->media().playMusic(kAtticMusic);
	ned()->show(true);
	ned()->enable(true);
	_game->_optionsAllowed = true;
	int f = 0;
	ned()->tick(_world, 0, f, _nedPos, _game->now(), _scrollX, _scrollY, &_hooks);
	return 0;
}

// m4 0x404290
int AtticScene::leave(int toScene, int toSub) {
	// 0x455950 -> 0x44d020: every static (slot) voice stops; the streamed music plays on
	for (int a = 0; a < kAreaCount; a++) {
		for (int i = 0; i < kAreaSlots; i++)
			_game->stopSlotSound(_game->res().getSlot(a, i));
	}
	// 0x455ca0 flushes the loader queues; every request here is already loaded, so its
	// completion callback has run by then
	runCallbacks();
	_sst.clear();                  // 0x45a5f0
	ned()->setWrap1(false, 0, nullptr, nullptr);
	_soundXform = false;
	ned()->setFinishCallback(nullptr, nullptr);
	ned()->show(false);
	ned()->setCostume(1);
	ned()->listFree(kNedList1);
	ned()->listFree(kNedList2);
	roomsLeave();
	for (int i = 0; i < 9; i++)
		removeBaseMap(i);
	_world = CollisionWorld();
	_targets.clear();
	_userMap = CollisionMap();
	release(kBaseList, kBaseListCount);
	switch (toScene) {
	case 1:
		_game->media().loadClip(10);
		break;
	case 2:
		_game->media().loadClip(12);
		break;
	case 3:
		_game->media().loadClip(11);
		break;
	case 4:
		_game->media().loadClip(2);
		break;
	case 5:
		_game->media().loadClip(13);
		break;
	default:
		break;
	}
	return 0;
}

// m5 0x4043e0: the TV clip into the next scene
int AtticScene::unload(int toScene, int toSub) {
	if (toScene == 4) {
		_game->media().playClip(2, 1);
		return 0;
	}
	_game->closeNaf(kNafAttic);
	switch (toScene) {
	case 1:
		_game->media().playClip(10, 1);
		break;
	case 2:
		_game->media().playClip(12, 1);
		break;
	case 3:
		_game->media().playClip(11, 1);
		break;
	case 5:
		_game->media().playClip(13, 1);
		break;
	default:
		break;
	}
	return 0;
}

// 0x403a00: DAT_004c52b4, when Ned's scripted action ends
void AtticScene::actionFinished(void *user, int16 action) {
	AtticScene *a = (AtticScene *)user;
	if (action == 6)
		a->_r2NedHit = 0;
	else if (action == 0x35)
		a->ned()->enable(false);
	else if (action == 0x44)
		a->_r19NedHit = 0;
}

// m7 0x404f10
int AtticScene::tick(uint32 input, uint32 now) {
	int room = _room;
	if (now < _lastTick + kMsPerStep) {
		g_system->delayMillis(_lastTick + kMsPerStep - now);
		return 0;
	}
	_lastTick = now;
	checkSoundEnd();
	// Test hook: nightmarened_hint=<which>:<frames> calls the hint once that many frames have been drawn
	if (!_hintTestDone && ConfMan.hasKey("nightmarened_hint")) {
		int which = 0, frames = 0;
		if (sscanf(ConfMan.get("nightmarened_hint").c_str(), "%d:%d", &which, &frames) >= 1 &&
			_game->display()._drawCount >= (uint32)frames) {
			_hintTestDone = true;
			hint(which, facing() != 0);
		}
	}
	Common::Point pos = ned()->getSpritePosition(ned()->getCurSlot());

	// At the screen edges, a neighbour room that isn't shown yet stops Ned; so does Ctrl
	bool keep = true;
	if (input == kInputLeft) {
		int exit = _rooms[room].exits[2];
		keep = exit == -1 || (_rooms[exit].flags & kRoomActive) || pos.x > 0x23;
	} else if (input == kInputRight) {
		int exit = _rooms[room].exits[3];
		keep = exit == -1 || (_rooms[exit].flags & kRoomActive) || pos.x >= 0x25d;
	} else if (input == kInputButton3) {
		if (facing() == 0)
			keep = pos.x > 0xb1;
		else if (facing() == 1)
			keep = pos.x < 0x1cf;
	}
	if (!keep)
		input = 0;
	if (_inputLocked)
		input = 0;
	if (_autoWalk) {
		if ((_autoFacing && _nedPos.x < _autoTargetX) || (!_autoFacing && _autoTargetX < _nedPos.x)) {
			setFacing(_autoFacing);
			input = _autoInput;
		} else {
			_autoWalk = 0;
			_inputLocked = 0;
			_cameraLocked = 0;
			_toRoom25 = 0;
			_toRoom9 = 0;
			input = 0;
		}
	}
	roomsUpdate(now);
	int f = 0;
	ned()->tick(_world, input, f, _nedPos, now, _scrollX, _scrollY, &_hooks);
	camera(now);
	debugC(3, kDebugGraphics, "Attic: input %x room %d Ned %d,%d state %d scroll %d,%d", input, _room, _nedPos.x,
		_nedPos.y, ned()->getState(), _scrollX, _scrollY);
	if (!_skipDraw) {
		_game->display().setCollisionOverlay(overlayMaps(), Common::Point(_scrollX, _scrollY));
		_game->display().draw();
	} else {
		_skipDraw = 0;
	}
	if (_pendingScene != -1) {
		int scene = _pendingScene;
		_game->_optionsAllowed = false;
		if (!_game->hasScene(scene, -1)) {
			// Not ported yet: stay in the attic
			debugC(1, kDebugLoading, "Attic: door to scene %d (not ported), staying", scene);
			_pendingScene = -1;
			_game->_optionsAllowed = true;
		} else {
			_game->switchScene(scene, -1);
		}
	}
	return 0;
}

Common::Array<CollisionMap *> AtticScene::overlayMaps() const {
	Common::Array<CollisionMap *> maps;
	for (int i = 0; i < _world.getMapCount(); i++)
		maps.push_back(_world.getMap(i));
	maps.push_back(const_cast<CollisionMap *>(&_userMap));
	return maps;
}

// 0x404490: one .SST entry. Sprites (area 3) go to their slot, mirrored by +0x20, at the
// frame offset of their current step; bitmaps (kind 0/1) only take the position
int AtticScene::applyPlacement(const Placement &e, int ox, int oy) {
	if (e.kind == 2) {
		if (e.area != kArea || e.slot < 0 || e.slot >= kAreaSlots)
			return 1;
		Slot &s = slot(e.slot);
		s.x = e.x + ox;
		s.y = e.y + oy;
		if (e.flip)
			s.x += s.w;
		if (!s.sprite)
			return 1;
		int frame = 0, duration = 0;
		if (s.sequence)
			s.sequence->getStep(s.step, frame, duration);
		int fx = 0, fy = 0;
		if (s.boxes && frame >= 0 && frame < (int)s.boxes->frames.size()) {
			fx = s.boxes->frames[frame].offX;
			fy = s.boxes->frames[frame].offY;
		}
		s.sprite->setPosition(s.x - fx, s.y - fy);
		return 1;
	}
	if (e.kind == 0 || e.kind == 1) {
		int16 k = (int16)_game->exe().readUint16(kAreaIndex + 2 * e.area);
		if (e.area <= 0)
			return 1;
		BitmapRec *rec = bmp(kBitmapTable + 4 * ((k * 15 + e.slot) * 100 + e.sub));
		if (!rec)
			return 1;
		rec->x = e.x;
		rec->y = e.y;
		rec->z = e.z;
	}
	return 1;
}

// 0x404700: 0x4870a0(DAT_00525de4, 0x404490, {0, 0})
void AtticScene::applyPlacements() {
	for (const Placement &e : _sst)
		applyPlacement(e, 0, 0);
}

// ---- Ned hooks (DAT_00525980) ----

// 0x405100: the nearest target in front of Ned; its box 0 in world coordinates
int AtticScene::nearestTarget(int16 &dir, int16 &found, int32 rect[4]) {
	found = -1;
	int unused = 0;
	if (ned()->findTarget(facing(), ned()->getCurSlot(), _targets, dir, unused, found)) {
		Common::Point p = slot(found).sprite ? slot(found).sprite->getPosition() : Common::Point();
		stepRect(found, 0, rect);
		int x = p.x - _scrollX, y = p.y - _scrollY;
		rect[0] += x;
		rect[1] += y;
		rect[3] += y;
		rect[2] += x;
	}
	return 1;
}

// 0x406150: entering state 0, Ned aims at the nearest target
int AtticScene::hookUse(void *user, int x, int y, int16 *state) {
	AtticScene *a = (AtticScene *)user;
	int16 dir = 0x2f, found;
	int32 rect[4];
	a->nearestTarget(dir, found, rect);
	a->ned()->setUseSlot(dir);
	return 1;
}

// 0x407c10
void AtticScene::hookFindTarget(void *user, int x, int y, int16 *a, int16 *b, int32 rect[4]) {
	AtticScene *s = (AtticScene *)user;
	*a = 3;
	int16 dir;
	s->nearestTarget(dir, *b, rect);
}

int AtticScene::hookRegion(void *user, int32 id, uint32 flags, int x, int y, int16 *state) {
	return ((AtticScene *)user)->region(id, flags, x, y, state);
}

int AtticScene::hookHitTarget(void *user, int x, int y, int16 *a, int16 *b, const int32 rect[4]) {
	return ((AtticScene *)user)->hitTarget(*b);
}


// ---- 0x405200: the region hook. id is the collision record Ned walked into ----

int AtticScene::region(int32 id, uint32 flags, int x, int y, int16 *state) {
	debugC(2, kDebugGraphics, "Attic: region %x (flags %x) in room %d", id, flags, _room);
	int nedX = _nedPos.x;
	double dim;
	if (id == 0x409) {
		dim = 0.5;
	} else if (id == 0x3f0) {
		dim = 1.0 - (double)(nedX - 0x1183) * 0.005 * 0.5;
	} else if (id == 0x3f1) {
		dim = 1.0 - (double)(0x14a6 - nedX) * 0.005 * 0.5;
	} else {
		return region2(id, state);
	}
	memcpy(_paletteDim, _palette, sizeof(_paletteDim));
	dimPalette(_paletteDim, dim);
	_game->display().applyPalette(_paletteDim, kPalAll);
	_paletteDimmed = 1;
	return 0;
}

int AtticScene::region2(int32 id, int16 *state) {
	if (_paletteDimmed && (id == 0x407 || id == 0x408)) {
		_game->display().applyPalette(_palette, kPalAll);
		_paletteDimmed = 0;
		return 0;
	}
	if (id == 0x403) {
		if (!_r7Shown && _r7Plays > 0) {
			_viewRoomY = 6;
			_r6Look = 1;
			_inputLocked = 1;
			_r7Shown = 1;
		}
		return 0;
	}
	if (id == 0x3f2 || id == 0x3f3) {
		if (!_hint3Shown && _r8Channel) {
			_hint3Shown = 1;
			hint(3, id == 0x3f3);
		}
		return 0;
	}
	if (id == 0x401 || id == 0x402 || id == 0x406) {
		if (ned()->getCostume() != 8)
			return 0;
		ned()->setCostume(1);
		addBaseMap(8);
		ned()->setAction(0x18);
		if (id != 0x402 && (_rooms[22].flags & kRoomActive))
			bmpVisible(0x4e1560, false);
		_r19s6D = slotSwitch(0x6d, _r19s6D);
		int d = place(_r19s6D, 0x62, true);
		setDeadline(_r19s6D, _game->now() + d * kMsPerStep);
		_r19Time6D = 0;
		int32 rect[4] = { 0, 0, 0, 0 };
		_r19Region = addRegion(rect, 0x80000, 0xd2);
		_world.setUser(&_userMap);
		*state = 0xf;
		return 1;
	}
	if (id == 0x3ff && !_r23Drop) {
		_r23Drop = 1;
		return 0;
	}
	if (id == 0x400 && _r21Done) {
		_viewRoomX = 0x14;
		_viewRoomY = 0x14;
		_inputLocked = 1;
		_endPan = 1;
		return 0;
	}
	// The doors to the dream scenes
	if (id == 0x3ef) {
		_pendingScene = 2;
		return 0;
	}
	if (id == 0x3f8) {
		_pendingScene = 3;
		return 0;
	}
	if (id == 0x3fb) {
		_pendingScene = 5;
		return 0;
	}
	if (id == 0x3f7 || id == 0x3fc) {
		_r10Enter = 1;
		return 0;
	}
	if (id == 0x3fd) {
		_r12Exit = 1;
		return 0;
	}
	if (id == 0x3eb && _rooms[9].polled > 1) {
		if (_r9s82 != -1 && _r9s82 != 0x2a) {
			_r9s82 = slotSwitch(0x2a, _r9s82);
			int d = place(_r9s82, 0x82, true);
			setDeadline(_r9s82, _game->now() + d * kMsPerStep);
			playAt(0x2a);
		}
		return 0;
	}
	if (id == 1000) {
		if (!_r3Started && _r3sEB != -1) {
			_r3Started = 1;
			_r3sEB = slotSwitch(0xeb, _r3sEB);
			setDeadline(_r3sEB, 0);
			playAt(0xeb);
		}
		if (_r3s2D != 0x2e && _r3s2D != -1) {
			_r3s2D = slotSwitchPre(0x2e, _r3s2D);
			setStep(_r3s2D, 0);
			int d = place(_r3s2D, 0x2d, true);
			setDeadline(_r3s2D, _game->now() + d * kMsPerStep);
			playAt(0x2e);
		}
		return 0;
	}
	if (id == _r5Region31) {
		if (_r5s31 != 0x84 && _rooms[5].polled > 3) {
			targetRemove(_r5s31);
			_r5s31 = slotSwitch(0x84, _r5s31);
			int d = place(_r5s31, 0x31, true);
			setDeadline(_r5s31, _game->now() + d * kMsPerStep);
			playAt(0x84, 0x83);
		}
		*state = 0xe;
		return 1;
	}
	if (id == _r5Region86) {
		if (_r5s86 != 0x88 && _rooms[5].polled > 3) {
			targetRemove(_r5s86);
			_r5s86 = slotSwitch(0x88, _r5s86);
			int d = place(_r5s86, 0x86, true);
			setDeadline(_r5s86, _game->now() + d * kMsPerStep);
			playAt(0x88, 0x87);
		}
		*state = 0xe;
		return 1;
	}
	if (id == _r5Region8A) {
		if (_r5s8A != 0x8c && _rooms[5].polled > 3) {
			targetRemove(_r5s8A);
			_r5s8A = slotSwitch(0x8c, _r5s8A);
			int d = place(_r5s8A, 0x8a, true);
			setDeadline(_r5s8A, _game->now() + d * kMsPerStep);
			playAt(0x8c, 0x8b);
		}
		*state = 0xe;
		return 1;
	}
	if (id == _r12RegionA || id == _r12RegionB || id == _r12RegionC) {
		_r12Touched = 1;
		return 0;
	}
	if (id == 0x3ee && _rooms[8].polled > 2) {
		if (_r8State == 1 && _r8sCh == -1) {
			_r8Channel = (_r8Channel + 1) % 3;
			_inputLocked = 1;
			_r8sCh = slotSwitch(_r8Channel + 0x27, -1);
			int d = place(_r8sCh, _r8sCh, true);
			setDeadline(_r8sCh, _game->now() + d * kMsPerStep);
			playAt(_r8sCh);
		}
		return 0;
	}
	if (id == 0x3e9 || id == 0x3ea) {
		if (_game->now() < _r25Time)
			return 0;
		_r25Count++;
		if (_r25Count == 3) {
			_r25Count = 2;
			return 0;
		}
		_r25s = slotSwitch(0x41, _r25s);
		setStep(_r25s, (int16)_game->exe().readUint16(kR25Steps + _r25Count * 4));
		int d = place(_r25s, _r25s, true);
		setDeadline(_r25s, _game->now() + d * kMsPerStep);
		playAt(_r25s);
		_r25Time = _game->now() + 0x15e;
		return 0;
	}
	if (id == 0x3ed) {
		if (_rooms[25].polled <= 1)
			return regionTail(id, state);
		if (_game->now() < _r25Time)
			return 0;
		_r25Count++;
		if (_r25Count == 3) {
			_r25Count = 2;
			if (_rooms[25].polled == 3) {
				_r25s = slotSwitch(0x43, _r25s);
				place(_r25s, _r25s, true);
				setDeadline(_r25s, _game->now());
				playAt(_r25s);
				_r25Time = _game->now() + 10000;
				setFacing(1);
				ned()->setAction(0x37);
				*state = 0xf;
				return 1;
			}
		}
		_r25Time = _game->now() + 0x15e;
		return 0;
	}
	return regionTail(id, state);
}

int AtticScene::regionTail(int32 id, int16 *state) {
	if (id == _r19Region) {
		if (_r19s6D == 0x6e || _r19s6D == 0x6d) {
			_r19s6D = slotSwitch(0x6f, _r19s6D);
			setStep(_r19s6D, 0);
			int d = place(_r19s6D, 0x6d, true);
			setDeadline(_r19s6D, _game->now() + d * kMsPerStep);
			playAt(_r19s6D, 0x6e);
			_inputLocked = 1;
		}
		*state = 4;
		return 1;
	}
	if (id == 0x3f6) {
		if (!_r2Open || _r2s14 == 0x15)
			return 0;
		_r2s14 = slotSwitch(0x15, _r2s14);
		int d = place(_r2s14, 0x14, true);
		setDeadline(_r2s14, _game->now() + d * kMsPerStep);
		playAt(0x15);
		_inputLocked = 1;
		ned()->setAction(0x35);
		*state = 0xf;
		return 1;
	}
	if (id == 0x3f4) {
		_r22Spin = 1;
		_r22Teleport = 1;
		setFacing(0);
		ned()->setAction(0x41);
		playAt(0x183);
		*state = 0xf;
		return 1;
	}
	if (id == 0x3f5) {
		if (_rooms[4].polled <= 2)
			return regionLast(id, state);
		if (_r4Climbed || _r4s21 != 0x22)
			return 0;
		_inputLocked = 1;
		_r4Climbed = 1;
		_r4s24 = slotSwitch(0x24, _r4s24);
		setStep(_r4s24, 0);
		int d = place(_r4s24, 0x24, true);
		setDeadline(_r4s24, _game->now() + d * kMsPerStep);
		bmpVisible(0x4df460, true);
		playAt(0x24);
		return 0;
	}
	return regionLast(id, state);
}

int AtticScene::regionLast(int32 id, int16 *state) {
	if (id == 0x3f9 && !_toRoom9) {
		_inputLocked = 1;
		ned()->enable(false);
		_toRoom25 = 1;
		return 0;
	}
	if (id == 0x3fa && !_toRoom25) {
		_inputLocked = 1;
		ned()->show(false);
		_toRoom9 = 1;
		return 0;
	}
	if (id != _r9Region && id != _r25Region)
		return 0;
	*state = 0xe;
	return 1;
}

// ---- 0x4061a0: the hit hook. found is the target Ned's zap reached ----

int AtticScene::hitTarget(int16 found) {
	debugC(2, kDebugGraphics, "Attic: zap hit slot %x in room %d", found, _room);
	int d;
	switch (found) {
	case 0x0b:
		_r2s4 = slotSwitch(0xd, _r2s4);
		d = place(_r2s4, 4, true);
		setDeadline(_r2s4, _game->now() + d * kMsPerStep);
		playAt(0xd);
		_r2Hits4++;
		return 1;
	case 0x1b:
	case 0x1c:
		if (!_r10Done && _rooms[10].polled > 2) {
			targetRemove(_r10s1C);
			_r10s1C = slotSwitch(0xf, _r10s1C);
			d = place(_r10s1C, 0x1c, true);
			setDeadline(_r10s1C, _game->now() + d * kMsPerStep);
			_rooms[10].exits[1] = 0xd;
			_rooms[2].exits[0] = 0xd;
			_r10Done = 1;
			playAt(0xf, 0x1c);
			_viewRoomX = 10;
			_inputLocked = 1;
			return 1;
		}
		if (_rooms[10].polled < 2)
			return 1;
		targetRemove(_r10s1C);
		_r10s1C = slotSwitch(0x1b, _r10s1C);
		d = place(_r10s1C, 0x1c, true);
		setDeadline(_r10s1C, _game->now() + d * kMsPerStep);
		playAt(0x1b);
		return 1;
	case 0x2b:
		if (_rooms[9].polled < 2)
			return 1;
		targetRemove(_r9s2B);
		_r9s2B = slotSwitch(0x2c, _r9s2B);
		d = place(_r9s2B, 0x2b, true);
		setDeadline(_r9s2B, _game->now() + d * kMsPerStep);
		_r9Time = slot(_r9s2B).deadline + 6000;
		if (_r9Region)
			removeRegion(_r9Region);
		playAt(0x2c);
		return 1;
	case 0x21:
		if (_rooms[4].polled < 2)
			return 1;
		targetRemove(_r4s21);
		_r4s21 = slotSwitch(0x22, _r4s21);
		d = place(_r4s21, 0x21, true);
		setDeadline(_r4s21, _game->now() + d * kMsPerStep);
		playAt(0x22);
		return 1;
	case 0x37:
		targetRemove(_r12s36);
		_r12s36 = slotSwitch(0x38, _r12s36);
		d = place(_r12s36, 0x36, true);
		setDeadline(_r12s36, _game->now() + d * kMsPerStep);
		_r12Open36 = 1;
		if (!_r12MapAAdded) {
			addMap(map(0x525740));
			_r12MapAAdded = 1;
		}
		if (_r12RegionA)
			removeRegion(_r12RegionA);
		return 1;
	case 0x32:
	case 0x33:
		targetRemove(_r12s32);
		_r12s32 = slotSwitch(0x34, _r12s32);
		d = place(_r12s32, 0x32, true);
		setDeadline(_r12s32, _game->now() + d * kMsPerStep);
		playAt(0x34);
		if (_rooms[12].polled < 4) {
			_r12Time = _game->now() + 3000;
			return 1;
		}
		_r12Time = slot(_r12s32).deadline + _game->exe().readSint32(kR12Delays + _r12Count * 4);
		playSound(_r12Count + 0xf1);
		_r12Count = (_r12Count + 1) & 7;
		return 1;
	case 0x31:
	case 0x83:
		_r5Hits31++;
		if (_r5Hits31 < 2) {
			targetRemove(_r5s31);
			_r5s31 = slotSwitch(0x83, _r5s31);
			d = place(_r5s31, 0x31, true);
			setDeadline(_r5s31, _game->now() + d * kMsPerStep);
			playAt(0x83);
			return 1;
		}
		_r5Down31 = 1;
		targetRemove(_r5s31);
		_r5s31 = slotSwitchPre(0x85, _r5s31);
		setStep(_r5s31, 0);
		d = place(_r5s31, 0x31, true);
		setDeadline(_r5s31, _game->now() + d * kMsPerStep);
		playAt(0x85, 0x83);
		if (_r5Region31)
			removeRegion(_r5Region31);
		return 1;
	case 0x46:
		if (!(_rooms[7].flags & kRoomLoaded))
			return 1;
		_r7s46 = slotSwitch((int16)_game->exe().readUint16(kR7Variants + _r7Variant * 2), _r7s46);
		d = place(_r7s46, 0x46, true);
		setDeadline(_r7s46, _game->now() + d * kMsPerStep);
		playAtNotify(_r7s46, -1, 0x40ebb0);
		return 1;
	case 0x3a:
		if (!_r12Open36)
			return 1;
		targetRemove(_r12s39);
		_r12s39 = slotSwitch(0x94, _r12s39);
		d = place(_r12s39, 0x39, true);
		setDeadline(_r12s39, _game->now() + d * kMsPerStep);
		_r12Open39 = 1;
		if (!_r12MapBAdded) {
			addMap(map(0x525744));
			_r12MapBAdded = 1;
		}
		if (_r12RegionB)
			removeRegion(_r12RegionB);
		return 1;
	case 0x5e:
		targetRemove(_r18s5E);
		_r18s5E = slotSwitch(0xaa, _r18s5E);
		d = place(_r18s5E, 0x5e, true);
		setDeadline(_r18s5E, _game->now() + d * kMsPerStep);
		playAt(0xaa, 0x5e);
		return 1;
	case 0x59:
	case 0x5a:
		targetRemove(_r17s59);
		_r17s59 = slotSwitch(0x5b, _r17s59);
		d = place(_r17s59, 0x59, true);
		setDeadline(_r17s59, _game->now() + d * kMsPerStep);
		playAt(0x5b, 0x5a);
		return 1;
	case 0x62:
		targetRemove(_r18s62);
		_r18s62 = slotSwitch(99, _r18s62);
		d = place(_r18s62, 0x62, true);
		setDeadline(_r18s62, _game->now() + d * kMsPerStep);
		return 1;
	case 0x86:
	case 0x87:
		_r5Hits86++;
		if (_r5Hits86 < 2) {
			targetRemove(_r5s86);
			_r5s86 = slotSwitch(0x87, _r5s86);
			d = place(_r5s86, 0x86, true);
			setDeadline(_r5s86, _game->now() + d * kMsPerStep);
			playAt(0x87);
			return 1;
		}
		_r5Down86 = 1;
		targetRemove(_r5s86);
		_r5s86 = slotSwitchPre(0x89, _r5s86);
		setStep(_r5s86, 0);
		d = place(_r5s86, 0x86, true);
		setDeadline(_r5s86, _game->now() + d * kMsPerStep);
		playAt(0x89, 0x87);
		if (_r5Region86)
			removeRegion(_r5Region86);
		return 1;
	case 0x8a:
	case 0x8b:
		_r5Hits8A++;
		if (_r5Hits8A < 2) {
			targetRemove(_r5s8A);
			_r5s8A = slotSwitch(0x8b, _r5s8A);
			d = place(_r5s8A, 0x8a, true);
			setDeadline(_r5s8A, _game->now() + d * kMsPerStep);
			playAt(0x8b);
			return 1;
		}
		_r5Down8A = 1;
		targetRemove(_r5s8A);
		_r5s8A = slotSwitchPre(0x8d, _r5s8A);
		setStep(_r5s8A, 0);
		d = place(_r5s8A, 0x8a, true);
		setDeadline(_r5s8A, _game->now() + d * kMsPerStep);
		playAt(0x8d, 0x8b);
		if (_r5Region8A)
			removeRegion(_r5Region8A);
		return 1;
	case 0x98:
		if (_r12Open39) {
			targetRemove(_r12s95);
			_r12s95 = slotSwitch(0x3e, _r12s95);
			d = place(_r12s95, 0x95, true);
			setDeadline(_r12s95, _game->now() + d * kMsPerStep);
			_r12Open95 = 1;
			if (!_r12MapCAdded) {
				addMap(map(0x525748));
				_r12MapCAdded = 1;
			}
			if (_r12RegionC)
				removeRegion(_r12RegionC);
		}
		return 1;
	case 0xae:
		if (_r21s218 != -1)
			return 1;
		if (_r21Ready && _r21s208 == 0xee) {
			r21Hit(0xec, true);
			return 1;
		}
		if (_r21s208 == 0xed) {
			r21Hit(0x194, false);
			return 1;
		}
		_r21Step = 0;
		_r21X = _r21Pos[0];
		_r21Y = _r21PosY[0];
		_r21X2 = _r21Pos[0] + 0x200;
		_r21Y2 = _r21PosY[0] - 0xf0;
		_r21Steps = 0x10;
		_r21Arc = 0x10;
		return 1;
	case 0xb3:
	case 0xb4:
		if (_rooms[4].polled > 1) {
			targetRemove(_r4sB3);
			_r4sB3 = slotSwitch(0xb5, _r4sB3);
			d = place(_r4sB3, 0xb3, true);
			setDeadline(_r4sB3, _game->now() + d * kMsPerStep);
			playAt(0xb5);
		}
		return 1;
	case 0xbf:
		if (_rooms[8].polled > 1) {
			targetRemove(_r8sBF);
			_r8sBF = slotSwitch(0xc0, _r8sBF);
			setStep(_r8sBF, 0);
			d = place(_r8sBF, 0xbf, true);
			setDeadline(_r8sBF, _game->now() + d * kMsPerStep);
			playAt(0xc0);
		}
		return 1;
	case 0xc2:
		if (_rooms[5].polled > 2) {
			_r5Hit = 1;
			targetRemove(_r5s2F);
			_r5s2F = slotSwitch(0x30, _r5s2F);
			d = place(_r5s2F, 0x2f, true);
			setDeadline(_r5s2F, _game->now() + d * kMsPerStep);
			playAt(0xc2);
			playAt(0x30);
			_viewRoomX = 5;
			_inputLocked = 1;
		}
		return 1;
	case 0xcf:
		_r2sC9 = slotSwitch(0xd1, _r2sC9);
		d = place(_r2sC9, 0xc9, true);
		setDeadline(_r2sC9, _game->now() + d * kMsPerStep);
		playAt(0xd);
		_r2HitsC9++;
		return 1;
	case 0xef:
		targetRemove(_r19sEF);
		_r19HitEF = 1;
		return 1;
	case 0x184:
		targetRemove(_r25s184);
		_r25s184 = slotSwitch(0x185, _r25s184);
		d = place(_r25s184, 0x184, true);
		setDeadline(_r25s184, _game->now() + d * kMsPerStep);
		_r25Time184 = slot(_r25s184).deadline + 6000;
		if (_r25Region)
			removeRegion(_r25Region);
		return 1;
	default:
		return 1;
	}
}

// 0x4061a0 case 0xae: the hit starts the room 21 sequence (0xec ready, 0x194 not)
void AtticScene::r21Hit(int newSlot, bool ready) {
	targetRemove(_r21sB0);
	_r21s218 = slotSwitch(newSlot, _r21s218);
	int d = place(_r21s218, _r21s218, true);
	setDeadline(_r21s218, _game->now() + d * kMsPerStep);
	if (ready)
		_r21Done = 1;
	int sound = -1;
	if (_r21Count == 0) {
		_r21sB0 = slotSwitch(0x186, _r21sB0);
		sound = 0x186;
	} else if (_r21Count == 1) {
		if (_rooms[21].polled > 1) {
			_r21sB0 = slotSwitch(0x188, _r21sB0);
			sound = 0x188;
		} else {
			_r21sB0 = slotSwitch(0x186, _r21sB0);
		}
	} else if (_r21Count == 2) {
		_r21sB0 = slotSwitch(_rooms[21].polled < 2 ? 0x186 : 0x18c, _r21sB0);
	}
	if (sound >= 0)
		playAt(sound);
	_r21Count = (_r21Count + 1) % 3;
	d = place(_r21sB0, 0xb0, true);
	setDeadline(_r21sB0, _game->now() + d * kMsPerStep);
	_r21s208 = slotSwitch(-1, _r21s208);
	playAt(_r21sB0);
	playAt(ready ? 0xec : 0x194);
	_r21Step = 0;
	int k = _r21s218 == 0x194 ? 1 : 0;
	_r21X = _r21Pos[k];
	_r21Y = _r21PosY[k];
	_r21X2 = _r21X + 0x200;
	_r21Y2 = _r21Y - 0xf0;
	_r21Steps = 0x10;
	_r21Arc = 0x10;
}

// ---- 0x407c50: the camera ----

// The scroll step towards a target d pixels away
static int stepX(int d) {
	if (d < 7)
		return 1;
	if (d < 0xd)
		return 2;
	if (d < 0x19)
		return 4;
	return d > 0x30 ? 0x12 : 0xc;
}

static int stepY(int d) {
	if (d < 0x97)
		return d > 0x19 ? 2 : 1;
	return d < 0x12d ? 6 : 0x10;
}

void AtticScene::camera(uint32 now) {
	int dx = 0, dy = 0;
	int room = _room;
	int sx = _nedPos.x + _scrollX;
	int sy = _nedPos.y + _scrollY;
	int16 state = ned()->getState();

	if (_cameraJump) {
		if (!_cameraLocked) {
			_cameraJump = 0;
			dy = _jumpY;
			dx = _jumpX;
		}
		goto apply;
	}
	if (_cameraLocked)
		goto apply;
	if (state == 10)
		return;

	// Lead Ned: facing left keeps him right of 440, facing right left of 200
	if (facing() == 0) {
		if (state == 7) {
			if (_lastCamMove + 0x12 < now)
				dx = dy = 1;
		} else if (state == 8) {
			if (_lastCamMove + 0xc < now) {
				dx = 1;
				dy = -1;
			}
		} else if (state == 1 && sx >= 0x1b8) {
			if ((ned()->getCostume() != 8 ? 0xc : 0x48) + _lastCamMove < now)
				dx = 1;
		} else if (sx < 0x1b8) {
			dx = stepX(0x1b8 - sx);
		}
	} else {
		if (state == 7) {
			if (_lastCamMove + 0x12 < now) {
				dy = 1;
				dx = -1;
			}
		} else if (state == 8) {
			if (_lastCamMove + 0xc < now)
				dx = dy = -1;
		} else if (state == 1 && sx < 0xc9) {
			if ((ned()->getCostume() != 8 ? 0xc : 0x48) + _lastCamMove < now)
				dx = -1;
		} else if (200 < sx) {
			dx = -stepX(sx - 200);
		}
	}
	if (sy < 200)
		dy = stepY(200 - sy);
	else if (0x118 < sy)
		dy = -stepY(sy - 0x118);

	// Rooms that pull the view
	if (room == 5 && state == 5) {
		_viewRoomX = 0xb;
		_viewRoomY = -1;
	} else if (room == 0x10) {
		if (-(_rooms[16].view[3] - 0x1df) != _scrollY)
			_viewRoomY = 0x10;
		else
			_viewRoomX = -1;
	} else if (room == 0xb) {
		_viewRoomX = 0xb;
		_viewRoomY = -1;
	} else if (_viewRoomX == 0xb && room == 5 && dx != 0) {
		if (-(_rooms[5].view[3] - 0x1df) == dy + _scrollY) {
			_viewRoomX = -1;
			_viewRoomY = -1;
		} else {
			_viewRoomY = 5;
			_viewRoomX = 0xb;
		}
	} else if (room == 0x12) {
		if (sy < 200 && dx < 0 && dy > 0)
			_viewRoomY = 0x15;
		else if (dx > 0 && dy < 0)
			_viewRoomY = -1;
	} else {
		cameraPull(room, state, sx, sy, dx, dy);
	}

	// Scroll to the pulling rooms' views, then keep out of rooms the edge locks close
	if (_viewRoomX != -1) {
		const Room &v = _rooms[_viewRoomX];
		if (-_scrollX < v.view[0]) {
			int d = _scrollX + v.view[0];
			dy = 0;
			dx = -stepX(d);
			if (v.view[2] - 0x27f < ABS(dx + _scrollX))
				dx = ABS(_scrollX) + (0x27f - v.view[2]);
		} else {
			int lim = v.view[2] - 0x27f;
			if (-lim == _scrollX || -_scrollX < lim) {
				dx = 0;
			} else {
				dy = 0;
				dx = stepX(ABS(0x27f - _scrollX - v.view[2]));
				if (-(dx + _scrollX) < v.view[0])
					dx = ABS(_scrollX) - v.view[0];
			}
		}
	}
	if (_viewRoomY != -1) {
		const Room &v = _rooms[_viewRoomY];
		if (-_scrollY < v.view[1]) {
			int d = _scrollY + v.view[1];
			dx = 0;
			dy = -stepY(d);
			if (v.view[3] - 0x1df < ABS(_scrollY + dy))
				dy = ABS(_scrollY) + (0x1df - v.view[3]);
		} else {
			int lim = v.view[3] - 0x1df;
			if (-lim == _scrollY || -_scrollY < lim) {
				dy = 0;
			} else {
				dx = 0;
				dy = stepY(ABS(0x1df - _scrollY - v.view[3]));
				if (-(dy + _scrollY) < v.view[1])
					dy = ABS(_scrollY) - v.view[1];
			}
		}
	}
	if (dy > 0 && (_edgeLocks & 0x100) && -(dy + _scrollY) < _rooms[room].view[1])
		dy = ABS(_scrollY) - _rooms[room].view[1];
	if (dy < 0 && (_edgeLocks & 0x400) && _rooms[room].view[3] - 0x1df < ABS(dy + _scrollY))
		dy = ABS(_scrollY) + (0x1df - _rooms[room].view[3]);
	if (dx > 0 && (_edgeLocks & 0x80) && -(dx + _scrollX) < _rooms[room].view[0])
		dx = ABS(_scrollX) - _rooms[room].view[0];
	if (dx < 0 && (_edgeLocks & 0x200) && _rooms[room].view[2] - 0x27f < ABS(dx + _scrollX))
		dx = ABS(_scrollX) + (0x27f - _rooms[room].view[2]);

apply:
	_scrollX += dx;
	_scrollY += dy;
	static const uint32 kBase[] = { 0x4df484, 0x4df45c, 0x4df480, 0x4df47c, 0x4df460, 0x4df474, 0x4df470,
		0x4df46c, 0x4df468, 0x4df464 };
	for (uint32 va : kBase)
		bmpPlace(va);
	cameraRooms(dx, dy);
	if (!_cameraJump)
		ned()->adjPosition(dx, dy);
	_lastNedX = sx;
	_lastNedY = sy;
	if (dx || dy)
		_lastCamMove = now;
}

// 0x407c50, second half: each active room's backgrounds follow the scroll, its sprites are nudged
void AtticScene::cameraRooms(int dx, int dy) {
	if (_rooms[2].flags & kRoomActive) {
		bmpPlace(0x4df61c);
		nudge(_r2s14, dx, dy);
		if (_r2s4 != -1)
			nudge(_r2s4, dx, dy);
		if (_r2sC9 != -1)
			nudge(_r2sC9, dx, dy);
		if (_r2s9 > 0)
			nudge(_r2s9, dx, dy);
		if (_r2sCD > 0)
			nudge(_r2sCD, dx, dy);
	}
	if (_rooms[3].flags & kRoomActive) {
		bmpPlace(0x4df7ac);
		nudge(_r3s2D, dx, dy);
		nudge(_r3sEB, dx, dy);
	}
	if (_rooms[4].flags & kRoomActive) {
		bmpPlace(0x4df93c);
		bmpPlace(0x4df940);
		nudge(_r4sB3, dx, dy);
		nudge(_r4s21, dx, dy);
		if (_r4s24 != -1)
			nudge(_r4s24, dx, dy);
		if (_r4sBall != -1)
			nudge(_r4sBall, dx, dy);
	}
	if (_rooms[5].flags & kRoomActive) {
		bmpPlace(0x4dfacc);
		nudge(_r5s31, dx, dy);
		nudge(_r5s86, dx, dy);
		nudge(_r5s8A, dx, dy);
		nudge(_r5s2F, dx, dy);
		nudge(_r5s3B, dx, dy);
		nudge(_r5sC2, dx, dy);
	}
	if (_rooms[6].flags & kRoomActive)
		bmpPlace(0x4dfc5c);
	if (_rooms[7].flags & kRoomActive) {
		bmpPlace(0x4dfdec);
		bmpPlace(0x4dfdf4);
		nudge(_r7s46, dx, dy);
		nudge(_r7s45, dx, dy);
	}
	if (_rooms[8].flags & kRoomActive) {
		bmpPlace(0x4dff7c);
		if (_rooms[8].polled > 1)
			bmpPlace(0x4dff80);
		bmpPlace(0x4dff84);
		nudge(_r8sBF, dx, dy);
		if (_r8sCh != -1)
			nudge(_r8sCh, dx, dy);
	}
	if (_rooms[9].flags & kRoomActive) {
		bmpPlace(0x4e010c);
		nudge(_r9s2B, dx, dy);
		nudge(_r9s82, dx, dy);
	}
	if (_rooms[10].flags & kRoomActive) {
		bmpPlace(0x4e029c);
		bmpPlace(0x4e02a0);
		if (_r10s1C != -1)
			nudge(_r10s1C, dx, dy);
		if (_r10sA != -1)
			nudge(_r10sA, dx, dy);
		if (_r10s1D != -1)
			nudge(_r10s1D, dx, dy);
		if (_r10sB != -1)
			nudge(_r10sB, dx, dy);
	}
	if (_rooms[11].flags & kRoomActive)
		bmpPlace(0x4e042c);
	if (_rooms[12].flags & kRoomActive) {
		bmpPlace(0x4e05bc);
		bmpPlace(0x4e05cc);
		bmpPlace(0x4e05d0);
		nudge(_r12s32, dx, dy);
		nudge(_r12s36, dx, dy);
		nudge(_r12s39, dx, dy);
		nudge(_r12s95, dx, dy);
	}
	if (_rooms[13].flags & kRoomActive) {
		bmpPlace(0x4e074c);
		bmpPlace(0x4e0750);
		bmpPlace(0x4e0754);
		nudge(_r13s47, dx, dy);
		if (_r13sBall != -1)
			nudge(_r13sBall, dx, dy);
	}
	if (_rooms[15].flags & kRoomActive) {
		bmpPlace(0x4e0a6c);
		bmpPlace(0x4e0a70);
	}
	if (_rooms[16].flags & kRoomActive)
		bmpPlace(0x4e0bfc);
	if (_rooms[17].flags & kRoomActive) {
		bmpPlace(0x4e0d8c);
		bmpPlace(0x4e0d90);
		bmpPlace(0x4e0d94);
		bmpPlace(0x4e0d98);
		nudge(_r17s59, dx, dy);
		nudge(_r17s61, dx, dy);
		nudge(_r17s5C, dx, dy);
		nudge(_r17sF[0], dx, dy);
		nudge(_r17sF[1], dx, dy);
		nudge(_r17sF[2], dx, dy);
		nudge(_r17sF[3], dx, dy);
	}
	if (_rooms[18].flags & kRoomActive) {
		bmpPlace(0x4e0f1c);
		bmpPlace(0x4e0f20);
		bmpPlace(0x4e0f24);
		bmpPlace(0x4e0f28);
		nudge(_r18s5E, dx, dy);
		nudge(_r18s5D, dx, dy);
		nudge(_r18s62, dx, dy);
	}
	if (_rooms[19].flags & kRoomActive) {
		bmpPlace(0x4e10ac);
		bmpPlace(0x4e10b0);
		bmpPlace(0x4e10b4);
		nudge(_r19s67, dx, dy);
		nudge(_r19s65, dx, dy);
		nudge(_r19sAny, dx, dy);
		nudge(_r19s6D, dx, dy);
		nudge(_r19sEF, dx, dy);
	}
	if (_rooms[21].flags & kRoomActive) {
		bmpPlace(0x4e13cc);
		nudge(_r21sB0, dx, dy);
		if (_r21s208 != -1)
			nudge(_r21s208, dx, dy);
		if (_r21s218 != -1)
			nudge(_r21s218, dx, dy);
	}
	if (_rooms[22].flags & kRoomActive) {
		bmpPlace(0x4e155c);
		bmpPlace(0x4e1560);
		if (_r22s183 != -1)
			nudge(_r22s183, dx, dy);
	}
	if (_rooms[23].flags & kRoomActive) {
		bmpPlace(0x4e16ec);
		bmpPlace(0x4e16f0);
		bmpPlace(0x4e16f4);
	}
	if (_rooms[24].flags & kRoomActive)
		bmpPlace(0x4e187c);
	if (_rooms[25].flags & kRoomActive) {
		bmpPlace(0x4e1a0c);
		if (_r25s != -1)
			nudge(_r25s, dx, dy);
		nudge(_r25s184, dx, dy);
	}
}

// 0x407c50: the rooms 0xf, 0x13, 0x15..0x17, 0xd, 10, 0x19 and 6 pull the view or lock the edges
void AtticScene::cameraPull(int room, int16 state, int sx, int sy, int dx, int dy) {
	if (_viewRoomY == 0x15) {
		if (room == 0x15) {
			_viewRoomY = -1;
			return;
		}
	} else if (room == 0x15) {
		uint16 locks;
		if (!(_rooms[15].flags & kRoomActive) || !(_rooms[18].flags & kRoomActive))
			locks = _edgeLocks | 0x80;
		else
			locks = (_edgeLocks & ~0x80) | (_rooms[21].flags & 0x80);
		if (!(_rooms[19].flags & kRoomActive))
			_edgeLocks = locks | 0x200;
		else
			_edgeLocks = (locks & ~0x200) | (_rooms[21].flags & 0x200);
		return;
	}
	if (room == 0xf) {
		if (!(_rooms[21].flags & kRoomActive) || -_rooms[21].view[1] != _scrollY)
			_edgeLocks |= 0x200;
		else
			_edgeLocks = (_edgeLocks & ~0x200) | (_rooms[15].flags & 0x200);
		if ((_rooms[17].flags & kRoomActive) && (sy > 199 || dx > -1 || dy < 1))
			_viewRoomY = -1;
		else
			_viewRoomY = 0x15;
		return;
	}
	if (room == 0x13) {
		if (-_scrollY < _rooms[21].view[1] && (_rooms[21].flags & kRoomActive)) {
			_edgeLocks |= 0x80;
			if (-_scrollX < _rooms[19].view[0]) {
				_viewRoomX = 0x13;
				return;
			}
		} else {
			_edgeLocks = (_edgeLocks & ~0x80) | (_rooms[19].flags & 0x80);
		}
		if (_viewRoomX == 0x13 && _rooms[19].view[0] <= -_scrollX)
			_viewRoomX = -1;
		return;
	}
	if (room == 0x16) {
		if (-_scrollY < _rooms[19].view[1]) {
			_edgeLocks |= 0x80;
			if (-_scrollX < _rooms[22].view[0])
				_viewRoomX = 0x16;
		} else if (_rooms[19].flags & kRoomActive) {
			_edgeLocks = (_edgeLocks & ~0x80) | (_rooms[22].flags & 0x80);
		}
		if (_viewRoomX == 0x16 && _rooms[22].view[0] <= -_scrollX)
			_viewRoomX = -1;
		return;
	}
	if (room == 0x17 && !_endPan) {
		_viewRoomY = 0x17;
		int lim = _rooms[23].view[3] - 0x1df;
		_viewRoomX = (-lim == _scrollY || -_scrollY < lim) ? -1 : 0x16;
		return;
	}
	if (room == 10 && state == 0x12) {
		_viewRoomX = 0xd;
		_viewRoomY = -1;
		return;
	}
	if (room == 0xd && state != 0x12) {
		_viewRoomX = -1;
		_viewRoomY = -1;
		return;
	}
	if (room == 0x19) {
		if (-(dx + _scrollX) < _rooms[7].view[0] || _rooms[25].view[2] - 0xfa < _nedPos.x)
			_viewRoomY = 6;
		else if (!_r6Look)
			_viewRoomY = -1;
	} else if (_viewRoomY == 6 && (-_rooms[6].view[1] == _scrollY || room == 7)) {
		_viewRoomY = -1;
	}
}

// ---- The room tracker (0x467b40..0x4684c1, table 0x497930) ----
//
// A room loads in stages: load (+0x3c) submits one request list; its completion callback
// counts the stage (+0x00) and sets 0x20 (and 0x800 for one more stage, 0x10 when done).
// poll (+0x40) then shows what the stage brought (+0x04) and sets 0x08 once the room can
// be seen. Rooms next to Ned's load, rooms away from him unload.

// 0x467b40
void AtticScene::roomsEnter(int room) {
	if (room != -1 && (_rooms[room].flags & kRoomIdle)) {
		_rooms[room].stages = 0;
		_rooms[room].polled = 0;
		if (roomLoad(room) == 0) {
			_rooms[room].flags &= ~kRoomIdle;
			_rooms[room].flags |= kRoomLoading;
		}
	}
	_room = room;
	_edgeLocks = 0x780;
}

// 0x467bb0 -> 0x467e30: every room unloads (waiting for loads in progress), then no room
void AtticScene::roomsLeave() {
	for (int r = 2; r < kRoomCount; r++) {
		if (_rooms[r].flags & kRoomLoading)
			_rooms[r].flags |= kRoomCancel;
	}
	for (int r = 2; r < kRoomCount; r++) {
		Room &room = _rooms[r];
		if (room.flags & kRoomIdle)
			continue;
		if (!(room.flags & (kRoomCancel | kRoomUnloading))) {
			room.flags |= kRoomUnloading;
			roomUnload(r);
			room.flags &= 0x3fa7;
			room.flags |= kRoomIdle;
			room.stages = 0;
		} else {
			// The original waits for the loader threads; here each update runs the callbacks
			for (int n = 0; !(room.flags & (kRoomAbort | kRoomLoaded | kRoomIdle)); n++) {
				if (n == kWaitLimit || _game->getEngine()->shouldQuit()) {
					warning("Attic: room %d never finished loading (flags %x)", r, room.flags);
					break;
				}
				roomsUpdate(_game->now());
			}
		}
	}
	_room = -1;
}

// 0x467bc0 -> 0x4680e0: Ned's room by PtInRect over the exits, then the update walk from it
void AtticScene::roomsUpdate(uint32 now) {
	runCallbacks();
	for (int r = 2; r < kRoomCount; r++)
		_rooms[r].flags &= ~kRoomVisited;
	if (_room < 0)
		return;
	for (int i = 0; i < 4; i++) {
		int exit = _rooms[_room].exits[i];
		if (exit == -1)
			continue;
		const int32 *rc = _rooms[exit].rect;
		if (rc[0] <= _nedPos.x && _nedPos.x < rc[2] && rc[1] <= _nedPos.y && _nedPos.y < rc[3]) {
			_room = exit;
			_edgeLocks = 0x780;
			for (int k = 0; k < 4; k++) {
				int e = _rooms[_room].exits[k];
				if (e == -1 || !(_rooms[e].flags & kRoomActive))
					continue;
				uint16 own = _rooms[_room].flags;
				switch (k) {
				case 0:
					_edgeLocks = (_edgeLocks & ~0x100) | (own & 0x100);
					break;
				case 1:
					_edgeLocks = (_edgeLocks & ~0x400) | (own & 0x400);
					break;
				case 2:
					_edgeLocks = (_edgeLocks & ~0x80) | (own & 0x80);
					break;
				default:
					_edgeLocks = (_edgeLocks & ~0x200) | (own & 0x200);
					break;
				}
			}
			break;
		}
	}
	roomUpdate(_room, now);
}

// 0x467c50: a neighbour became active; its side's edge lock takes the current room's bit
void AtticScene::roomEdge(int room) {
	int side = -1;
	for (int i = 0; i < 4; i++) {
		if (_rooms[room].exits[i] == _room) {
			side = i;
			break;
		}
	}
	uint16 own = _rooms[_room].flags;
	switch (side) {
	case 0:
		_edgeLocks = (_edgeLocks & ~0x400) | (own & 0x400);
		break;
	case 1:
		_edgeLocks = (_edgeLocks & ~0x100) | (own & 0x100);
		break;
	case 2:
		_edgeLocks = (_edgeLocks & ~0x200) | (own & 0x200);
		break;
	case 3:
		_edgeLocks = (_edgeLocks & ~0x80) | (own & 0x80);
		break;
	default:
		break;
	}
}

// 0x468270: loads, polls, ticks and unloads room, then recurses into its exits
void AtticScene::roomUpdate(int r, uint32 now) {
	while (!(_rooms[r].flags & kRoomVisited)) {
		Room &room = _rooms[r];
		if ((_rooms[_room].flags & kRoomActive) && (room.flags & kRoomIdle)) {
			if (roomShouldLoad(r) && (room.flags & kRoomIdle)) {
				room.stages = 0;
				room.polled = 0;
				if (roomLoad(r) == 0) {
					room.flags &= ~kRoomIdle;
					room.flags |= kRoomLoading;
				}
			}
		}
		if ((room.flags & kRoomMore) && roomLoad(r) == 0) {
			room.flags &= ~kRoomIdle;
			room.flags |= kRoomLoading;
			room.flags &= ~kRoomMore;
		}
		bool doTick;
		if (room.polled < room.stages || (room.flags & kRoomArrived)) {
			if (room.stages > 0 && roomPoll(r) == 0) {
				room.flags |= kRoomReady;
				room.flags &= ~kRoomArrived;
			}
			if (room.flags & kRoomActive)
				roomEdge(r);
			doTick = room.flags & kRoomReady;
		} else if (room.flags & kRoomReady) {
			doTick = true;
		} else {
			if (room.stages > 0) {
				if (roomPoll(r) == 0) {
					room.flags |= kRoomReady;
					room.flags &= ~kRoomArrived;
				}
				if (room.flags & kRoomActive)
					roomEdge(r);
			}
			doTick = room.flags & kRoomReady;
		}
		if (doTick)
			roomTick(r, now);
		if ((room.flags & kRoomAbort) ||
			((room.flags & kRoomActive) && !(room.flags & (kRoomIdle | kRoomUnloading)) && roomShouldUnload(r))) {
			room.flags |= kRoomUnloading;
			roomUnload(r);
			room.flags &= 0x3fa7;
			room.flags |= kRoomIdle;
			room.stages = 0;
		}
		room.flags |= kRoomVisited;
		if (room.exits[2] != -1)
			roomUpdate(room.exits[2], now);
		if (room.exits[3] != -1)
			roomUpdate(room.exits[3], now);
		if (room.exits[0] != -1)
			roomUpdate(room.exits[0], now);
		r = room.exits[1];
		if (r == -1)
			return;
	}
}

// 0x467d60: a room loads when Ned's room is one of its exits
bool AtticScene::isNeighbour(int r) {
	for (int i = 0; i < 4; i++) {
		if (_rooms[r].exits[i] == _room)
			return true;
	}
	return false;
}

// 0x467da0: a room unloads when it's neither Ned's room nor next to it and done loading;
// one still loading is marked to unload when it finishes
bool AtticScene::isFar(int r) {
	if (r == _room)
		return false;
	for (int i = 0; i < 4; i++) {
		if (_rooms[r].exits[i] == _room)
			return false;
	}
	if (_rooms[r].flags & (kRoomAbort | kRoomLoaded))
		return true;
	if (_rooms[r].flags & kRoomLoading)
		_rooms[r].flags |= kRoomCancel;
	return false;
}

// The room table's six functions per room
bool AtticScene::roomShouldLoad(int r) {
	switch (_rooms[r].fn[0]) {
	case 0x467d60:
		return isNeighbour(r);
	case 0x40be30:
		return r5ShouldLoad();
	case 0x418150:
		return r23ShouldLoad();
	default:
		return false;
	}
}

bool AtticScene::roomShouldUnload(int r) {
	switch (_rooms[r].fn[4]) {
	case 0x467da0:
		return isFar(r);
	case 0x40be60:
		return r5ShouldUnload(r);
	default:
		return false;
	}
}

} // End of namespace NightmareNed
