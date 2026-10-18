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
#include "common/ptr.h"
#include "common/system.h"
#include "common/textconsole.h"

#include "nightmarened/detection.h"
#include "nightmarened/display.h"
#include "nightmarened/game.h"
#include "nightmarened/media.h"
#include "nightmarened/ned.h"
#include "nightmarened/nightmarened.h"
#include "nightmarened/rdx.h"
#include "nightmarened/scene7.h"

namespace NightmareNed {

// Ned's slots that 0x428b40 draws at order 5, in front of the upper jaw
static const int16 kNedFrontSlots[] = {
	142, 144, 145, 146, 328, 52, 419, 23, 24, 25, 268, 269, 270, 271, 272, 273, 274, 275, 276
};

Scene7::Scene7(Game *game) : Scene(game, 7) {
	const ExeImage &exe = _game->exe();
	for (int i = 0; i < 4; i++)
		_jawMarks[i] = (int16)exe.readUint16(0x49e230 + 2 * i);
	for (int i = 0; i < 8; i++)
		_jawAnchors[i] = (int16)exe.readUint16(0x49e238 + 2 * i);
	for (int i = 0; i < 5; i++) {
		_loopSlots[i] = (int16)exe.readUint16(0x49e24a + 2 * i);
		_loopRefs[i] = (int16)exe.readUint16(0x49e258 + 2 * i);
	}
	for (int i = 0; i < 7; i++)
		_jawSlots[i] = (int16)exe.readUint16(0x49e268 + 2 * i);
	for (int i = 0; i < 4; i++)
		_stillSlots[i] = (int16)exe.readUint16(0x49e278 + 2 * i);
	_creatureSlot = (int16)exe.readUint16(0x49e280);
	_jawArmed = exe.readSint32(0x49e284);
	_bugSlot = (int16)exe.readUint16(0x49e288);
	_dropSlot = (int16)exe.readUint16(0x49e28c);
	_monsterSlot = (int16)exe.readUint16(0x49e290);
	_tongueSlot = (int16)exe.readUint16(0x49e294);
	_gumSlot = (int16)exe.readUint16(0x49e298);
	_cameraSettled = exe.readSint32(0x49e29c);
	_creatureNew = exe.readSint32(0x4a0238);
	_bugNew = exe.readSint32(0x4a023c);
	_bugSound = exe.readSint32(0x4a0240);
	_dropNew = exe.readSint32(0x4a0244);
	_monsterNew = exe.readSint32(0x4a0248);
	_liftFirst = exe.readSint32(0x4a024c);
	_tongueLoad = exe.readSint32(0x4a0250);
	_gumLoad = exe.readSint32(0x4a0254);
	_jawOpenMapIn = exe.readSint32(0x4a025c);
	_nedActive = exe.readSint32(0x4a0260);

	_scrollX = _scrollY = 0;
	_unportedExit = -1;
	_bugRegion = 0;
	_jawTime = 0;
	for (int i = 0; i < 12; i++)
		_teeth[i] = _teethY[i] = 0;
	for (int i = 0; i < 3; i++)
		_gates[i] = 0;
	_gumRef = _gumNext = 0;
	for (int i = 0; i < 4; i++)
		_jawMarkY[i] = 0;
	for (int i = 0; i < 8; i++)
		_jawAnchorY[i] = 0;
	for (int i = 0; i < 7; i++)
		_jawSlotY[i] = 0;
	_monsterFlip = _nedHit = 0;
	_tongueRef = 0;
	_saveCopy = 0;
	_popRef = 0;
	_jawOpen = 0;
	_flashTime = _jawShutTime = 0;
	_dropRef = 0;
	_jawY = 0;
	_creatureRegion = 0;
	_monsterRef = 0;
	_anim9 = _pop = 0;
	_hits = 0;
	_lost = _stageReady = _toScene3 = _toScene1 = _toScene2 = _inputLock = 0;
	_toRightLip = _toLeftLip = _armed = _cameraLock = _lift = _flashOn = 0;
	_monsterGone = _creatureDone = _unusedFlag = _dropAfterPop = _leftLipDone = 0;
	_stage = _bugIndex = 0;
	_bugArmed = 0;
	_bugTime = 0;
	_dropFloorKnown = 0;
	_dropFloorY = 0;
	_dropBoxY = 0;
	_monsterPlaced = _monsterFloorY = 0;
	_jawStepTime = 0;
	_jawMoving = _climbDown = 0;
	_lastDraw = 0;
	_climbTicks = 0;
	_lastScreenX = _lastScreenY = 0;
	_scrollTime = 0;
	for (int i = 0; i < 4; i++)
		_spit[i] = 0;
	memset(_palette, 0, sizeof(_palette));
	memset(_paletteFlash, 0, sizeof(_paletteFlash));

	_hooks.user = this;
	_hooks.use = hookUse;
	_hooks.region = hookRegion;
	_hooks.findTarget = hookFindTarget;
	_hooks.hitTarget = hookHitTarget;
}

Scene7::~Scene7() {
	freeAll();
}

// ---- Resources ----

Slot &Scene7::slot(int id) {
	return _game->res().getSlot(kArea, CLIP(id, 0, (int)kAreaSlots - 1));
}

void Scene7::visible(int id, bool on) {
	if (id >= 0 && id < kAreaSlots && slot(id).sprite)
		slot(id).sprite->setVisibility(on);
}

BitmapRec *Scene7::bmp(uint32 va) {
	return _bitmaps.contains(va) ? _bitmaps[va] : nullptr;
}

CollisionMap *Scene7::map(uint32 va) {
	return _maps.contains(va) ? _maps[va] : nullptr;
}

// 0x4586e0 (synchronous): every entry loads now; the completion callback runs on the next tick
int Scene7::submit(uint32 va, int count, int scene) {
	NafArchive *naf = _game->getNaf(kNafMouth);
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
			if (req.hasPalette && req.paletteVA == kPaletteGame)
				memcpy(_palette, req.palette, sizeof(_palette));
			if (req.hasPalette && req.paletteVA == kPaletteFlash)
				memcpy(_paletteFlash, req.palette, sizeof(_paletteFlash));
			_entries[e] = req;
		}
		uint32 cb = exe.readUint32(e + 0x1c);
		if (cb)
			_pendingCallbacks.push_back(cb);
	}
	return 0;
}

// 0x458dc0: frees backwards
void Scene7::release(uint32 va, int count) {
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
}

// 0x4288d0, the only list callback: the next loading stage may start
void Scene7::runCallbacks() {
	while (!_pendingCallbacks.empty()) {
		uint32 cb = _pendingCallbacks.front();
		_pendingCallbacks.remove_at(0);
		if (cb == kListCallback) {
			_stageReady = 1;
			_stage++;
		}
	}
}

void Scene7::freeAll() {
	_pendingCallbacks.clear();
	Common::Array<uint32> keys;
	for (EntryMap::iterator it = _entries.begin(); it != _entries.end(); ++it)
		keys.push_back(it->_key);
	Common::sort(keys.begin(), keys.end());
	for (int i = (int)keys.size() - 1; i >= 0; i--)
		release(keys[i], 1);
}

// 0x434370: the list's sprite entry for id, or 0
uint32 Scene7::entryFor(uint32 list, int count, int id) const {
	const ExeImage &exe = _game->exe();
	for (int i = 0; i < count; i++) {
		uint32 e = list + i * 0x24;
		if (exe.readSint32(e + 0x0c) == id && exe.readSint32(e + 0x10) == Request::kSprite)
			return e;
	}
	return 0;
}

// 0x434370 + 0x459090: the list's sprite entry for id gets draw order z on the primary surface
void Scene7::show(uint32 list, int count, int id, int z, bool on) {
	uint32 e = entryFor(list, count, id);
	if (!e || !isLoaded(e))
		return;
	Slot &s = _game->res().getSlot(_game->exe().readSint32(e + 0x08), id);
	if (s.sprite) {
		s.sprite->setDrawOrder((int16)z);
		s.sprite->setDestination(_game->display().getPrimary());
		s.sprite->setVisibility(on);
	}
}

// 0x459240: hides everything of the list that is shown and remembers it, or shows it again
void Scene7::saveVisibility(uint32 list, int count, bool restore, Common::Array<int> &state) {
	state.resize(count);
	for (int i = count - 1, k = 0; i >= 0; i--, k++) {
		uint32 e = list + i * 0x24;
		if (!isLoaded(e))
			continue;
		const Request &req = _entries[e];
		RdxObject *obj = nullptr;
		if (req.type == Request::kSprite && req.area >= 0 && req.area < kAreaCount)
			obj = _game->res().getSlot(req.area, req.id).sprite;
		else if (req.type == Request::kBitmap && req.bitmap)
			obj = req.bitmap->object;
		if (!obj)
			continue;
		if (!restore) {
			state[k] = obj->getVisibility();
			if (state[k])
				obj->setVisibility(false);
		} else if (state[k]) {
			obj->setVisibility(true);
		}
	}
}

// ---- Display helpers (area 5) ----

// 0x430890
int16 Scene7::switchSlot(int newSlot, int oldSlot) {
	return (int16)_game->display().slotSwitch(kArea, (int16)newSlot, (int16)oldSlot);
}

// 0x430930
int16 Scene7::switchSlotPre(int newSlot, int oldSlot) {
	return (int16)_game->display().slotSwitch(kArea, (int16)newSlot, (int16)oldSlot, true);
}

// 0x430850 with the scroll xform 0x42f110: returns the step's duration; outPos is the sprite position
int Scene7::place(int id, int ref, bool noMirror, int *outFrame, Common::Point *outPos) {
	int frame = 0, duration = 0;
	if (id < 0 || id >= kAreaSlots || ref < 0 || ref >= kAreaSlots)
		return 0;
	_game->display().slotPlace(kArea, id, ref, &frame, &duration, noMirror, Common::Point(_scrollX, _scrollY));
	if (outFrame)
		*outFrame = frame;
	if (outPos && slot(id).sprite)
		*outPos = slot(id).sprite->getPosition();
	return duration;
}

// Places a slot against ref and sets its next deadline (the pattern after every step)
void Scene7::advance(int id, int ref, bool noMirror, uint32 now) {
	int duration = place(id, ref, noMirror);
	slot(id).deadline = now + (int16)duration * kMsPerStep;
}

// objAdjPosition on a slot's sprite
void Scene7::adj(int id, int dx, int dy) {
	if (id >= 0 && id < kAreaSlots && slot(id).sprite)
		slot(id).sprite->adjPosition(dx, dy);
}

// 0x430440 slot_attach_pos: where slot would be placed against ref's anchor
void Scene7::attachPos(int id, int ref, int out[2], bool noMirror) {
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

// 0x4860d0 on a slot's hit boxes
void Scene7::boxRect(int id, int frame, int k, int32 out[4]) {
	Slot &s = slot(id);
	if (s.boxes)
		s.boxes->getRawRect(frame, k, out);
	else
		out[0] = out[1] = out[2] = out[3] = -0x7fff;
}

// 0x486930 on the slot's current step; clamp keeps the step inside the sequence
int Scene7::currentFrame(int id, bool clamp) {
	Slot &s = slot(id);
	int step = s.step;
	if (clamp) {
		if (step == -1)
			step = 0;
		if (s.sequenceLength <= step)
			step = s.sequenceLength - 1;
	}
	int frame = 0, duration = 0;
	if (s.sequence)
		s.sequence->getStep(step, frame, duration);
	return frame;
}

// objApplyEffect(FX_HORIZ_FLIP) when the slot has none, then fxSetActive
void Scene7::applyFlip(int id, bool active) {
	Slot &s = slot(id);
	if (!s.sprite)
		return;
	if (!s.flip)
		s.flip = s.sprite->applyEffect(kFxHorizFlip);
	s.flip->active = active;
}

// objApplyEffect(FX_HORIZ_FLIP) when the slot has none (a new effect is active)
void Scene7::ensureFlip(int id) {
	Slot &s = slot(id);
	if (s.sprite && !s.flip)
		s.flip = s.sprite->applyEffect(kFxHorizFlip);
}

// IntersectRect
bool Scene7::intersect(const int32 a[4], const int32 b[4]) {
	return MAX(a[0], b[0]) < MIN(a[2], b[2]) && MAX(a[1], b[1]) < MIN(a[3], b[3]);
}

// ---- Sound ----

// 0x455880 snd_play
void Scene7::playSound(int id, int stop) {
	_game->playSlotSound(slot(id), stop >= 0 ? &slot(stop) : nullptr);
}

// 0x455930
void Scene7::stopSound(int id) {
	_game->stopSlotSound(slot(id));
}

// 0x455950 -> 0x44d020: every slot voice stops; the streamed music plays on
void Scene7::stopAllSounds() {
	for (int a = 0; a < kAreaCount; a++) {
		for (int i = 0; i < kAreaSlots; i++)
			_game->stopSlotSound(_game->res().getSlot(a, i));
	}
}

// ---- Collision ----

// 0x487f50 + 0x4872a0
void Scene7::addMap(uint32 va) {
	CollisionMap *m = map(va);
	if (m)
		_world.addMap(m, m->bounds.left, m->bounds.top, m->bounds.right, m->bounds.bottom);
}

// 0x487320
void Scene7::removeMap(uint32 va) {
	CollisionMap *m = map(va);
	if (m)
		_world.removeMap(m);
}

// 0x487f50 + 0x487fc0 + 0x4878d0: the world bounds move only while the map is in the world;
// the regions always do
void Scene7::moveMapBy(uint32 va, int dy) {
	CollisionMap *m = map(va);
	if (!m)
		return;
	Common::Rect r = m->bounds;
	r.top += dy;
	r.bottom += dy;
	_world.moveMap(m, r);
	regionShift(m, 0, dy);
}

// 0x487c50 bc_solid_at on one map
bool Scene7::solidAt(uint32 va, int x, int y) {
	CollisionMap *m = map(va);
	return m && m->isSolid((int16)x, (int16)y);
}

// 0x488060: bit 31 of the first region with the id is the disabled flag
void Scene7::regionEnable(CollisionMap *m, int32 id, bool on) {
	if (!m)
		return;
	for (CollisionMap::Region &r : m->regions) {
		if (r.id != id)
			continue;
		if (on)
			r.type &= 0x7fffffff;
		else
			r.type |= 0x80000000;
		return;
	}
}

// 0x4877f0
void Scene7::regionSetRect(CollisionMap *m, int32 id, const int32 rect[4]) {
	if (!m)
		return;
	for (CollisionMap::Region &r : m->regions) {
		if (r.id == id) {
			r.rect.left = rect[0];
			r.rect.top = rect[1];
			r.rect.right = rect[2];
			r.rect.bottom = rect[3];
			return;
		}
	}
}

// 0x4878d0
void Scene7::regionShift(CollisionMap *m, int dx, int dy) {
	if (!m)
		return;
	for (CollisionMap::Region &r : m->regions) {
		r.rect.left += dx;
		r.rect.top += dy;
		r.rect.right += dx;
		r.rect.bottom += dy;
	}
}

// 0x487ba0: drops the solid mask and zeroes the bounds; the regions stay
void Scene7::clearMask(CollisionMap *m) {
	if (!m)
		return;
	m->mask.clear();
	m->maskStride = 0;
	m->maskBounds = Common::Rect();
	m->bounds = Common::Rect();
}

// 0x487600 bc_add_rect on the scene's own region map; returns the new region's id
int32 Scene7::addRegion(const int32 rect[4], uint32 type, int32 id) {
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
void Scene7::removeRegion(int32 id) {
	for (uint i = 0; i < _userMap.regions.size(); i++) {
		if (_userMap.regions[i].id == id) {
			_userMap.regions.remove_at(i);
			break;
		}
	}
	_world.setUser(&_userMap);
}

Common::Array<CollisionMap *> Scene7::overlayMaps() const {
	Common::Array<CollisionMap *> maps;
	for (int i = 0; i < _world.getMapCount(); i++)
		maps.push_back(_world.getMap(i));
	maps.push_back(const_cast<CollisionMap *>(&_userMap));
	return maps;
}

// ---- Targets ----

// 0x4309c0 olist_add_sprite (always with noFlip = 1 here)
void Scene7::targetAdd(int id) {
	Placement p;
	memset(&p, 0, sizeof(p));
	p.area = kArea;
	p.slot = (int16)id;
	p.sub = 1;
	p.kind = 2;
	p.flip = 0;
	_targets.push_back(p);
}

// 0x486f50 olist_remove (its index loop skips the entry after each removal)
void Scene7::targetRemove(int id) {
	for (uint i = 0; i < _targets.size(); i++) {
		if (_targets[i].area == kArea && _targets[i].slot == (int16)id && _targets[i].sub == 1)
			_targets.remove_at(i);
	}
}

// ---- Ned ----

Ned *Scene7::ned() {
	return _game->ned();
}

// 0x454920 with the scene's world, scroll and hooks
void Scene7::nedTick(uint32 input, uint32 now) {
	int f = 0;
	ned()->tick(_world, input, f, _nedPos, now, _scrollX, _scrollY, &_hooks);
}

// 0x42f110: world -> screen
void Scene7::xform(void *user, int &x, int &y) {
	Scene7 *s = (Scene7 *)user;
	x += s->_scrollX;
	y += s->_scrollY;
}

// ---- Calls into modules that are not ported ----

// 0x475180: the help hints (Ned's thought balloons) belong to the hints module
void Scene7::hint(int which) {
	debugC(1, kDebugGraphics, "Scene 7: hint %d (0x475180) not ported", which);
}

// 0x475970: whether a hint is showing (DAT_0051f760 == 0x472a30); never, until hints exist
bool Scene7::hintActive() const {
	return false;
}

// 0x479ff0 + 0x47aa00: a TV clip at its own rate without music, not ported
void Scene7::playClipNoMusic(int clip) {
	debugC(1, kDebugVideo, "Scene 7: TV clip %d (0x47aa00) not ported", clip);
}

// 0x45a0f0, staying here when the target isn't registered yet
int Scene7::switchTo(int scene) {
	if (!_game->hasScene(scene, -1)) {
		if (_unportedExit != scene)
			debugC(1, kDebugLoading, "Scene 7: exit to scene %d (not ported), staying", scene);
		_unportedExit = scene;
		return 1;
	}
	_unportedExit = -1;
	_game->switchScene(scene, -1);
	return 0;
}

// ---- Scene methods ----

// The fields 0x428b40 resets on every visit
void Scene7::clearState() {
	_creatureDone = _lost = _toScene3 = _toScene1 = _toScene2 = _toRightLip = _toLeftLip = 0;
	_inputLock = _dropAfterPop = _cameraLock = 0;
	_cameraSettled = 1;
	_stage = 0;
	_stageReady = 0;
	_gumSlot = _monsterSlot = _bugSlot = _dropSlot = _creatureSlot = -1;
	_unusedFlag = 0;
	_leftLipDone = 0;
	for (int i = 0; i < 7; i++)
		_jawSlots[i] = (int16)(0xd3 + i);
}

// m0 0x429b00 / m1 0x42f3d0
int Scene7::init() {
	_saveCopy = 0;
	_armed = 0;
	return 0;
}

void Scene7::shutdown() {
	_saveCopy = 0;
	_armed = 0;
}

// m9 0x42f380
void Scene7::saveState(int32 *out) {
	_saveCopy = _armed;
	out[0] = _armed;
}

// m10 0x42f3a0
void Scene7::restoreState(const int32 *in, int size) {
	if (size == 4 && in) {
		_armed = in[0];
		return;
	}
	_saveCopy = 0;
	_armed = 0;
}

// m2 0x428ad0
int Scene7::load(int fromScene, int fromSub) {
	ned()->listLoad(kNedList);
	if (!_game->getNaf(kNafMouth))
		return 5;
	_game->media().loadClip(0x2d);
	return submit(kMainList, kMainCount, 0);
}

// m3 0x428b40
int Scene7::enter(int fromScene, int fromSub) {
	ned()->setFacing(1);
	ned()->listShow(kNedList);
	clearState();
	regionTick(true);
	spitTick(true);
	bugTick(0, true);
	dropTick(0, true);
	creatureTick(0, true);
	for (int16 id : kNedFrontSlots) {
		Slot &s = _game->res().getSlot(1, id);
		if (s.sprite)
			s.sprite->setDrawOrder(5);
	}
	_game->media().playClip(0x2d, 1);
	if (!isLoaded(kMainList))
		return 5;
	ned()->copyPalette(_palette);
	_game->display().applyPalette(_palette, kPalGame);

	// The world: the lower jaw, the open upper jaw, the two gates and the scene's own regions
	_world = CollisionWorld();
	_userMap = CollisionMap();
	_targets.clear();
	addMap(kMapFloor);
	addMap(kMapJawOpen);
	_world.setUser(&_userMap);
	addMap(kMapGateA);
	addMap(kMapGateB);
	moveMapBy(kMapJaw, kJawOpenY);    // not in the world yet: only its regions move
	regionEnable(map(kMapFloor), 0x186, false);
	regionEnable(map(kMapFloor), 0x2ee, false);
	regionEnable(map(kMapFloor), 0x2f8, false);
	ned()->resetIdle();
	_nedPos = Common::Point(200, 200);
	_scrollX = _scrollY = 0;
	_game->res().getSlot(1, 4).offsets[0] = 6;
	ned()->setFacing(1);
	ned()->setWrap1(false, kWorldW, xform, this);
	for (int y = 0x3bf; y >= 0; y--) {
		if (solidAt(kMapFloor, _nedPos.x, y)) {
			_nedPos.y = y;
			break;
		}
	}
	// Regions 0x3fb (the bug) and 0x419 (the creature) start with gate B's bounds
	CollisionMap *gateB = map(kMapGateB);
	int32 rect[4] = { 0, 0, 0, 0 };
	if (gateB) {
		rect[0] = gateB->bounds.left;
		rect[1] = gateB->bounds.top;
		rect[2] = gateB->bounds.right;
		rect[3] = gateB->bounds.bottom;
	}
	_bugRegion = addRegion(rect, 5, 0x3fb);
	_creatureRegion = addRegion(rect, 5, 0x419);
	stripStream(kUpperList, 0x3c, kUpperBitmaps);
	stripStream(kLowerList, 0x3d, kLowerBitmaps);

	// 0x45a560(6, 999, 1): 93999001.SST, applied by 0x428a20
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

	// The teeth with cavities, the gates, and the things around them
	for (int i = 0; i < 10; i++) {
		show(kMainList, kMainCount, 0x8c + i, 6, false);
		_teeth[i] = switchSlot(0x8c + i, -1);
		_teethY[i] = (int16)slot(_teeth[i]).y;
		targetAdd(0x8c + i);
	}
	show(kMainList, kMainCount, 0xc0, 6, true);
	_teeth[10] = 0xc0;
	_teethY[10] = (int16)slot(0xc0).y;
	targetAdd(0xc0);
	show(kMainList, kMainCount, 0xc1, 6, true);
	_teeth[11] = 0xc1;
	_teethY[11] = (int16)slot(0xc1).y;
	targetAdd(0xc1);
	for (int i = 0; i < 2; i++) {
		show(kMainList, kMainCount, 0x96 + i, 0x96, false);
		_gates[i] = switchSlot(0x96 + i, -1);
		targetAdd(0x96 + i);
	}
	show(kMainList, kMainCount, 0xdd, 0x96, true);
	_gates[2] = 0xdd;
	targetAdd(0xdd);
	for (int16 id : _teeth) {
		if (id != -1)
			place(id, id, true);
	}
	for (int16 id : _gates) {
		if (id != -1)
			place(id, id, true);
	}
	for (int i = 0; i < 4; i++)
		show(kMainList, kMainCount, 0xe + i, 0x96, false);
	show(kMainList, kMainCount, 0xd0, 0x96, true);
	show(kMainList, kMainCount, 0xd1, 0x96, true);
	show(kMainList, kMainCount, 0xd2, 0x96, true);
	visible(0x11, true);
	visible(0xf, true);
	visible(0xe, false);
	visible(0x10, false);
	loopTick(0);
	for (int i = 0; i < 8; i++)
		_jawAnchorY[i] = (int16)slot(0xc6 + i).y;
	for (int i = 0; i < 2; i++)
		_jawMarkY[i] = (int16)slot(0xc2 + i).y;
	_jawMarkY[2] = (int16)slot(0xce).y;
	_jawMarkY[3] = (int16)slot(0xcf).y;
	for (int i = 0; i < 7; i++) {
		show(kMainList, kMainCount, 0xd3 + i, 4, false);
		_jawSlotY[i] = (int16)slot(0xd3 + i).y;
	}
	show(kMainList, kMainCount, 0x18, 0x96, true);
	show(kMainList, kMainCount, 0xda, 0x96, true);
	show(kMainList, kMainCount, 0xdb, 0x96, true);
	show(kMainList, kMainCount, 0xdc, 0x96, true);
	for (int16 id : _stillSlots) {
		if (id != -1)
			place(id, id, true);
	}
	show(kMainList, kMainCount, 0xdf, 5, false);
	show(kMainList, kMainCount, 0xe0, 5, false);
	place(0xdf, 0xdf, true);
	place(0xe0, 0xe0, true);
	nedTick(0, 0);
	camera(0);
	if (stripPlace(kUpperList, kUpperBitmaps, 0, 6, 6, kJawOpenY) || stripPlace(kLowerList, kLowerBitmaps, 0, 6, 200, 0))
		return 5;

	// The lists that load in stages behind the level (0x4288d0 moves the stages on)
	if (submit(kBugList, 9, 0) || submit(kSlot12List, 6, 0) || submit(kEnemy0List, 8, 0))
		return 5;
	_game->media().playMusic(kMouthMusic);
	if (!isLoaded(entryFor(kEnemy0List, 8, 7)))
		return 5;
	_jawTime = _game->now() + 30000;
	flash(0, true);
	_jawY = kJawOpenY;
	moveMapBy(kMapJawOpen, kJawOpenY);
	_jawOpen = 1;
	ned()->show(true);
	ned()->enable(true);
	_hits = 0;
	_game->_optionsAllowed = true;
	return 0;
}

// m4 0x429960
int Scene7::leave(int toScene, int toSub) {
	stopAllSounds();
	// 0x455ca0 flushes the loader queues: the callbacks of loads still queued never run
	_pendingCallbacks.clear();
	_world = CollisionWorld();
	_targets.clear();
	_userMap = CollisionMap();
	ned()->setWrap1(false, 0, nullptr, nullptr);
	_sst.clear();
	ned()->restoreIdle();
	ned()->show(false);
	ned()->enable(false);
	ned()->resetIdle();
	ned()->listFree(kNedList);
	release(kMainList, kMainCount);
	release(kUpperList, kStripCount);
	release(kLowerList, kStripCount);
	release(kBugList, 9);
	release(kSlot12List, 6);
	release(kSlot8List, 4);
	release(kSlot35List, 2);
	release(kEnemy0List, 8);
	release(kEnemy1List, 5);
	release(kEnemy2List, 5);
	release(kEnemy3List, 5);
	if (toScene == 1)
		_game->media().loadClip(0x15);
	else if (toScene == 3)
		_game->media().loadClip(0x17);
	return 0;
}

// m5 0x429b10: 0x47a6b0 plays the TV clip with a voice track instead of the music
int Scene7::unload(int toScene, int toSub) {
	_game->closeNaf(kNafMouth);
	if (toScene == 1)
		debugC(1, kDebugVideo, "Scene 7: TV clip 0x15 with 87210001.wav (0x47a6b0) not ported");
	else if (toScene == 3)
		debugC(1, kDebugVideo, "Scene 7: TV clip 0x17 with 87230001.wav (0x47a6b0) not ported");
	return 0;
}

// 0x428900: every 19 s the cavities light up for 2 s in the other palette
void Scene7::flash(uint32 now, bool init) {
	if (init) {
		_flashOn = 0;
		_flashTime = _game->now() + 19000;
		return;
	}
	if (_flashTime < now) {
		for (int16 id : _jawSlots) {
			if (id != -1)
				visible(id, true);
		}
		_game->display().applyPalette(_paletteFlash, kPalGame);
		_flashTime = _game->now() + 19000;
		_flashOn = 1;
		playSound(0xf9);
	}
	if (_flashTime - 17000 < now) {
		for (int16 id : _jawSlots) {
			if (id != -1)
				visible(id, false);
		}
		ned()->copyPalette(_palette);
		_game->display().applyPalette(_palette, kPalGame);
		_flashOn = 0;
	}
}

// 0x42ea70 / 0x42eb40: the strips within 6 of Ned's are loaded, the others freed
void Scene7::stripStream(uint32 list, int scene, uint32 dest) {
	int center = _nedPos.x / kStripW;
	int last = MIN(center + 6, 0x34);
	int first = MAX(center - 6, 0);
	for (int i = 0; i < kStripCount; i++) {
		uint32 e = list + i * 0x24;
		if (i < first || last < i) {
			if (bmp(dest + 4 * i))
				release(e, 1);
		} else if (!bmp(dest + 4 * i)) {
			submit(e, 1, scene);
		}
	}
}

// 0x42f130 / 0x42f250: the loaded strips first..last are placed at the scroll (y + dy) at order z
int Scene7::stripPlace(uint32 list, uint32 dest, int first, int last, int z, int dy) {
	for (int i = 0; i < kStripCount; i++) {
		BitmapRec *rec = bmp(dest + 4 * i);
		if (!rec || !rec->object)
			continue;
		if (i < first || last < i) {
			rec->object->setVisibility(false);
			continue;
		}
		rec->x = _scrollX + i * kStripW;
		rec->y = _scrollY;
		rec->object->setDestination(_game->display().getPrimary());
		rec->object->setPosition(rec->x, dy + _scrollY);
		rec->object->setVisibility(true);
		rec->object->setDrawOrder(z);
	}
	return 0;
}

// 0x42ec10: keeps Ned near x 200 facing right and 440 facing left (320 hanging), clamped
// to the level; everything not placed through the xform this tick moves with the scroll
int Scene7::camera(uint32 now) {
	int sx = _scrollX + _nedPos.x;
	int sy = _nedPos.y + _scrollY;
	int dx = 0;
	int16 state = ned()->getState();
	bool tail = false;
	int d = 0;
	if (!ned()->isHanging()) {
		if (ned()->getFacing() == 0) {
			if (state == 1 ? 0x1b7 < sx : 0x1b7 < sx) {
				if (state == 1 && _scrollTime + 0x29 < now)
					dx = 1;
			} else {
				d = 0x1b8 - sx;
				if (d < 7)
					dx = 1;
				else if (d < 0xd)
					dx = 2;
				else if (d < 0x19)
					dx = 4;
				else
					dx = 0x30 < d ? 0x12 : 0xc;
			}
		} else {
			if (sx < 0xc9) {
				if (state == 1 && _scrollTime + 0x29 < now)
					dx = -1;
			} else {
				d = sx - 200;
				if (d < 7)
					dx = -1;
				else
					tail = true;
			}
		}
	} else {
		if (sx < _lastScreenX) {
			// The state 0x148 test can never pass (there are 23 states)
			if (state == 0x148 ? 0x13f < sx : 0x13f < sx) {
				if (state == 0x148 && _scrollTime + 0x29 < now)
					dx = 1;
			} else {
				d = 0x140 - sx;
				if (d < 7)
					dx = 1;
				else if (d < 0xd)
					dx = 2;
				else if (d < 0x19)
					dx = 4;
				else
					dx = 0x30 < d ? 0x12 : 0xc;
			}
		} else {
			if (sx < 0x141) {
				if (state == 0x148 && _scrollTime + 0x29 < now)
					dx = -1;
			} else {
				d = sx - 0x140;
				if (d < 7)
					dx = -1;
				else
					tail = true;
			}
		}
	}
	if (tail) {
		if (d < 0xd)
			dx = -2;
		else if (d < 0x19)
			dx = -4;
		else
			dx = 0x30 < d ? -0x12 : -0xc;
	}
	if ((state == 10 && !ned()->isState10Ending()) || _cameraLock)
		dx = 0;

	int scroll = dx + _scrollX;
	if (scroll < -0x1ea0) {
		scroll = -0x1ea0;
		dx = -0x1ea0 - _scrollX;
	} else if (scroll > 0) {
		// The original's clamp sets dx to the old scroll, not its negation
		scroll = 0;
		dx = _scrollX;
	}
	int first = -scroll / kStripW;
	int last = MIN(first + 6, 0x34);
	_scrollX = scroll;
	int r = stripPlace(kUpperList, kUpperBitmaps, first, last, 6, _jawY);
	if (r)
		return r;
	teethPlace(dx, 0);
	if (_dropSlot != -1) {
		adj(_dropSlot, dx, 0);
	} else {
		for (int i = 0; i < 7; i++) {
			if (_jawSlots[i] == -1)
				continue;
			slot(_jawSlots[i]).y = _jawSlotY[i] + _jawY;
			place(_jawSlots[i], _jawSlots[i], true);
		}
	}
	r = stripPlace(kLowerList, kLowerBitmaps, first, last, 200, 0);
	if (r)
		return r;
	ned()->adjPosition(dx, 0);
	jawMarksPlace();
	for (int16 id : _loopSlots) {
		if (id != -1)
			adj(id, dx, 0);
	}
	if (_creatureSlot != -1)
		adj(_creatureSlot, dx, 0);
	if (_bugSlot != -1)
		adj(_bugSlot, dx, 0);
	if (_gumSlot != -1)
		adj(_gumSlot, dx, 0);
	if (_monsterSlot != -1)
		adj(_monsterSlot, dx, 0);
	if (_tongueSlot != -1) {
		adj(_tongueSlot, dx, 0);
	} else {
		for (int16 id : _stillSlots)
			adj(id, dx, 0);
	}
	adj(0xdf, dx, 0);
	adj(0xe0, dx, 0);
	_lastScreenX = sx;
	_lastScreenY = sy;
	if (dx == 0) {
		_cameraSettled = 1;
		return 0;
	}
	_scrollTime = g_system->getMillis();
	_cameraSettled = 0;
	return 0;
}

// 0x42f9c0: a TV clip between the level's frames (the lips: clips 0x19 and 0x1a); refloor
// drops Ned onto the floor below again afterwards
void Scene7::clipBreak(int clip, bool refloor) {
	_game->media().stopMusic();
	RdxSurface *primary = _game->display().getPrimary();
	primary->setClearColor(0);
	_game->display().draw();
	primary->setClearColor(-1);
	Common::Array<int> upper, lower, main, bug, gum;
	saveVisibility(kUpperList, kStripCount, false, upper);
	saveVisibility(kLowerList, kStripCount, false, lower);
	saveVisibility(kMainList, kMainCount, false, main);
	saveVisibility(kBugList, 9, false, bug);
	saveVisibility(kSlot12List, 6, false, gum);
	playClipNoMusic(clip);
	saveVisibility(kUpperList, kStripCount, true, upper);
	saveVisibility(kLowerList, kStripCount, true, lower);
	saveVisibility(kMainList, kMainCount, true, main);
	saveVisibility(kBugList, 9, true, bug);
	saveVisibility(kSlot12List, 6, true, gum);
	ned()->resetIdle();
	nedTick(0, 0);
	ned()->copyPalette(_palette);
	_game->display().applyPalette(_palette, kPalGame);
	if (refloor) {
		for (int y = 0x3bf; y >= 0; y--) {
			if (solidAt(kMapFloor, _nedPos.x, y)) {
				_nedPos.y = y;
				break;
			}
		}
	}
	_game->media().playMusic(kMouthMusic);
}

// m7 0x42cf90
int Scene7::tick(uint32 input, uint32 now) {
	// The original runs when now >= last + 41 but draws only when now > last + 41, so within
	// that one millisecond it reruns undrawn ticks (and the camera) as often as the CPU allows;
	// here the tick waits for the drawn one
	if (now <= _lastDraw + kMsPerStep) {
		g_system->delayMillis(MIN<uint32>(_lastDraw + kMsPerStep + 1 - now, 10));
		return 0;
	}
	if (0x1af4 < _nedPos.x && !hintActive() && _creatureDone)
		_toScene2 = 1;
	creatureTick(now, false);
	bugTick(now, false);
	anim9Tick(now);
	popTick(now);
	dropTick(now, false);
	monsterTick(now);
	liftTick(now);
	regionTick(false);
	loopTick(now);
	gumTick(now);
	for (int16 id : _stillSlots) {
		if (id != -1)
			place(id, id, true);
	}
	tongueTick(now);
	spitTick(false);
	hitChecks();
	if (0x1af4 < _nedPos.x && _flashOn)
		flash(now, false);
	if (_inputLock)
		input &= kInputButton2;
	if (_nedActive)
		nedTick(input, now);
	stripStream(kLowerList, 0x3d, kLowerBitmaps);
	stripStream(kUpperList, 0x3c, kUpperBitmaps);

	// The staged lists: each one's callback (0x4288d0) lets the next one load
	runCallbacks();
	if (_stageReady) {
		switch (_stage) {
		case 1:
			_stageReady = 0;
			break;
		case 2:
			if (submit(kEnemy1List, 5, 0))
				return 5;
			_stageReady = 0;
			break;
		case 3:
			if (submit(kEnemy2List, 5, 0))
				return 5;
			_stageReady = 0;
			break;
		case 4:
			if (submit(kEnemy3List, 5, 0))
				return 5;
			_stageReady = 0;
			break;
		case 5:
			if (0x1af4 < _nedPos.x) {
				if (submit(kSlot8List, 4, 0))
					return 5;
				_stageReady = 0;
			}
			break;
		case 6:
			if (0x1edc < _nedPos.x) {
				if (submit(kSlot35List, 2, 0))
					return 5;
				_stageReady = 0;
			}
			break;
		default:
			break;
		}
	}

	int r = camera(now);
	if (r)
		return r;
	if (_lastDraw + kMsPerStep < now) {
		_game->display().setCollisionOverlay(overlayMaps(), Common::Point(_scrollX, _scrollY));
		_game->display().draw();
		_lastDraw = now;
	}
	debugC(3, kDebugGraphics, "Scene 7: input %x Ned %d,%d state %d slot %d scroll %d jaw %d hits %d", input,
		_nedPos.x, _nedPos.y, ned()->getState(), ned()->getCurSlot(), _scrollX, _jawY, _hits);

	int16 cur = ned()->getCurSlot();
	if (_lost && cur != 0x98 && cur != 0x36 && _monsterGone) {
		if (_unportedExit != 0)
			debugC(1, kDebugLoading, "Scene 7: caught four times, back to the menus");
		if (!switchTo(0))
			return 0;
	}
	if (_nedPos.y > 0x258) {
		if (0x1806 < _nedPos.x && _nedPos.x < 0x1888)
			_toScene1 = 1;
		else if (0xa28 < _nedPos.x && _nedPos.x < 0xaf0)
			_toScene3 = 1;
	} else if (_nedPos.y <= 0x12) {
		if (0x1239 < _nedPos.x)
			_toRightLip = 1;
		else if (_nedPos.x < 0x514 && !_leftLipDone)
			_toLeftLip = 1;
	}
	if (_climbDown) {
		if (!_nedActive) {
			// Ned climbs back down the ladder for four ticks
			_jawArmed = 0;
			_toLeftLip = 0;
			nedTick(kInputDown, now);
			if (_climbTicks > 2) {
				_nedActive = 1;
				_climbTicks = 0;
			} else {
				_climbTicks++;
			}
		} else if (ned()->getCurSlot() == 1 && _jawY == kJawOpenY) {
			_climbDown = 0;
			_leftLipDone = 0;
			_inputLock = 0;
			hint(0);
		}
	}
	if (_nedPos.x < 0x1af4) {
		jawTick(now);
		flash(now, false);
	}
	if (_toScene3) {
		if (!switchTo(3))
			return 0;
		_toScene3 = 0;
	}
	if (_toScene1) {
		if (!switchTo(1))
			return 0;
		_toScene1 = 0;
	}
	if (_toScene2) {
		// 0x482e60 sets a flag of scene 2's (*DAT_00521840 = 1); scene 2 isn't ported
		if (!switchTo(2))
			return 0;
		_toScene2 = 0;
	}
	if (_toRightLip) {
		clipBreak(0x19, true);
		_toRightLip = 0;
		hint(1);
	}
	if (_toLeftLip) {
		_armed = 1;
		clipBreak(0x1a, false);
		_toLeftLip = 0;
		_climbDown = 1;
		_nedActive = 0;
		_leftLipDone = 1;
		_inputLock = 1;
	}
	return 0;
}

// ---- Hooks ----

// 0x42da40 (+0x00): button 2 uses slot 0x2f, 0x30 at the gate 0x97, 0x9b at a monster once armed
int Scene7::hookUse(void *user, int x, int y, int16 *state) {
	Scene7 *s = (Scene7 *)user;
	int16 dir = 0x2f, found = -1;
	int32 rect[4] = { 0, 0, 0, 0 };
	s->findTarget(dir, found, rect);
	if (found == 0x97)
		dir = 0x30;
	if (s->_armed && s->_monsterSlot != -1)
		dir = 0x9b;
	s->ned()->setUseSlot(dir);
	return 1;
}

// 0x42da00 (+0x04): every region takes state 0xe, but the bug's while it is at its fourth hole
int Scene7::hookRegion(void *user, int32 id, uint32 flags, int x, int y, int16 *state) {
	Scene7 *s = (Scene7 *)user;
	if (id == s->_bugRegion && s->_bugSlot != -1 && s->slot(0x13).x == 0x1676)
		return 0;
	*state = 0xe;
	return 1;
}

// 0x42ea30 (+0x08): the area is 5; the direction 0x42d810 finds is dropped
void Scene7::hookFindTarget(void *user, int x, int y, int16 *a, int16 *b, int32 rect[4]) {
	Scene7 *s = (Scene7 *)user;
	*a = 5;
	int16 dir = 5;
	s->findTarget(dir, *b, rect);
}

// 0x42dac0 (+0x0c)
int Scene7::hookHitTarget(void *user, int x, int y, int16 *a, int16 *b, const int32 rect[4]) {
	return ((Scene7 *)user)->hitTarget(*b);
}

// 0x42d810: the target in front of Ned, as a world rect (box 0 of its current frame)
int Scene7::findTarget(int16 &dir, int16 &found, int32 rect[4]) {
	found = -1;
	int unused = 0;
	if (!ned()->findTarget(ned()->getFacing(), ned()->getCurSlot(), _targets, dir, unused, found))
		return 1;
	int ref = found;
	bool noMirror = true;
	switch (found) {
	case 4:
	case 0x9b:
	case 0xa2:
	case 0xa9:
		ref = _monsterRef;
		noMirror = _monsterFlip != 0;
		break;
	case 0x15:
		ref = 0x13;
		break;
	case 0x32:
		ref = 0x31;
		break;
	default:
		break;
	}
	int pos[2];
	attachPos(found, ref, pos, noMirror);
	Slot &s = slot(found);
	int frame = 0, duration = 0;
	if (s.sequence)
		s.sequence->getStep(s.step, frame, duration);
	boxRect(found, frame, 0, rect);
	rect[0] += pos[0];
	rect[1] += pos[1];
	rect[3] += pos[1];
	rect[2] += pos[0];
	return 1;
}

} // End of namespace NightmareNed
