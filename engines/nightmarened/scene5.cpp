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
#include "nightmarened/scene5.h"
#include "nightmarened/slotanim.h"

namespace NightmareNed {

// ---- Scene5Base: what the scene and its sub-scenes share ----

Scene5Base::Scene5Base(Game *game, int id) : Scene(game, id), _paletteVA(0), _unportedExit(-1),
	_nedListLoaded(false), _lastDraw(0) {
	memset(_palette, 0, sizeof(_palette));
	_hooks.user = this;
	_hooks.use = nullptr;
	_hooks.region = nullptr;
	_hooks.findTarget = nullptr;
	_hooks.hitTarget = nullptr;
}

Scene5Base::~Scene5Base() {
	freeAll();
}

Slot &Scene5Base::slot(int id) {
	return _game->res().getSlot(kArea, CLIP(id, 0, (int)kAreaSlots - 1));
}

void Scene5Base::visible(int id, bool on) {
	if (id >= 0 && id < kAreaSlots && slot(id).sprite)
		slot(id).sprite->setVisibility(on);
}

void Scene5Base::order(int id, int z) {
	if (id >= 0 && id < kAreaSlots && slot(id).sprite)
		slot(id).sprite->setDrawOrder((int16)z);
}

BitmapRec *Scene5Base::bmp(uint32 va) {
	return _bitmaps.contains(va) ? _bitmaps[va] : nullptr;
}

CollisionMap *Scene5Base::map(uint32 va) {
	return _maps.contains(va) ? _maps[va] : nullptr;
}

// 0x4586e0 (synchronous), into NED90000 (DAT_004baee0)
int Scene5Base::submit(uint32 va, int count, int scene) {
	NafArchive *naf = _game->getNaf(kNaf);
	if (!naf)
		return 5;
	for (int i = 0; i < count; i++) {
		uint32 e = va + i * 0x24;
		if (_entries.contains(e))
			continue;
		Common::Array<Request> one = _game->readList(e, 1);
		Request &req = one[0];
		req.scene = scene;
		if (!_game->res().load(naf, req))
			continue;
		if (req.bitmap)
			_bitmaps[req.destVA] = req.bitmap;
		if (req.collision)
			_maps[req.destVA] = req.collision;
		if (req.hasPalette && req.paletteVA == _paletteVA)
			memcpy(_palette, req.palette, sizeof(_palette));
		_entries[e] = req;
	}
	return 0;
}

// 0x458dc0: frees backwards
void Scene5Base::release(uint32 va, int count) {
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

void Scene5Base::freeAll() {
	Common::Array<uint32> keys;
	for (EntryMap::iterator it = _entries.begin(); it != _entries.end(); ++it)
		keys.push_back(it->_key);
	Common::sort(keys.begin(), keys.end());
	for (int i = (int)keys.size() - 1; i >= 0; i--)
		release(keys[i], 1);
}

// 0x459000: every sprite and bitmap of the list goes on the primary surface, hidden
void Scene5Base::attachList(uint32 va, int count) {
	RdxSurface *primary = _game->display().getPrimary();
	for (int i = 0; i < count; i++) {
		uint32 e = va + i * 0x24;
		if (!isLoaded(e))
			continue;
		const Request &req = _entries[e];
		RdxObject *obj = nullptr;
		if (req.type == Request::kSprite && req.area >= 0 && req.area < kAreaCount)
			obj = _game->res().getSlot(req.area, req.id).sprite;
		else if (req.type == Request::kBitmap && req.bitmap)
			obj = req.bitmap->object;
		if (obj) {
			obj->setDestination(primary);
			obj->setVisibility(false);
		}
	}
}

// 0x459240: hides everything of the list that is shown and remembers it, or shows it again
void Scene5Base::saveVisibility(uint32 list, int count, bool restore, Common::Array<int> &state) {
	state.resize(count);
	for (int i = count - 1, k = 0; i >= 0; i--, k++) {
		uint32 e = list + i * 0x24;
		if (!restore)
			state[k] = 0;
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

// A bitmap record's object at draw order z, shown, colour key 10
BitmapRec *Scene5Base::showBitmap(uint32 va, int z) {
	BitmapRec *rec = bmp(va);
	if (rec && rec->object) {
		rec->object->setDrawOrder(z);
		rec->object->setVisibility(true);
		rec->object->setBitmap(&rec->bitmap.surface, kKey);
	}
	return rec;
}

// ---- Slots ----

// 0x430890
int16 Scene5Base::switchSlot(int newSlot, int oldSlot) {
	return (int16)_game->display().slotSwitch(kArea, (int16)newSlot, (int16)oldSlot);
}

// 0x430530 slot_place_nx: returns the step's duration
int Scene5Base::place(int id, int ref) {
	int frame = 0, duration = 0;
	if (id < 0 || id >= kAreaSlots || ref < 0 || ref >= kAreaSlots)
		return 0;
	_game->display().slotPlace(kArea, id, ref, &frame, &duration, true, Common::Point(0, 0));
	return duration;
}

bool Scene5Base::step(int id, int ref, uint32 now, int16 duration) {
	return slotStep(_game, kArea, id, ref, now, duration);
}

int16 Scene5Base::start(int newSlot, int oldSlot, int ref, uint32 now, int16 stepIndex) {
	return slotStart(_game, kArea, newSlot, oldSlot, ref, now, stepIndex);
}

void Scene5Base::nudge(int id, int dx, int dy) {
	slotNudge(_game, kArea, id, dx, dy);
}

void Scene5Base::showSlot(int id, int z, bool on) {
	slotShow(_game, kArea, id, z, on);
}

// 0x430440 slot_attach_pos (not mirrored)
void Scene5Base::attachPos(int id, int ref, int out[2]) {
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
	out[0] = r.x - d;
	out[1] = r.y + (say - ray - foy);
}

// IntersectRect
bool Scene5Base::intersect(const int32 a[4], const int32 b[4]) {
	return MAX(a[0], b[0]) < MIN(a[2], b[2]) && MAX(a[1], b[1]) < MIN(a[3], b[3]);
}

// ---- Sound ----

// 0x455880 snd_play
void Scene5Base::playSound(int id) {
	_game->playSlotSound(slot(id), nullptr);
}

// 0x4558e0: the slot's sound, looping
void Scene5Base::loopSound(int id) {
	Slot &s = slot(id);
	_game->stopSlotSound(s);
	if (s.sound.size() < 18)
		return;
	uint32 pcm = 18 + READ_LE_UINT16(s.sound.data() + 16);
	if (pcm >= s.sound.size())
		return;
	_game->playVoice(s.voice, s.sound.data(), s.sound.data() + pcm, s.sound.size() - pcm, 0, 0, true);
}

// 0x455930
void Scene5Base::stopSound(int id) {
	_game->stopSlotSound(slot(id));
}

// 0x455950 -> 0x44d020: every slot voice stops; the streamed music plays on
void Scene5Base::stopAllSounds() {
	for (int a = 0; a < kAreaCount; a++) {
		for (int i = 0; i < kAreaSlots; i++)
			_game->stopSlotSound(_game->res().getSlot(a, i));
	}
}

// ---- Collision ----

// 0x487f50 + 0x4872a0
void Scene5Base::addMap(CollisionMap *m) {
	if (m)
		_world.addMap(m, m->bounds.left, m->bounds.top, m->bounds.right, m->bounds.bottom);
}

void Scene5Base::addMap(CollisionMap *m, const int32 rect[4]) {
	if (m)
		_world.addMap(m, rect[0], rect[1], rect[2], rect[3]);
}

// 0x487320
void Scene5Base::removeMap(CollisionMap *m) {
	if (m)
		_world.removeMap(m);
}

// 0x487f50
void Scene5Base::bounds(const CollisionMap *m, int32 out[4]) {
	if (!m) {
		out[0] = out[1] = out[2] = out[3] = 0;
		return;
	}
	out[0] = m->bounds.left;
	out[1] = m->bounds.top;
	out[2] = m->bounds.right;
	out[3] = m->bounds.bottom;
}

// 0x41d800: the enabled regions with ids first..first + count - 1, by id - first
void Scene5Base::regionRects(CollisionMap *m, int32 (*out)[4], int first, int count) {
	if (!m)
		return;
	for (const CollisionMap::Region &r : m->regions) {
		if ((r.type & 0x80000000) || r.id < first || r.id >= first + count)
			continue;
		int32 *o = out[r.id - first];
		o[0] = r.rect.left;
		o[1] = r.rect.top;
		o[2] = r.rect.right;
		o[3] = r.rect.bottom;
	}
}

// 0x41d880: the last of the rects Ned's box overlaps, or -1
int Scene5Base::nedInRect(const int32 (*rects)[4], int count) {
	int32 box[4];
	ned()->getBox(ned()->getCurSlot(), _nedPos, box);
	for (int i = count - 1; i >= 0; i--) {
		if (intersect(box, rects[i]))
			return i;
	}
	return -1;
}

Common::Array<CollisionMap *> Scene5Base::overlayMaps() const {
	Common::Array<CollisionMap *> maps;
	for (int i = 0; i < _world.getMapCount(); i++)
		maps.push_back(_world.getMap(i));
	return maps;
}

// ---- Targets and placements ----

// 0x4309c0 olist_add_sprite
void Scene5Base::targetAdd(int id, bool noFlip) {
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
void Scene5Base::targetRemove(int id) {
	for (uint i = 0; i < _targets.size(); i++) {
		if (_targets[i].area == kArea && _targets[i].slot == (int16)id && _targets[i].sub == 1)
			_targets.remove_at(i);
	}
}

// 0x41cae0 / 0x479860 / 0x45d840: the target in front of Ned, as a world rect (box 0 of
// its current frame); the sub-scene 1 copy leaves out the scroll, which is 0 there
int Scene5Base::findTarget(int16 &dir, int16 &found, int32 rect[4], int scrollX, int scrollY) {
	found = -1;
	int unused = 0;
	if (!ned()->findTarget(ned()->getFacing(), ned()->getCurSlot(), _targets, dir, unused, found))
		return 1;
	int pos[2];
	attachPos(found, found, pos);
	Slot &s = slot(found);
	int frame = 0, duration = 0;
	if (s.sequence)
		s.sequence->getStep(s.step, frame, duration);
	if (s.boxes)
		s.boxes->getRawRect(frame, 0, rect);
	else
		rect[0] = rect[1] = rect[2] = rect[3] = -0x7fff;
	pos[0] -= scrollX;
	pos[1] -= scrollY;
	rect[0] += pos[0];
	rect[1] += pos[1];
	rect[3] += pos[1];
	rect[2] += pos[0];
	return 1;
}

// 0x45a560(9, 999, n): 90999<n>.SST
bool Scene5Base::loadSst(int n) {
	_sst.clear();
	Common::String name = Common::String::format("%02d%03d0%02d.sst",
		(int16)_game->exe().readUint16(kSstAreas + 2 * kSstAreaIndex), 999, n);
	Common::ScopedPtr<Common::SeekableReadStream> stream(_game->getEngine()->openFile(name));
	if (!stream || !loadPlacements(*stream, _sst)) {
		warning("Can't read %s", name.c_str());
		return false;
	}
	return true;
}

// 0x4870a0 with 0x41d4c0: every area-6 entry puts its slot at (x, y) + (dx, dy), mirrored
// by its width, and the sprite at that point less its current frame's offset
void Scene5Base::applySst(int dx, int dy) {
	for (const Placement &e : _sst) {
		if (e.area != kArea || e.slot < 0 || e.slot >= kAreaSlots)
			continue;
		Slot &s = slot(e.slot);
		s.x = e.x + dx;
		s.y = e.y + dy;
		if (e.flip)
			s.x += s.w;
		if (!s.sprite)
			continue;
		int frame = 0, duration = 0;
		if (s.sequence)
			s.sequence->getStep(s.step, frame, duration);
		int ox = 0, oy = 0;
		if (s.boxes && frame >= 0 && frame < (int)s.boxes->frames.size()) {
			ox = s.boxes->frames[frame].offX;
			oy = s.boxes->frames[frame].offY;
		}
		s.sprite->setPosition(s.x - (int16)ox, s.y - (int16)oy);
	}
}

// ---- Ned and the scene graph ----

Ned *Scene5Base::ned() {
	return _game->ned();
}

// 0x454920 with the scene's world, scroll and hooks
void Scene5Base::nedTick(uint32 input, uint32 now, int scrollX, int scrollY) {
	int f = 0;
	ned()->tick(_world, input, f, _nedPos, now, scrollX, scrollY, &_hooks);
}

// The sub-scenes rely on scene 5 having loaded Ned's list 0x16; the test hook
// nightmarened_scene=5:<sub> enters them from elsewhere, so they load it themselves then
void Scene5Base::nedLoad(int fromScene) {
	_nedListLoaded = false;
	if (fromScene != 5) {
		ned()->listLoad(kNedList);
		_nedListLoaded = true;
	}
}

// 0x475180: a vision of this world's creature (Hints)
void Scene5Base::hint(int which, int x, int y, NedXform xform) {
	_game->hints().trigger(which, ned()->getFacing() != 0, x, y, xform, this);
}

// 0x45a0f0, staying here when the target isn't registered yet
int Scene5Base::switchTo(int scene, int sub) {
	if (!_game->hasScene(scene, sub)) {
		if (_unportedExit != scene * 100 + sub)
			debugC(1, kDebugLoading, "Scene 5: exit to scene %d:%d (not ported), staying", scene, sub);
		_unportedExit = scene * 100 + sub;
		return 1;
	}
	_unportedExit = -1;
	_game->switchScene(scene, sub);
	return 0;
}

// ---- Scene5 ----

Scene5::Scene5(Game *game) : Scene5Base(game, 5) {
	const ExeImage &exe = _game->exe();
	for (int i = 0; i < kTubs; i++) {
		_tubA[i] = (int16)exe.readUint16(0x49ca28 + 2 * i);
		_tubB[i] = (int16)exe.readUint16(0x49ca40 + 2 * i);
	}
	_tubOrder = exe.readSint32(0x49ca54);
	_normalSpeed = exe.readSint32(0x49ca58);
	_exitClip = exe.readSint32(0x49add8);
	_paletteVA = kPaletteVA;

	_warpTest = ConfMan.hasKey("nightmarened_s5_warp") ? ConfMan.getInt("nightmarened_s5_warp") : 0;
	_posTest = Common::Point(-1, -1);
	if (ConfMan.hasKey("nightmarened_s5_pos")) {
		int x = -1, y = -1;
		if (sscanf(ConfMan.get("nightmarened_s5_pos").c_str(), "%d,%d", &x, &y) == 2)
			_posTest = Common::Point(x, y);
	}
	_exitTest[0] = _exitTest[1] = 0;
	if (ConfMan.hasKey("nightmarened_s5_exit"))
		sscanf(ConfMan.get("nightmarened_s5_exit").c_str(), "%d:%d", &_exitTest[0], &_exitTest[1]);
	_ratTime = 0;
	_drainPull = _cameraFrozen = 0;
	for (int i = 0; i < 3; i++)
		_ratSlot[i] = -1;
	_leverSlot = 0;
	_drainWant = 0;
	memset(_tubMapBounds, 0, sizeof(_tubMapBounds));
	_ratLanded = 0;
	for (int i = 0; i < kTubs; i++) {
		_splashEnd[i] = 0;
		_dripTime[i] = 0;
		_dropSlot[i] = -1;
		_dripState[i] = 0xf0;
		_dropFall[i] = 0;
		_splash[i] = 0;
	}
	_drainDelta = 0;
	memset(_scene3Rect, 0, sizeof(_scene3Rect));
	memset(_exitRects, 0, sizeof(_exitRects));
	memset(_tubZones, 0, sizeof(_tubZones));
	memset(_drainRect, 0, sizeof(_drainRect));
	memset(_musicRect, 0, sizeof(_musicRect));
	_ratsCaught = 0;
	_climbEase = _ledgeEase = 0;
	_doorWant = _doorState = 0;
	_midRemX = _midRemY = _farRemX = _farRemY = 0;
	_tentaclePos = -1;
	_scrollX = _scrollY = 0;
	_doorSlot = 0xb7;
	_tubMapIndex = -1;
	_drainSlot = 4;
	_backdrop = _midLayer = _farLayer = nullptr;
	_drainState = _leverWant = _leverState = 0;
	_ratLane = -1;
	_ratZapped = 0;
	memset(&_grid, 0, sizeof(_grid));
	_ratFall = 0;
	_tubZone = 0;
	_fallDone = 0;
	_tentacleTime = 0;
	_drainSpeed = 0;
	_clipPending = _drainIdle = _ratsDone = _hint1Armed = _hint3Armed = 0;
	_exitStage = _exitScene = _exitSub = 0;
	_climbing = 0;
	_lastNedX = _lastNedY = 0;
	_fallSound = _altMusic = 0;
	_screenX = _screenY = 0;
	_scrollTime = 0;
	_camNedX = _camNedY = 0;
	_ambientTime = 0;
	_fallPolling = false;

	_hooks.use = hookUse;
	_hooks.region = hookRegion;
	_hooks.findTarget = hookFindTarget;
	_hooks.hitTarget = hookHitTarget;
}

// m0 0x419c00
int Scene5::init() {
	_musicRect[0] = 0x4e8;
	_musicRect[1] = 1000;
	_musicRect[2] = 0xf00;
	_musicRect[3] = 0x602;
	_ratsCaught = 0;
	_drainIdle = _ratsDone = _hint3Armed = 0;
	_cameraFrozen = 0;
	return 0;
}

// m1 0x419120
void Scene5::shutdown() {
	_ratsCaught = 0;
	_drainIdle = _ratsDone = _hint3Armed = 0;
}

// m9 0x41dfc0
void Scene5::saveState(int32 *out) {
	out[0] = _ratsCaught;
}

// m10 0x41dfd0
void Scene5::restoreState(const int32 *in, int size) {
	_ratsCaught = (size == 4 && in) ? in[0] : 0;
}

// m2 0x419140: from a sub-scene only the level's list loads again; from the quilt (0) or
// a new game (-1) the arrival clip 8705 loads first (when installed)
int Scene5::load(int fromScene, int fromSub) {
	if (fromScene != 5) {
		if ((fromScene == -1 || fromScene == 0) && _game->media()._installLevel)
			_game->media().loadClip(5);
		if (!_game->getNaf(kNaf))
			return 5;
		ned()->listLoad(kNedList);
	}
	return submit(kMainList, kMainCount, 1);
}

// m3 0x4191e0
int Scene5::enter(int fromScene, int fromSub) {
	_exitClip = -1;
	_cameraFrozen = 0;
	_clipPending = 0;
	_hint1Armed = 0;
	if (fromScene != 5) {
		if ((fromScene == -1 || fromScene == 0) && _game->media()._installLevel)
			_game->media().playClipWithMusic(1, 5);
		_game->media().loadClip(0x2a);
		_game->media().playClipWithMusic(1, 0x2a);
	}
	uint32 now = _game->now();
	_game->_optionsAllowed = true;
	_game->media().playMusic(kMusic);
	if (!isLoaded(kMainLast))
		return 5;
	attachList(kMainList, kMainCount);
	ned()->copyPalette(_palette);
	_game->display().applyPalette(_palette, kPalAll);
	ned()->listShow(kNedList);
	startLevel(now);
	return 0;
}

// The rest of m3 0x4191e0, once the lists are in
void Scene5::startLevel(uint32 now) {
	// Every map but 3 (opened by the rats), 9 (the open outhouse door) and 11 (regions only)
	_world = CollisionWorld();
	for (int n = 1; n <= 11; n++) {
		if (n != 9 && n != 3 && n != 11)
			addMap(mapN(n));
	}
	bounds(mapN(6), _scene3Rect);
	_scene3Rect[3] = _scene3Rect[1] + 100;
	_nedPos.x = 0x465;
	for (int y = 0x602; y >= 0; y--) {
		CollisionMap *m = mapN(2);
		if (m && m->isSolid((int16)_nedPos.x, (int16)y)) {
			_nedPos.y = y;
			break;
		}
	}
	_game->res().getSlot(1, 4).offsets[0] = 6;
	ned()->setFacing(1);
	ned()->setWrap1(true, kWorldW, xform, this);
	ned()->show(false);
	_drainPull = 0;
	_scrollX = -0x39d;
	_scrollY = -0x422;

	// 90999000.SST, applied at the scroll
	if (!loadSst(0))
		return;
	applySst(_scrollX, _scrollY);
	_targets.clear();

	_backdrop = showBitmap(kBackdrop, 0x76);
	if (_backdrop && _backdrop->object)
		_backdrop->object->setPosition(0, 0);
	_midLayer = showBitmap(kMidLayer, 0x75);
	_farLayer = showBitmap(kFarLayer, 0x74);
	_midRemX = _midRemY = _farRemX = _farRemY = 0;

	// The tubs and their legs, each at a random step of its walk
	for (int i = 0; i < kTubs; i++) {
		order(_tubA[i], 0x72);
		order(_tubB[i], 5);
		Slot &a = slot(_tubA[i]);
		int16 st = a.sequenceLength > 0 ? (int16)((int)_game->crtRandom() % a.sequenceLength) : 0;
		start(_tubA[i], -1, _tubA[i], now, st);
		start(_tubB[i], -1, _tubA[i], now, st);
		order(0x10e + i, 4);
		_splash[i] = 0;
	}
	_tubZone = 0;
	_tubMapIndex = -1;

	// The drain, the lever and the outhouse door
	order(4, 0x70);
	order(5, 0x70);
	_drainSlot = start(4, -1, 4, now, 0);
	_drainState = 0;
	_drainWant = 1;
	order(0x52, 0x71);
	order(0x6c, 0x71);
	_leverSlot = start(0x6c, -1, 0x6c, now, 0);
	targetAdd(_leverSlot, false);
	_leverState = _leverWant = 2;
	order(0x53, 0x6e);
	order(0xb7, 0x6e);
	_doorSlot = start(0xb7, -1, 0xb7, now, 0);
	targetAdd(_doorSlot, false);
	_doorState = _doorWant = 0;
	_ratZapped = 0;

	// The caught-rat counter, every other one mirrored
	for (int i = 12; i >= 0; i--) {
		order(200 + i, 0x6f);
		if (i & 1) {
			Slot &s = slot(200 + i);
			if (s.sprite && !s.flip)
				s.flip = s.sprite->applyEffect(kFxHorizFlip);
		}
	}
	// The three rat lanes
	for (int k = 2; k >= 0; k--) {
		order(0xdc + k, 3);
		order(0xdf + k, 3);
		order(0xe5 + k, 3);
		_ratSlot[k] = -1;
	}
	_ratLane = -1;
	_ratTime = 0;
	_ratLanded = 0;

	// The nine drips
	for (int i = 0; i < kTubs; i++) {
		showSlot(0xf0 + i, 0x69, false);
		showSlot(0xfa + i, 0x69, false);
		showSlot(0x104 + i, 0x6a, false);
		start(0xf0 + i, -1, 0xf0 + i, now, -1);
		_dripTime[i] = 0;
		_dripState[i] = 0xf0;
		_dropSlot[i] = -1;
	}
	_tentaclePos = -1;
	_tentacleTime = 0;
	showSlot(0x1f, 3, false);
	_exitScene = _exitSub = 0;
	_exitStage = 0;

	// The pieces drawn in front of or behind Ned
	static const int16 kFront[][3] = {
		{ 304, 1, 3 }, { 305, 1, 3 }, { 306, 1, 3 }, { 307, 1, 0x6b }, { 308, 1, 0x6c },
		{ 309, 1, 3 }, { 311, 1, 0x67 }, { 312, 1, 0x65 }, { 313, 0, 0x6d }, { 314, 1, 3 },
		{ 315, 1, 0x66 }, { 316, 1, 0x68 }
	};
	for (const auto &f : kFront) {
		visible(f[0], f[1] != 0);
		order(f[0], f[2]);
	}

	CollisionMap *regions = mapN(11);
	regionRects(regions, _exitRects, 100, 10);
	regionRects(regions, _tubZones, 200, 9);
	regionRects(regions, _drainRect, 300, 1);

	// The tile grid: 9 x 5 tiles of 512 x 320 from NAF scene 99
	_grid.area = 9;
	_grid.scene = 99;
	_grid.x = 0;
	_grid.y = 0;
	_grid.cellW = 0x200;
	_grid.cellH = 0x140;
	_grid.cols = 9;
	_grid.rows = 5;
	_grid.list = kTileList;
	streamTiles();
	for (int i = 0; i < kTubMapCount; i++)
		bounds(tubMap(i), _tubMapBounds[i]);

	ned()->playAction(0x3e, _nedPos, _scrollX, _scrollY);
	ned()->show(true);
	ned()->enable(true);
	int16 caught = (int16)_ratsCaught;
	for (int i = 0; i < caught; i++)
		start(200 + i, -1, 200 + i, 0, 5);
	if (caught == kRatsNeeded) {
		_ratsDone = 1;
		addMap(mapN(3));
	}
}

// m4 0x419c50
int Scene5::leave(int toScene, int toSub) {
	stopAllSounds();
	_fallPolling = false;
	// The overlay would draw the maps freed here during the exit clip
	_game->display().setCollisionOverlay(Common::Array<CollisionMap *>(), Common::Point());
	_sst.clear();
	_game->_optionsAllowed = false;
	ned()->resetRunSpeeds();
	ned()->setWrap1(false, 0, nullptr, nullptr);
	release(kMainList, kMainCount);
	release(kTileList, kTileCount);
	_backdrop = _midLayer = _farLayer = nullptr;
	if (toScene != 5) {
		ned()->listFree(kNedList);
		_game->closeNaf(kNaf);
	}
	if (_exitClip != -1)
		_game->media().loadClip(_exitClip);
	_world = CollisionWorld();
	_targets.clear();
	return 0;
}

// m5 0x419d00: the TV clip on the way to scene 1 (8727), the attic (8728) or below (8744)
int Scene5::unload(int toScene, int toSub) {
	if (_exitClip == 0x1b || _exitClip == 0x1c || _exitClip == 0x2c)
		_game->media().playClipWithMusic(1, _exitClip);
	return 0;
}

// The clip 0x1f break (region 9999): the level is hidden while 8731 plays
void Scene5::clipBreak(uint32 now) {
	_clipPending = 0;
	_game->media().stopMusic();
	stopAllSounds();
	_game->media().loadClip(0x1f);
	Common::Array<int> main, tiles;
	saveVisibility(kMainList, kMainCount, false, main);
	saveVisibility(kTileList, kTileCount, false, tiles);
	_game->media().playClipFull(2, 0x1f);
	saveVisibility(kMainList, kMainCount, true, main);
	saveVisibility(kTileList, kTileCount, true, tiles);
	_game->media().playMusic(kAltMusic);
	if (_drainState == 2)
		loopSound(5);
	ned()->copyPalette(_palette);
	_game->display().applyPalette(_palette, kPalAll);
	nedTick(kInputDown, now, _scrollX, _scrollY);
	_hint3Armed = 1;
}

// Down a pipe (exit 5, -1): clip 8743, then Ned drops in at (4000, 0x423)
void Scene5::exitDrop() {
	_cameraFrozen = 0;
	_game->media().loadClip(0x2b);
	_game->media().playClipWithMusic(1, 0x2b);
	ned()->copyPalette(_palette);
	_game->display().applyPalette(_palette, kPalAll);
	_game->media().playMusic(kMusic);
	_nedPos = Common::Point(4000, 0x423);
	ned()->show(false);
	ned()->startFall();
	_exitScene = _exitSub = 0;
	order(311, 0x67);
	order(312, 0x65);
	order(313, 0x6d);
	order(315, 0x66);
	order(316, 0x68);
	_hint1Armed = 1;
}

// m7 0x419ff0. The original tick has no throttle of its own (it runs every main-loop pass);
// here it runs at the 41 ms of the other scenes
int Scene5::tick(uint32 input, uint32 now) {
	if (now <= _lastDraw + kMsPerStep) {
		g_system->delayMillis(MIN<uint32>(_lastDraw + kMsPerStep + 1 - now, 10));
		return 0;
	}
	// 0x41e050, the end-of-sound callback of slot 125's fall
	if (_fallPolling && !_game->isVoicePlaying(slot(125).voice)) {
		_fallPolling = false;
		_fallDone = 1;
	}
	if (_ambientTime < now) {
		_ambientTime = now + ((int)_game->crtRandom() % 5 + 20) * 1000;
		playSound(33);
	}
	if (_musicRect[0] - 0x32 < _nedPos.x && _nedPos.x < _musicRect[2] + 0x32) {
		if (!_altMusic) {
			_game->media().playMusic(kAltMusic);
			_altMusic = 1;
		}
	} else if (_altMusic) {
		_game->media().playMusic(kMusic);
		_altMusic = 0;
	}

	// On a tub Ned runs 40% faster and can't leave its middle
	CollisionMap *standing = _world.getMapCount() ? _world.getLastHitMap() : nullptr;
	if (standing && standing == tubMap(_tubMapIndex)) {
		if (_normalSpeed) {
			ned()->setRunSpeeds(0x28);
			_normalSpeed = 0;
		}
		int32 b[4];
		bounds(standing, b);
		int left = b[0] - _tubMapBounds[_tubMapIndex][0];
		if (_nedPos.x < left + 0x155)
			input &= ~kInputLeft;
		else if (left + 0x186 < _nedPos.x)
			input &= ~kInputRight;
	} else if (!_normalSpeed) {
		_normalSpeed = 1;
		ned()->resetRunSpeeds();
	}
	if (!_clipPending)
		nedTick(input, now, _scrollX, _scrollY);

	// The pit under the ladders: a scream (slot 125), then Ned is back at (0x4e2, 0x5c5)
	if (_nedPos.y > 0x792) {
		if (_fallDone) {
			debugC(1, kDebugLoading, "Scene 5: back from the pit");
			_nedPos = Common::Point(0x4e2, 0x5c5);
			ned()->resetIdle();
			_fallSound = 0;
		}
	} else if (_nedPos.y > 0x634 && _musicRect[0] < _nedPos.x && _nedPos.x < _musicRect[2] && !_fallSound) {
		_fallDone = 0;
		debugC(1, kDebugLoading, "Scene 5: Ned falls into the pit at %d,%d", _nedPos.x, _nedPos.y);
		playSound(125);
		_fallPolling = true;
		_fallSound = 1;
	}
	if (_scene3Rect[0] <= _nedPos.x && _nedPos.x < _scene3Rect[2] && _scene3Rect[1] <= _nedPos.y && _nedPos.y < _scene3Rect[3]) {
		_exitScene = 3;
		_exitSub = -1;
	}
	if (_clipPending)
		clipBreak(now);
	if (_drainPull) {
		// Down the drain: Ned lands on map 2 below, 48 pixels to the left
		for (int y = 0x602; y >= 0; y--) {
			CollisionMap *m = mapN(2);
			if (m && m->isSolid((int16)_nedPos.x, (int16)y)) {
				_nedPos.y = y;
				break;
			}
		}
		_nedPos.x -= 0x30;
		ned()->playAction(0x3f, _nedPos, _scrollX, _scrollY);
		_drainPull = 0;
	}
	// The developer keys 1..9 (GetAsyncKeyState) put Ned on map 1..9; the test hook
	// nightmarened_s5_warp=<map> presses one once Ned stands idle
	for (int n = 1; n <= 11; n++) {
		bool down = n <= 9 && _game->getEngine()->isKeyDown((Common::KeyCode)(Common::KEYCODE_0 + n));
		if (_warpTest == n && ned()->getState() == 4) {
			down = true;
			_warpTest = 0;
		}
		if (!down)
			continue;
		int32 b[4];
		bounds(mapN(n), b);
		_nedPos = Common::Point(b[0] + 0x20, b[1]);
		ned()->resetIdle();
	}

	if (ned()->getState() == 4) {
		if (_posTest.x >= 0) {
			_nedPos = _posTest;
			_posTest = Common::Point(-1, -1);
			ned()->startFall();
		} else if (_exitTest[0]) {
			_exitScene = _exitTest[0];
			_exitSub = _exitTest[1];
			_exitTest[0] = 0;
		}
	}

	int d[2];
	camera(now, d);
	tubsTick(now, d[0], d[1]);
	doorTick(now, d[0], d[1]);
	leverTick(now, d[0], d[1]);
	drainTick(now, d[0], d[1]);
	ratsTick(now, d[0], d[1]);
	dripsTick(now, d[0], d[1]);
	tentacleTick(now, d[0], d[1]);
	_game->display().setCollisionOverlay(overlayMaps(), Common::Point(_scrollX, _scrollY));
	_game->display().draw();
	_lastDraw = now;
	debugC(3, kDebugGraphics, "Scene 5: input %x Ned %d,%d state %d slot %d scroll %d,%d drain %d rats %d door %d exit %d,%d",
		input, _nedPos.x, _nedPos.y, ned()->getState(), ned()->getCurSlot(), _scrollX, _scrollY, _drainState,
		(int16)_ratsCaught, _doorState, _exitStage, _exitScene);

	exitRegions();
	int zone = nedInRect(_tubZones, kTubs);
	if (zone >= 0)
		_tubZone = (int16)zone;
	if (nedInRect(_drainRect, 1) == 0 && _drainState == 2)
		_drainPull = 1;

	if (_exitScene != 0) {
		if (_exitScene == 5 && _exitSub == -1) {
			exitDrop();
			return 0;
		}
		switch (_exitScene) {
		case 1:
			_exitClip = _exitSub != -1 ? -1 : 0x1b;
			break;
		case 3:
			_exitClip = _exitSub + 1 != 0 ? -1 : 0x2c;
			break;
		case 4:
			_exitClip = _exitSub != -1 ? -1 : 0x1c;
			break;
		case 5:
			_exitClip = _exitSub != 0 ? -1 : 0x2c;
			break;
		default:
			_exitClip = -1;
			break;
		}
		stopAllSounds();
		int scene = _exitScene, sub = _exitSub;
		if (switchTo(scene, sub)) {
			// Not ported: the level goes on
			_exitScene = _exitSub = 0;
			_exitClip = -1;
		}
	}
	return 0;
}

} // End of namespace NightmareNed
