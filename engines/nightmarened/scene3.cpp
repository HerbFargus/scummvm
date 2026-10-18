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

#include "nightmarened/detection.h"
#include "nightmarened/display.h"
#include "nightmarened/game.h"
#include "nightmarened/hints.h"
#include "nightmarened/media.h"
#include "nightmarened/ned.h"
#include "nightmarened/nightmarened.h"
#include "nightmarened/rdx.h"
#include "nightmarened/scene3.h"
#include "nightmarened/scene3_room.h"
#include "nightmarened/scene3_school.h"

namespace NightmareNed {

// The rooms' list callbacks (0x468690..0x468890), one per room
static const uint32 kRoomCallbacks[] = {
	0x468690, 0x4686d0, 0x468710, 0x468750, 0x468790, 0x4687d0, 0x468810, 0x468850, 0x468890
};

Scene3::Scene3(Game *game) : Scene(game, 3), _puff(game) {
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
		room.flags = exe.readUint16(va + 0x54);
	}
	// 0x472080 (a static initialiser): no room yet
	_room = -1;
	_edgeLocks = 0;
	_scrollX = _scrollY = 0;
	_unportedExit = -1;
	for (int i = 0; i < 8; i++)
		_saved[i] = 0;
	for (int i = 0; i < 10; i++)
		_doors[i] = 0;
	for (int i = 0; i < 16; i++)
		_mapSlots[i] = nullptr;
	for (int i = 0; i < 4; i++)
		_region1e61[i] = 0;
	memset(_palette, 0, sizeof(_palette));
	memset(_paletteAlt, 0, sizeof(_paletteAlt));
	_lastTick = 0;
	_startRoom = 0;
	_frontZ = 9;
	_walkIn = _walkInDoor = _hintBack = _hintAction = _behind = 0;
	_cameraBlocked = 0;
	_cameraTime = _stepTime = 0;
	_exit = 0;
	_passUp = _passDown = _passLeft = _passRight = 0;
	_r6Enter = _r7Enter = _upstairs = 0;
	_r0Grabber = _r0Entrance = -1;
	_r1Creature = -1;
	_r1Hit = _r1Gone = 0;
	_r2Creature = -1;
	_r2Time = 0;
	_r2Region = 0;
	_r3a = _r3b = -1;
	_r3aTime = _r3bTime = 0;
	_r3aRegion = _r3bRegion = 0;
	_r3Done = _r3aTouch = _r3bTouch = 0;
	_r4a = _r4b = -1;
	_r4aHit = 0;
	_r4Time = 0;
	_r4Gone = 0;
	_r5Creature = _r5Slot = -1;
	_r5Touch = 0;
	_r5Region = 0;
	_r6Creature = -1;
	_r6Time = 0;
	_r6MinX = _r6MaxX = 0;
	_r8Creature = _r8Slot = -1;
	_r8Time = 0;
	_r8Turn = _r8Zap = _r8Gone = 0;
	_r8Ride = _r8RideOn = _r8RideOff = _r8Action = 0;

	_hooks.user = this;
	_hooks.use = hookUse;
	_hooks.region = hookRegion;
	_hooks.findTarget = hookFindTarget;
	_hooks.hitTarget = hookHitTarget;

	addSubScene(new Scene3School(game));
	addSubScene(new Scene3Room(game));
}

Scene3::~Scene3() {
	freeAll();
}

// ---- Resources ----

Slot &Scene3::slot(int id) {
	return _game->res().getSlot(kArea, CLIP(id, 0, (int)kAreaSlots - 1));
}

Slot &Scene3::nedSlot(int id) {
	return _game->res().getSlot(1, CLIP(id, 0, (int)kAreaSlots - 1));
}

void Scene3::visible(int id, bool on) {
	if (id >= 0 && id < kAreaSlots && slot(id).sprite)
		slot(id).sprite->setVisibility(on);
}

// objGetVisibility on a slot's sprite
bool Scene3::isVisible(int id) {
	return id >= 0 && id < kAreaSlots && slot(id).sprite && slot(id).sprite->getVisibility();
}

BitmapRec *Scene3::bmp(uint32 va) {
	return _bitmaps.contains(va) ? _bitmaps[va] : nullptr;
}

CollisionMap *Scene3::map(uint32 va) {
	return _maps.contains(va) ? _maps[va] : nullptr;
}

// DAT_0051f610[i]: cbix i (map 0 is DAT_0051f684)
CollisionMap *Scene3::mapAt(int i) {
	return (i >= 1 && i < 16) ? map(0x51f610 + 4 * i) : nullptr;
}

// 0x4586e0 (synchronous): every entry loads now; the completion callbacks run on the next room update
int Scene3::submit(uint32 va, int count, int scene) {
	NafArchive *naf = _game->getNaf(kNaf);
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
			if (req.hasPalette && req.paletteVA == kPaletteMain)
				memcpy(_palette, req.palette, sizeof(_palette));
			if (req.hasPalette && req.paletteVA == kPaletteAlt)
				memcpy(_paletteAlt, req.palette, sizeof(_paletteAlt));
			_entries[e] = req;
		}
		uint32 cb = exe.readUint32(e + 0x1c);
		if (cb)
			_pendingCallbacks.push_back(cb);
	}
	return 0;
}

// 0x458dc0: frees backwards
int Scene3::release(uint32 va, int count) {
	for (int i = count - 1; i >= 0; i--) {
		uint32 e = va + i * 0x24;
		if (!_entries.contains(e))
			continue;
		Common::Array<Request> one;
		one.push_back(_entries[e]);
		if (one[0].bitmap)
			_bitmaps.erase(one[0].destVA);
		if (one[0].collision) {
			_world.removeMap(one[0].collision);
			_maps.erase(one[0].destVA);
		}
		_game->freeList(one);
		_entries.erase(e);
	}
	return 0;
}

void Scene3::runCallbacks() {
	while (!_pendingCallbacks.empty()) {
		uint32 cb = _pendingCallbacks.front();
		_pendingCallbacks.remove_at(0);
		listCallback(cb);
	}
}

void Scene3::freeAll() {
	_pendingCallbacks.clear();
	Common::Array<uint32> keys;
	for (EntryMap::iterator it = _entries.begin(); it != _entries.end(); ++it)
		keys.push_back(it->_key);
	Common::sort(keys.begin(), keys.end());
	for (int i = (int)keys.size() - 1; i >= 0; i--)
		release(keys[i], 1);
}

// 0x459090 res_show_sprite(entry, z, 0, primary): attached hidden; slot_switch shows it
void Scene3::showSprite(uint32 entry, int z) {
	if (!isLoaded(entry))
		return;
	const Request &req = _entries[entry];
	if (req.type != Request::kSprite || req.area < 0 || req.area >= kAreaCount)
		return;
	Slot &s = _game->res().getSlot(req.area, req.id);
	if (s.sprite) {
		s.sprite->setDrawOrder(z);
		s.sprite->setDestination(_game->display().getPrimary());
		s.sprite->setVisibility(false);
	}
}

// 0x459100 res_show_bitmap(entry, 2000, 1, primary)
void Scene3::showBitmap(uint32 entry) {
	if (!isLoaded(entry))
		return;
	const Request &req = _entries[entry];
	if (req.type != Request::kBitmap || !req.bitmap || !req.bitmap->object)
		return;
	req.bitmap->object->setDrawOrder(2000);
	req.bitmap->object->setDestination(_game->display().getPrimary());
	req.bitmap->object->setVisibility(true);
}

// The rooms' background pattern: rec x, y = (x, y), placed through the xform 0x471dd0
void Scene3::bmpPlace(uint32 va, int x, int y) {
	BitmapRec *rec = bmp(va);
	if (!rec)
		return;
	rec->x = x;
	rec->y = y;
	if (rec->object)
		rec->object->setPosition(wrapX(rec->x), rec->y + _scrollY);
}

// ---- Display helpers (area 4) ----

// 0x430890
int16 Scene3::switchSlot(int newSlot, int oldSlot) {
	return (int16)_game->display().slotSwitch(kArea, (int16)newSlot, (int16)oldSlot);
}

// 0x430930
int16 Scene3::switchSlotPre(int newSlot, int oldSlot) {
	return (int16)_game->display().slotSwitch(kArea, (int16)newSlot, (int16)oldSlot, true);
}

// 0x430850 with an xform (0x471dd0 unless given): returns the step's duration
int Scene3::place(int id, int ref, bool noMirror, int *outFrame, NedXform xf) {
	int frame = 0, duration = 0;
	if (id < 0 || id >= kAreaSlots || ref < 0 || ref >= kAreaSlots)
		return 0;
	if (!_game->display().slotPlace(kArea, id, ref, &frame, &duration, noMirror, Common::Point(0, 0)))
		return 0;
	Slot &s = slot(id);
	if (xf && s.sprite) {
		Common::Point p = s.sprite->getPosition();
		int x = p.x, y = p.y;
		xf(this, x, y);
		s.sprite->setPosition(x, y);
	}
	if (outFrame)
		*outFrame = frame;
	return duration;
}

// Places a slot against ref and sets its next deadline (the pattern after every step)
void Scene3::advance(int id, int ref, bool noMirror, uint32 now) {
	int duration = place(id, ref, noMirror);
	slot(id).deadline = now + (int16)duration * kMsPerStep;
}

// 0x430a30 slot_nudge
void Scene3::nudge(int id, int dx, int dy) {
	if (id >= 0 && id < kAreaSlots && slot(id).sprite)
		slot(id).sprite->adjPosition(dx, dy);
}

// 0x430440 slot_attach_pos: where slot would be placed against ref's anchor (world)
void Scene3::attachPos(int id, int ref, int out[2], bool noMirror) {
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
	int d = rax + fox - sax;
	out[0] = noMirror ? r.x - d : r.x - s.w + d;
	out[1] = r.y + (say - ray - foy);
}

// 0x4860d0 on a slot's hit boxes
void Scene3::boxRect(int id, int frame, int k, int32 out[4]) {
	Slot &s = slot(id);
	if (s.boxes)
		s.boxes->getRawRect(frame, k, out);
	else
		out[0] = out[1] = out[2] = out[3] = -0x7fff;
}

// 0x486930 on the slot's current step
int Scene3::currentFrame(int id) {
	Slot &s = slot(id);
	int frame = 0, duration = 0;
	if (s.sequence)
		s.sequence->getStep(s.step, frame, duration);
	return frame;
}

// objApplyEffect(FX_HORIZ_FLIP) when the slot has none, then fxSetActive
void Scene3::applyFlip(int id, bool active) {
	Slot &s = slot(id);
	if (!s.sprite)
		return;
	if (!s.flip)
		s.flip = s.sprite->applyEffect(kFxHorizFlip);
	s.flip->active = active;
}

// fxGetActive on the slot's flip effect
bool Scene3::flipActive(int id) {
	Slot &s = slot(id);
	return s.flip && s.flip->active;
}

void Scene3::setDrawOrder(int id, int z) {
	if (id >= 0 && id < kAreaSlots && slot(id).sprite)
		slot(id).sprite->setDrawOrder(z);
}

// objGetPosition on a slot's sprite
Common::Point Scene3::spritePos(int id) {
	Slot &s = slot(id);
	return s.sprite ? s.sprite->getPosition() : Common::Point();
}

// Ned's box (0x450330) through the xform, against a screen rect (IntersectRect)
bool Scene3::nedBoxHits(const int32 rect[4]) {
	int32 box[4];
	ned()->getBox(ned()->getCurSlot(), _nedPos, box);
	box[0] = wrapX(box[0]);
	box[1] += _scrollY;
	box[2] = wrapX(box[2]);
	box[3] += _scrollY;
	return intersect(box, rect);
}

// IntersectRect
bool Scene3::intersect(const int32 a[4], const int32 b[4]) {
	return MAX(a[0], b[0]) < MIN(a[2], b[2]) && MAX(a[1], b[1]) < MIN(a[3], b[3]);
}

// 0x471dd0's x: near the right end of the level, the left end shows past it
int Scene3::wrapX(int x) const {
	if (_scrollX < -0x121f && x < 0x280)
		return x + kWrap + _scrollX;
	return x + _scrollX;
}

// ---- Sound ----

// 0x455880 snd_play
void Scene3::playSound(int id, int stop) {
	_game->playSlotSound(slot(id), stop >= 0 ? &slot(stop) : nullptr);
}

// 0x455930
void Scene3::stopSound(int id) {
	_game->stopSlotSound(slot(id));
}

// 0x455950 -> 0x44d020: every slot voice stops; the streamed music plays on
void Scene3::stopAllSounds() {
	for (int a = 0; a < kAreaCount; a++) {
		for (int i = 0; i < kAreaSlots; i++)
			_game->stopSlotSound(_game->res().getSlot(a, i));
	}
}

// 0x471ee0: id's sound at ref's position, when Ned is on ref's floor and within 600 px
// (around the wrap too); stop (-1 none) is stopped first
void Scene3::playNear(int id, int stop, int ref) {
	Slot &r = slot(ref);
	int rx = r.x;
	if (r.y < 0x1e0) {
		if (_nedPos.y > 600)
			return;
	} else if (_nedPos.y < 0x1e0) {
		return;
	}
	Slot &s = slot(id);
	int sx = s.x, sy = s.y;
	s.x = rx;
	s.y = r.y;
	int d = _nedPos.x - rx;
	int dist = ABS(d);
	bool play = false;
	if (dist < 600) {
		play = true;
	} else if (dist >= 0xa51) {
		if (rx < _nedPos.x)
			d = rx - _nedPos.x;
		play = d + kWorldW <= 599;
	}
	if (play)
		playSound(id, stop);
	s.x = sx;
	s.y = sy;
}

// 0x468590 (inlined in 0x46ea50): footsteps every 450 ms in the passages, a creak at the doors
void Scene3::footsteps(uint32 now) {
	if (_stepTime + 0x1c2 < now) {
		_stepTime = now;
		ned()->playOwnSound(4);
	}
	int f = ned()->getFacing();
	if ((f != 0 && (_nedPos.x == 0x10b8 || _nedPos.x == 0x10b9)) ||
		(f == 0 && (_nedPos.x == 0x1106 || _nedPos.x == 0x1107)))
		_game->playSlotSound(nedSlot(0x2d), nullptr);
	if (_nedPos.x == 0x10de || _nedPos.x == 0x10df) {
		playSound(0x16f);
		_stepTime = now + 2000;
	}
}

// ---- Collision ----

// 0x487f50 + 0x4872a0
void Scene3::addMap(CollisionMap *m) {
	if (m)
		_world.addMap(m, m->bounds.left, m->bounds.top, m->bounds.right, m->bounds.bottom);
}

// 0x487320
void Scene3::removeMap(CollisionMap *m) {
	if (m)
		_world.removeMap(m);
}

// 0x488060: bit 31 of the first region with the id is the disabled flag
bool Scene3::regionEnable(CollisionMap *m, int32 id, bool on) {
	if (!m)
		return false;
	for (CollisionMap::Region &r : m->regions) {
		if (r.id != id)
			continue;
		if (on)
			r.type &= 0x7fffffff;
		else
			r.type |= 0x80000000;
		return true;
	}
	return false;
}

// 0x487940: the first region with the id
bool Scene3::regionRect(CollisionMap *m, int32 id, int32 rect[4]) {
	if (!m)
		return false;
	for (const CollisionMap::Region &r : m->regions) {
		if (r.id == id) {
			rect[0] = r.rect.left;
			rect[1] = r.rect.top;
			rect[2] = r.rect.right;
			rect[3] = r.rect.bottom;
			return true;
		}
	}
	return false;
}

// 0x487600 bc_add_rect on the scene's own map, then 0x487400 world_set_user. The callers
// pass an uninitialised rect; each region is moved into place before Ned can touch it.
int32 Scene3::addRegion(uint32 type, int32 id) {
	CollisionMap::Region r;
	r.type = type;
	r.id = id;
	r.rect = Common::Rect();
	_userMap.regions.push_back(r);
	_world.setUser(&_userMap);
	return id;
}

// 0x4876d0 bc_del_rect, then 0x487400 world_set_user
void Scene3::removeRegion(int32 &id) {
	if (!id)
		return;
	for (uint i = 0; i < _userMap.regions.size(); i++) {
		if (_userMap.regions[i].id == id) {
			_userMap.regions.remove_at(i);
			break;
		}
	}
	_world.setUser(&_userMap);
	id = 0;
}

// 0x4877f0 on the scene's own map
void Scene3::moveRegion(int32 id, const int32 rect[4]) {
	for (CollisionMap::Region &r : _userMap.regions) {
		if (r.id == id) {
			r.rect.left = rect[0];
			r.rect.top = rect[1];
			r.rect.right = rect[2];
			r.rect.bottom = rect[3];
			return;
		}
	}
}

// Box k of s's frame at its attach position against ref (0x4860d0 + 0x430440), as a region
void Scene3::moveRegionTo(int32 id, int s, int ref, int frame, int k) {
	int32 rect[4];
	boxRect(s, frame, k, rect);
	int pos[2];
	attachPos(s, ref, pos, true);
	rect[0] += pos[0];
	rect[2] += pos[0];
	rect[1] += pos[1];
	rect[3] += pos[1];
	moveRegion(id, rect);
}

// 0x470470: a door opens or shuts. Its two collision maps swap (when it has them) and its
// region (when it has one) is disabled while it is open.
void Scene3::door(int &open, CollisionMap *regions, int32 region, int mapOpen, int mapShut) {
	if (mapShut != -1 && mapOpen != -1) {
		removeMap(mapAt(mapShut));
		removeMap(mapAt(mapOpen));
	}
	if (open == 0) {
		open = 1;
		if (mapShut != -1 && mapOpen != -1)
			addMap(mapAt(mapOpen));
		if (region != -1)
			regionEnable(regions, region, false);
	} else {
		open = 0;
		if (mapShut != -1 && mapOpen != -1)
			addMap(mapAt(mapShut));
		if (region != -1)
			regionEnable(regions, region, true);
	}
}

Common::Array<CollisionMap *> Scene3::overlayMaps() const {
	Common::Array<CollisionMap *> maps;
	for (int i = 0; i < _world.getMapCount(); i++)
		maps.push_back(_world.getMap(i));
	maps.push_back(const_cast<CollisionMap *>(&_userMap));
	return maps;
}

// ---- Targets ----

// 0x4309c0 olist_add_sprite
void Scene3::targetAdd(int id, bool noFlip) {
	if (id < 0)
		return;
	Placement p;
	memset(&p, 0, sizeof(p));
	p.area = kArea;
	p.slot = (int16)id;
	p.sub = 1;
	p.kind = 2;
	p.flip = noFlip ? 0 : 1;
	_targets.push_back(p);
}

// 0x486f50 olist_remove (its index loop skips the entry after each removal)
void Scene3::targetRemove(int id) {
	for (uint i = 0; i < _targets.size(); i++) {
		if (_targets[i].area == kArea && _targets[i].slot == (int16)id && _targets[i].sub == 1)
			_targets.remove_at(i);
	}
}

// ---- Ned ----

Ned *Scene3::ned() {
	return _game->ned();
}

// 0x454920 with the scene's world, scroll and hooks
void Scene3::nedTick(uint32 input, uint32 now) {
	int f = 0;
	ned()->tick(_world, input, f, _nedPos, now, _scrollX, _scrollY, &_hooks);
}

// 0x44eb00(0) + 0x44e9c0(0)
void Scene3::nedHide() {
	ned()->show(false);
	ned()->enable(false);
}

// 0x452930: Ned steps dx and settles on the floor within 0x5b (0x455550 both ways)
void Scene3::sideStep(int dx) {
	_nedPos.x += (int16)dx;
	if (_world.isSolid(_nedPos.x, _nedPos.y))
		return;
	for (int d = 1; d < 0x5b; d++) {
		if (_world.isSolid(_nedPos.x, (int16)(_nedPos.y + d))) {
			_nedPos.y += d;
			return;
		}
		if (_world.isSolid(_nedPos.x, (int16)(_nedPos.y - d))) {
			_nedPos.y -= d;
			return;
		}
	}
}

// 0x471dd0: world -> screen; near the right end the level's left end follows on
void Scene3::xform(void *user, int &x, int &y) {
	Scene3 *s = (Scene3 *)user;
	x = s->wrapX(x);
	y += s->_scrollY;
}

// 0x471e40: for what sits past the right end (slot 0x19f)
void Scene3::xformRight(void *user, int &x, int &y) {
	Scene3 *s = (Scene3 *)user;
	int dx = s->_scrollX;
	if (-0x280 < dx)
		dx -= kWorldW;
	x += dx;
	y += s->_scrollY;
}

// 0x46adb0: after action 0x2c (room 8), the options open again and vision 3 waits
void Scene3::actionFinished(void *user, int16 action) {
	Scene3 *s = (Scene3 *)user;
	s->_game->_optionsAllowed = true;
	s->ned()->setFinishCallback(nullptr, nullptr);
	s->_hintAction = 1;
}

// 0x46cdd0: after the grab of room 0
void Scene3::grabFinished(void *user, int16 action) {
	Scene3 *s = (Scene3 *)user;
	s->nedSlot(0x183).offsets[0] = -2;
	s->ned()->setFinishCallback(nullptr, nullptr);
}

// 0x475180: a vision of this world's creature (Hints)
void Scene3::hint(int which, bool facingRight) {
	_game->hints().trigger(which, facingRight, _nedPos.x, _nedPos.y, xform, this);
}

// 0x45a0f0, staying here when the target isn't registered yet
int Scene3::switchTo(int scene, int sub) {
	if (!_game->hasScene(scene, sub)) {
		if (_unportedExit != scene)
			debugC(1, kDebugLoading, "Scene 3: exit to scene %d:%d (not ported), staying", scene, sub);
		_unportedExit = scene;
		return 1;
	}
	_unportedExit = -1;
	_game->switchScene(scene, sub);
	return 0;
}

// ---- The rooms (0x4720b0-0x472770) ----
//
// Each room loads in two stages. Stage 1 (load, +0x3c) is wanted when Ned's room is a
// neighbour; its list callback counts the stage and marks it arrived (0x20); poll (+0x44)
// then shows it and makes the room active (0x08). Once one stage is in and shown, stage 2
// (+0x40) loads, and its poll adds the targets. Rooms that are neither Ned's nor next to it
// unload (+0x50).

// 0x468690..0x468890: a stage of room r arrived
void Scene3::roomLoaded(int r) {
	Room &room = _rooms[r];
	int old = room.stages++;
	if (old == 0)
		room.flags &= ~kRoomLoading;
	else if (room.stages == 2)
		room.flags = (room.flags & ~kRoomLoading) | kRoomLoaded;
	if (!(room.flags & kRoomAbort))
		room.flags |= kRoomArrived;
}

void Scene3::listCallback(uint32 fn) {
	for (int r = 0; r < kRoomCount; r++) {
		if (kRoomCallbacks[r] == fn) {
			roomLoaded(r);
			return;
		}
	}
	warning("Scene 3: unknown list callback %x", fn);
}

// 0x4720b0: every room unloads when switching from one room to another; then room's first stage loads
void Scene3::roomsEnter(int room) {
	if (room == -1 || _room != -1) {
		for (int r = 0; r < kRoomCount; r++) {
			_rooms[r].flags |= kRoomUnloading;
			roomUnload(r);
			_rooms[r].flags &= 0x3fa7;
			_rooms[r].stages = 0;
		}
	}
	if (room != -1 && (_rooms[room].flags & kRoomIdle)) {
		_rooms[room].stages = 0;
		_rooms[room].polled = 0;
		if (roomLoad(room) == 0)
			_rooms[room].flags = (_rooms[room].flags & ~kRoomIdle) | kRoomLoading;
	}
	_room = room;
	_edgeLocks = 0x780;
}

// 0x472140: rooms still loading unload once loaded; every other room unloads now
void Scene3::roomsLeave() {
	for (int r = 0; r < kRoomCount; r++) {
		if (_rooms[r].flags & kRoomLoading)
			_rooms[r].flags |= kRoomCancel;
	}
	for (int r = 0; r < kRoomCount; r++) {
		if (!(_rooms[r].flags & kRoomIdle)) {
			_rooms[r].flags |= kRoomUnloading;
			roomUnload(r);
			_rooms[r].flags &= 0x3fa7;
			_rooms[r].stages = 0;
		}
	}
	_room = -1;
}

// 0x4721a0 -> 0x472610: Ned's room by PtInRect over the exits, then the update walk from it
void Scene3::roomsUpdate(uint32 now) {
	runCallbacks();
	for (int r = 0; r < kRoomCount; r++)
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

// 0x472220: a room became active; the edge on its side takes Ned's room's lock bit
void Scene3::roomEdge(int r) {
	int side = -1;
	for (int i = 0; i < 4; i++) {
		if (_rooms[r].exits[i] == _room) {
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

// 0x472770: loads, polls, ticks and unloads room r, then recurses into its exits
void Scene3::roomUpdate(int r, uint32 now) {
	while (!(_rooms[r].flags & kRoomVisited)) {
		Room &room = _rooms[r];
		if ((_rooms[_room].flags & kRoomActive) && (room.flags & kRoomIdle)) {
			if (roomNeighbour(r) && (room.flags & kRoomIdle)) {
				room.stages = 0;
				room.polled = 0;
				if (roomLoad(r) == 0)
					room.flags = (room.flags & ~kRoomIdle) | kRoomLoading;
			}
		}
		int stages = room.stages;
		if (room.polled < stages || (room.flags & kRoomArrived) || (!(room.flags & kRoomReady) && stages > 0)) {
			if (stages > 0 && roomPoll(r) == 0)
				room.flags = (room.flags & ~kRoomArrived) | kRoomReady;
			if (room.stages == 1 && room.polled == 1 && !(room.flags & kRoomLoading) && roomLoad2(r) == 0)
				room.flags = (room.flags & ~kRoomIdle) | kRoomLoading;
			if (room.flags & kRoomActive)
				roomEdge(r);
		}
		if (room.flags & kRoomReady)
			roomTick(r, now);
		if ((room.flags & kRoomActive) && !(room.flags & (kRoomIdle | kRoomUnloading)) && roomFar(r)) {
			room.flags |= kRoomUnloading;
			roomUnload(r);
			room.flags &= 0x3fa7;
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

// 0x472320 (+0x38 of every room): Ned's room is one of r's exits
bool Scene3::roomNeighbour(int r) {
	for (int i = 0; i < 4; i++) {
		if (_rooms[r].exits[i] == _room)
			return true;
	}
	return false;
}

// 0x472350 (+0x4c of every room): r is neither Ned's room nor next to it, and loaded (or
// aborted); one still loading unloads when done
bool Scene3::roomFar(int r) {
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

// The room table's per-room functions
int Scene3::roomLoad(int r) {
	switch (r) {
	case 0: return r0Load();
	case 1: return r1Load();
	case 2: return r2Load();
	case 3: return r3Load();
	case 4: return r4Load();
	case 5: return r5Load();
	case 6: return r6Load();
	case 7: return r7Load();
	default: return r8Load();
	}
}

// +0x40: the second stage
int Scene3::roomLoad2(int r) {
	static const uint32 lists[] = { 0x4b2730, 0x4b2904, 0x4b2ab8, 0x4b2bb4, 0x4b2dac, 0x4b2fa8, 0x4b30ec, 0x4b3234, 0x4b340c };
	static const int counts[] = { 6, 8, 4, 9, 8, 6, 4, 4, 7 };
	// 0x468a00, 0x469030, 0x469530, 0x4697f0, 0x469bf0, 0x46a020, 0x46a490, 0x46a820, 0x46aca0
	return submit(lists[r], counts[r]);
}

int Scene3::roomPoll(int r) {
	switch (r) {
	case 0: return r0Poll();
	case 1: return r1Poll();
	case 2: return r2Poll();
	case 3: return r3Poll();
	case 4: return r4Poll();
	case 5: return r5Poll();
	case 6: return r6Poll();
	case 7: return r7Poll();
	default: return r8Poll();
	}
}

void Scene3::roomTick(int r, uint32 now) {
	switch (r) {
	case 0:
		r0Tick(now);
		break;
	case 1:
		r1Tick(now);
		break;
	case 2:
		// 0x4697a0
		r2Creature(now);
		break;
	case 3:
		// 0x469b90
		r3CreatureA(now);
		r3CreatureB(now);
		break;
	case 4:
		// 0x469fa0
		r4Creature(now);
		prowler(now, _r4a, 4, 5, 0x127, _r4aHit, _r4Gone, 4);
		break;
	case 5:
		r5Tick(now);
		break;
	case 6:
		r6Tick(now);
		break;
	case 7:
		r7Tick(now);
		break;
	default:
		r8Tick(now);
		break;
	}
}

int Scene3::roomUnload(int r) {
	switch (r) {
	case 0: return r0Unload();
	case 1: return r1Unload();
	case 2: return r2Unload();
	case 3: return r3Unload();
	case 4: return r4Unload();
	case 5: return r5Unload();
	case 6: return r6Unload();
	case 7: return r7Unload();
	default: return r8Unload();
	}
}

// ---- Scene methods ----

// The fields 0x46b760 resets on every visit
void Scene3::clearState() {
	_r8RideOn = _r8RideOff = 0;
	_r6Enter = _r7Enter = _upstairs = 0;
	_passUp = _passDown = _passLeft = _passRight = 0;
	_r3aTouch = _r3bTouch = 0;
	_r3Done = 0;
	_r1Gone = 0;
	_r4Gone = 0;
	_r8Gone = 0;
}

// m0 0x468500 / m1 0x4684d0
int Scene3::init() {
	for (int i = 0; i < 8; i++)
		_saved[i] = 0;
	return 0;
}

void Scene3::shutdown() {
	for (int i = 0; i < 8; i++)
		_saved[i] = 0;
}

// m9 0x468530
void Scene3::saveState(int32 *out) {
	for (int i = 0; i < 8; i++)
		out[i] = _saved[i];
}

// m10 0x468550
void Scene3::restoreState(const int32 *in, int size) {
	for (int i = 0; i < 8; i++)
		_saved[i] = (size == 0x20 && in) ? in[i] : 0;
}

// 0x46b340: an .SST entry of area 4 goes to its slot (mirrored ones by +0x20)
// 0x46b760 m2
int Scene3::load(int fromScene, int fromSub) {
	_frontZ = 9;
	int hintIndex = -1;
	_hintBack = 0;
	_hintAction = 0;
	if (fromScene == 3) {
		_walkIn = 0;
	} else {
		if (!_game->getNaf(kNaf))
			return 5;
		if (fromScene == 6) {
			_walkIn = 1;
		} else {
			_scrollX = 0;
			_scrollY = -0x1e0;
			_r1Creature = _r4a = _r3a = _r3b = _r4b = _r2Creature = _r5Creature = -1;
			_r3aTouch = _r3bTouch = 0;
			_walkIn = 1;
		}
	}
	clearState();
	_cameraBlocked = 0;

	// 0x45a560(2, 999, 1): 97999001.SST, applied by 0x46b340
	_sst.clear();
	Common::String sstName = Common::String::format("%02d%03d0%02d.sst",
		(int16)_game->exe().readUint16(kSstAreas + 2 * kSstAreaIndex), 999, 1);
	Common::ScopedPtr<Common::SeekableReadStream> sst(_game->getEngine()->openFile(sstName));
	if (!sst || !loadPlacements(*sst, _sst)) {
		warning("Can't read %s", sstName.c_str());
		return 5;
	}
	for (const Placement &e : _sst) {
		if (e.area != kArea || e.slot < 0 || e.slot >= kAreaSlots)
			continue;
		Slot &s = slot(e.slot);
		s.x = e.x;
		s.y = e.y;
		if (e.flip)
			s.x += s.w;
	}
	_walkInDoor = 0;
	ned()->listLoad(kNedList);

	if (fromScene == 3) {
		if (fromSub == 0) {
			_nedPos = Common::Point(0x744, 0x1d6);
			ned()->setFacing(0);
			_startRoom = 2;
			hintIndex = 1;
		} else if (fromSub == 1) {
			_nedPos = Common::Point(0x27e, 0x306);
			_startRoom = 0;
			ned()->setFacing(0);
			_hintBack = 1;
			_frontZ = 7;
		}
	} else if (fromScene == 6) {
		_nedPos = Common::Point(0x41a, 0x351);
		ned()->setFacing(1);
		_startRoom = 0;
		_scrollX = -0x352;
		_scrollY = -0x1e0;
	} else {
		nedHide();
		_nedPos.x = 0x1cc;
		_scrollX = -0x104;
		ned()->setFacing(1);
		_startRoom = 0;
	}
	if (submit(kMapList, kMapCount) || submit(kPaletteList, kPaletteCount))
		return 5;
	if (hintIndex != -1)
		hint(hintIndex, ned()->getFacing() != 0);
	roomsEnter(_startRoom);
	return 0;
}

// 0x46b530: the floor maps that are always in the world (2, 7, 4, 14, 13, 12, 15)
int Scene3::addMaps() {
	static const int maps[] = { 2, 7, 4, 14, 13, 12, 15 };
	for (int i : maps)
		addMap(mapAt(i));
	return 0;
}

// m3 0x46ba40
int Scene3::enter(int fromScene, int fromSub) {
	if (fromScene == 0) {
		_game->media().loadClip(3);
		_game->media().playClip(3, 1);
	}
	ned()->listShow(kNedList);
	_world = CollisionWorld();
	_userMap = CollisionMap();
	_targets.clear();
	CollisionMap *map0 = map(0x51f684);
	if (!map0)
		return 5;
	for (const CollisionMap::Region &r : map0->regions) {
		if (r.id == 0x1e61) {
			_region1e61[1] = r.rect.top - 1;
			_region1e61[0] = r.rect.left - 1;
			_region1e61[2] = r.rect.right + 1;
			_region1e61[3] = r.rect.bottom + 1;
			break;
		}
	}
	addMap(map0);
	_world.setUser(&_userMap);
	addMaps();
	nedSlot(4).offsets[0] = 6;
	if (fromSub != 0 && fromSub != 1 && fromScene != 6) {
		for (int y = 0x3bf; y >= 0; y--) {
			if (map0->isSolid(_nedPos.x, y)) {
				_nedPos.y = y;
				break;
			}
		}
	}
	_exit = 0;
	showSprite(0x4b2558, 4);
	switchSlot(0x19a, -1);
	place(0x19a, 0x19a, true);
	ned()->copyPalette(_palette);
	ned()->copyPalette(_paletteAlt);
	_game->display().applyPalette(_palette, kPalAll);
	ned()->setWrap1(true, kWorldW, xform, this);
	ned()->enable(true);
	int waitA, waitB, waitC;
	if (fromScene == 3 && fromSub == 0) {
		waitA = -1;
		waitB = 1;
		waitC = 3;
	} else if (fromScene == 3 && fromSub == 1) {
		ned()->show(false);
		ned()->setCostume(7);
		_behind = 1;
		waitA = -1;
		waitB = 1;
		waitC = -1;
	} else if (fromScene == 6) {
		waitA = -1;
		waitB = 1;
		waitC = -1;
	} else {
		waitA = waitB = waitC = -1;
	}
	ned()->resetIdle();
	nedTick(0, _game->now());
	_game->media().playMusic(kMusic);
	// The original waits for the start room and the ones Ned will see first
	for (int n = 0;; n++) {
		bool ready = true;
		const int wait[] = { waitB, waitC, waitA, _startRoom };
		for (int r : wait) {
			if (r != -1 && !(_rooms[r].flags & kRoomActive))
				ready = false;
		}
		if (ready)
			break;
		if (n == kWaitLimit || _game->getEngine()->shouldQuit()) {
			warning("Scene 3: the start rooms never became active");
			break;
		}
		roomsUpdate(_game->now());
	}
	_game->_optionsAllowed = true;
	// Test hook: nightmarened_s3pos=<x>,<y> puts Ned there (world coordinates) once entered
	if (ConfMan.hasKey("nightmarened_s3pos")) {
		int x = 0, y = 0;
		if (sscanf(ConfMan.get("nightmarened_s3pos").c_str(), "%d,%d", &x, &y) == 2) {
			_nedPos = Common::Point(x, y);
			_walkIn = _walkInDoor = 0;
		}
	}
	return 0;
}

// m4 0x471ca0
int Scene3::leave(int toScene, int toSub) {
	nedSlot(0x7b).offsets[0] = 0;
	stopAllSounds();
	// 0x455ca0 flushes the loader queues: the callbacks of loads still queued never run
	_pendingCallbacks.clear();
	_sst.clear();
	ned()->setWrap1(false, 0, nullptr, nullptr);
	release(kMapList, kMapCount);
	release(kPaletteList, kPaletteCount);
	ned()->show(false);
	ned()->listFree(kNedList);
	roomsLeave();
	_world = CollisionWorld();
	_targets.clear();
	for (int i = 0; i < 16; i++)
		_mapSlots[i] = nullptr;
	_userMap = CollisionMap();
	// The debug overlay must not keep the freed maps (the TV clip draws next)
	_game->display().setCollisionOverlay(Common::Array<CollisionMap *>(), Common::Point());
	nedSlot(0x7b).offsets[4] = 0;
	int clip = -1;
	switch (toScene) {
	case 1:
		clip = 0xe;
		break;
	case 2:
		clip = 0x10;
		break;
	case 3:
		if (toSub == 0)
			clip = 0x13;
		else if (toSub == 1)
			clip = 0x14;
		break;
	case 5:
		clip = 0x11;
		break;
	case 6:
		clip = 0x12;
		break;
	default:
		break;
	}
	if (clip != -1)
		_game->media().loadClip(clip);
	ned()->setCostume(1);
	_upstairs = 0;
	_r8Ride = 0;
	_behind = 0;
	return 0;
}

// m5 0x46be50: the TV clip on the way out (8714..8720tv01; the file name argument is
// unused, the clip comes from the clip table); NED97000 stays open for the sub-scenes
int Scene3::unload(int toScene, int toSub) {
	_game->_optionsAllowed = false;
	int clip = -1;
	if (toScene == 3) {
		if (toSub == 0)
			clip = 0x13;
		else if (toSub == 1)
			clip = 0x14;
	} else {
		_game->closeNaf(kNaf);
		switch (toScene) {
		case 1:
			clip = 0xe;
			break;
		case 2:
			clip = 0x10;
			break;
		case 5:
			clip = 0x11;
			break;
		case 6:
			clip = 0x12;
			break;
		default:
			break;
		}
	}
	if (clip != -1)
		_game->media().playClip(clip, 1);
	return 0;
}

// 0x46ea50, the four passages: Ned walks on hidden behind the walls, 2 px a tick
void Scene3::passages(uint32 now) {
	if (_passUp) {
		_nedPos.x -= 2;
		_nedPos.y = 0x3bf;
		footsteps(now);
		ned()->markTime();
		if (_nedPos.x < 0xd54) {
			_nedPos = Common::Point(0xd54, 0x355);
			_passUp = 0;
			_r6Enter = 1;
		}
	}
	if (_passDown) {
		_nedPos.x += 2;
		_nedPos.y = 0x3bf;
		footsteps(now);
		ned()->markTime();
		if (0xf3c < _nedPos.x && _doors[7] == 0)
			door(_doors[7], map(0x51f684), -1, 9, 0xd);
		if (0xffa < _nedPos.x) {
			_r7Enter = 1;
			_nedPos = Common::Point(0xffa, 0x355);
			_passDown = 0;
		}
	}
	if (_passLeft) {
		_nedPos.x -= 2;
		_nedPos.y = 0x3bf;
		footsteps(now);
		ned()->markTime();
		if (_nedPos.x < 0)
			_nedPos.x = kWrap;
		if (_nedPos.x < 0x1004 && 500 < _nedPos.x && _doors[6] == 0)
			door(_doors[6], map(0x51f684), -1, 8, 0xe);
		if (_nedPos.x < 0xfad && 500 < _nedPos.x) {
			ned()->setCostume(1);
			_behind = 0;
			_nedPos = Common::Point(0xfad, 0x318);
			_passLeft = 0;
			ned()->enable(true);
		}
	}
	if (_passRight) {
		_nedPos.x += 2;
		_nedPos.y = 0x3bf;
		footsteps(now);
		ned()->markTime();
		if (kWrap < _nedPos.x) {
			_nedPos.x = 0;
			ned()->setCostume(7);
			_behind = 1;
			if (_doors[0] == 0)
				door(_doors[0], map(0x51f684), 0x15f, -1, -1);
		}
		if (0xee < _nedPos.x && _nedPos.x < 300) {
			ned()->enable(true);
			_nedPos = Common::Point(0xe3, 0x2d5);
			_passRight = 0;
		}
	}
}

// m7 0x46ea50
int Scene3::tick(uint32 input, uint32 now) {
	// The original sleeps until 41 ms have passed since the last tick
	if (now < _lastTick + kMsPerStep) {
		g_system->delayMillis(MIN<uint32>(_lastTick + kMsPerStep - now, 10));
		return 0;
	}
	_lastTick = now;
	_puff.tick(now, xform, this);
	place(0x19a, 0x19a, true);
	if (_walkInDoor || _walkIn) {
		if (_nedPos.x < 0x281) {
			input = kInputRight;
		} else {
			_walkInDoor = 0;
			_walkIn = 0;
		}
	}
	if (_cameraBlocked) {
		int sx = wrapX(_nedPos.x);
		bool edge = ned()->getFacing() == 0 ? sx <= 0xb3 : 0x1cd <= sx;
		if (edge)
			input &= kInputButton1 | kInputButton2;
	}
	passages(now);
	if (_hintAction && _nedPos.x < 0x1172 && 0x1076 < _nedPos.x) {
		_hintAction = 0;
		hint(3, false);
	}
	if (_hintBack && _nedPos.x < 0xf78 && 0xe7e < _nedPos.x) {
		_hintBack = 0;
		hint(0, false);
	}
	roomsUpdate(now);
	nedTick(input, now);
	_game->display().setCollisionOverlay(overlayMaps(), Common::Point(_scrollX, _scrollY));
	if (ned()->getCurSlot() == 0x8c) {
		// One frame of the zap in the other palette, facing left
		bool flash = nedSlot(0x8c).step == 3;
		if (flash) {
			_game->display().applyPalette(_paletteAlt, kPalAll);
			ned()->setFacing(0);
		}
		_game->display().draw();
		if (flash)
			_game->display().applyPalette(_palette, kPalAll);
	} else {
		_cameraBlocked = camera(now);
		_game->display().draw();
	}
	debugC(3, kDebugGraphics, "Scene 3: input %x Ned %d,%d state %d slot %d room %d scroll %d,%d", input,
		_nedPos.x, _nedPos.y, ned()->getState(), ned()->getCurSlot(), _room, _scrollX, _scrollY);
	if (!_exit)
		return 0;
	int scene = -1, sub = -1;
	switch (_exit) {
	case 3:
		scene = 3;
		sub = 0;
		break;
	case 4:
		ned()->setFacing(1);
		scene = 6;
		break;
	case 5:
		scene = 2;
		break;
	case 6:
		scene = 1;
		break;
	case 7:
		ned()->show(false);
		ned()->setCostume(1);
		_behind = 0;
		scene = 3;
		sub = 1;
		break;
	case 8:
		scene = 5;
		break;
	default:
		break;
	}
	_exit = 0;
	if (scene != -1)
		switchTo(scene, sub);
	return 0;
}

// 0x470e40: the camera keeps Ned near x 200 facing right and 440 facing left and the floor
// he is on in view; it stops while a neighbour room isn't shown yet. Returns that.
int Scene3::camera(uint32 now) {
	int dx = 0, dy = 0;
	int blocked = 0;
	int sx = wrapX(_nedPos.x);
	int y = _nedPos.y;
	int16 state = ned()->getState();
	if (ned()->getFacing() != 0) {
		if (state == 7) {
			dx = -2;
			dy = 2;
		} else if (state == 8) {
			dx = -3;
			dy = -3;
		} else if (state == 10) {
			if (ned()->isState10Ending())
				dx = -0x12;
			if (y < 0x1e0)
				dy = 2;
			else if (y > 0x1e0)
				dy = -2;
		} else {
			if (sx > 200) {
				int d = sx - 200;
				if (d <= 6)
					dx = -1;
				else if (d <= 0xc)
					dx = -2;
				else if (d <= 0x18)
					dx = -4;
				else
					dx = d > 0x30 ? -0x12 : -0xc;
			} else if (state == 1 && _cameraTime + 0x21 < now) {
				dx = -3;
			}
			if (y < 0x1e0)
				dy = 4;
			else if (y > 0x1e0)
				dy = -4;
		}
		if (sx < 0xb4) {
			dx = 0;
			_cameraTime = _game->now();
		}
	} else {
		if (state == 7) {
			dx = 2;
			dy = 2;
		} else if (state == 8) {
			dx = 3;
			dy = -3;
		} else if (state == 10) {
			if (ned()->isState10Ending())
				dx = 0x12;
			if (y < 0x1e0)
				dy = 2;
			else if (y > 0x1e0)
				dy = -2;
		} else {
			if (sx < 0x1b8) {
				int d = 0x1b8 - sx;
				if (d <= 6)
					dx = 1;
				else if (d <= 0xc)
					dx = 2;
				else if (d <= 0x18)
					dx = 4;
				else
					dx = d > 0x30 ? 0x12 : 0xc;
			} else if (state == 1) {
				dx = 3;
			}
			if (y < 0x1e0)
				dy = 4;
			else if (y > 0x1e0)
				dy = -4;
		}
		if (sx > 0x1cc) {
			dx = 0;
			_cameraTime = _game->now();
		}
	}
	if (dx == 0 && dy == 0)
		return 0;

	int room = _room;
	if (room < 0)
		return 0;
	for (int i = 0; i < 4; i++) {
		int e = _rooms[room].exits[i];
		if (e != -1 && !(_rooms[e].flags & kRoomActive)) {
			dx = dy = 0;
			blocked = 1;
			break;
		}
	}
	if (dy > 0 && (_edgeLocks & 0x100)) {
		if (-(dy + _scrollY) < _rooms[room].view[1])
			dy = ABS(_scrollY) - _rooms[room].view[1];
	}
	if (dy < 0 && (_edgeLocks & 0x400)) {
		if (ABS(dy + _scrollY) > _rooms[room].view[3] - 0x1e0)
			dy = ABS(_scrollY) + (0x1e0 - _rooms[room].view[3]);
	}
	if (room == 2) {
		BitmapRec *rec = bmp(0x4e0a74);
		if (rec && rec->object) {
			int bx = rec->object->getPosition().x;
			if (bx + dx > 0)
				dx = -bx;
		}
	} else if (room == 5) {
		BitmapRec *rec = bmp(0x4e0a80);
		if (rec && rec->object) {
			int bx = rec->object->getPosition().x;
			int lim = 0x280 - rec->bitmap.surface.w;
			if (bx + dx < lim)
				dx = lim - bx;
		}
	}
	if (dy + _scrollY > 0)
		dy = -_scrollY;
	_scrollY += dy;
	_scrollX += dx;
	if (_scrollX > 0)
		_scrollX -= kWrap;
	if (_scrollX < -kWrap)
		_scrollX += kWrap;
	// 0x477070: the vision's swirl moves along
	if (_game->hints().isActive())
		_game->hints().nudge(dx, dy);
	cameraRooms(dx, dy);
	nudge(0x19a, dx, dy);
	_puff.nudge(dx, dy);
	ned()->adjPosition(dx, dy);
	if (dx != 0 || dy != 0)
		_cameraTime = _game->now();
	return blocked;
}

// 0x470e40, second half: each shown room's backgrounds follow the scroll, its sprites are nudged.
// A door is nudged as its shut slot while that is shown, otherwise as its animation.
void Scene3::cameraRooms(int dx, int dy) {
	if (_rooms[0].flags & kRoomActive) {
		bmpPlace(0x4e0a68, _rooms[0].view[0], _rooms[0].view[1]);
		nudge(_r0Grabber, dx, dy);
		nudge(isVisible(0x11d) ? 0x11d : 0x15f, dx, dy);
		nudge(isVisible(0x11e) ? 0x11e : 0x165, dx, dy);
		nudge(isVisible(0x120) ? 0x120 : 0x169, dx, dy);
		nudge(0x15e, dx, dy);
		nudge(0x168, dx, dy);
		nudge(0x179, dx, dy);
		place(0x19f, 0x19f, true, nullptr, xformRight);
		place(0x19d, 0x19d, true);
	}
	if (_rooms[1].flags & kRoomActive) {
		bmpPlace(0x4e0a6c, _rooms[1].view[0], _rooms[1].view[1]);
		BitmapRec *first = bmp(0x4e0a6c);
		bmpPlace(0x4e0a70, (first ? first->bitmap.surface.w : 0) + _rooms[1].view[0], _rooms[1].view[1]);
		nudge(_r1Creature, dx, dy);
		nudge(isVisible(0x11f) ? 0x11f : 0x167, dx, dy);
		nudge(isVisible(0x121) ? 0x121 : 0x16b, dx, dy);
		nudge(0x166, dx, dy);
		nudge(0x16a, dx, dy);
	}
	if (_rooms[2].flags & kRoomActive) {
		bmpPlace(0x4e0a74, _rooms[2].view[0], _rooms[2].view[1]);
		nudge(_r2Creature, dx, dy);
	}
	if (_rooms[3].flags & kRoomActive) {
		bmpPlace(0x4e0a78, _rooms[3].view[0], _rooms[3].view[1]);
		nudge(_r3a, dx, dy);
		nudge(_r3b, dx, dy);
	}
	if (_rooms[4].flags & kRoomActive) {
		bmpPlace(0x4e0a7c, _rooms[4].view[0], _rooms[4].view[1]);
		nudge(_r4a, dx, dy);
		nudge(_r4b, dx, dy);
	}
	if (_rooms[5].flags & kRoomActive) {
		bmpPlace(0x4e0a80, _rooms[5].view[0], _rooms[5].view[1]);
		nudge(_r5Creature, dx, dy);
		nudge(_r5Slot, dx, dy);
		nudge(isVisible(0x126) ? 0x126 : 0x175, dx, dy);
		nudge(0x174, dx, dy);
	}
	if (_rooms[6].flags & kRoomActive) {
		bmpPlace(0x4e0a84, _rooms[6].view[0], _rooms[6].view[1]);
		nudge(_r6Creature, dx, dy);
		nudge(isVisible(0x122) ? 0x122 : 0x16d, dx, dy);
	}
	if (_rooms[7].flags & kRoomActive) {
		bmpPlace(0x4e0a88, _rooms[7].view[0], _rooms[7].view[1]);
		nudge(isVisible(0x123) ? 0x123 : 0x16f, dx, dy);
		nudge(isVisible(0x124) ? 0x124 : 0x171, dx, dy);
		nudge(0x16e, dx, dy);
		nudge(0x176, dx, dy);
	}
	if (_rooms[8].flags & kRoomActive) {
		bmpPlace(0x4e0a8c, _rooms[8].view[0], _rooms[8].view[1]);
		nudge(_r8Creature, dx, dy);
		nudge(_r8Slot, dx, dy);
		nudge(0x1a0, dx, dy);
		nudge(isVisible(0x125) ? 0x125 : 0x173, dx, dy);
		nudge(0x172, dx, dy);
	}
}

} // End of namespace NightmareNed
