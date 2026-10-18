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
#include "nightmarened/scene3_room.h"

namespace NightmareNed {

// The lit wire paths 444..451 (0x462b60 shows the one the switches pick)
static const uint32 kPictureEntries[8] = {
	0x4b0c10, 0x4b0c7c, 0x4b0cc4, 0x4b0d0c, 0x4b0d54, 0x4b0d9c, 0x4b0de4, 0x4b0e2c
};

Scene3Room::Scene3Room(Game *game) : Scene(game, 1) {
	const ExeImage &exe = _game->exe();
	_channelSound = (int16)exe.readSint32(0x4b0a40);
	_nedInput = exe.readSint32(0x4b0a44);
	_draggedSlot = (int16)exe.readUint16(0x4b12f4);
	_parentsSlot = (int16)exe.readUint16(0x4b12f8);

	_backdrop = nullptr;
	_map = nullptr;
	memset(_palette, 0, sizeof(_palette));
	_scrollX = _scrollY = 0;
	_unportedExit = -1;
	for (int i = 0; i < kAreaSlots; i++)
		_soundCallback[i] = kCbNone;
	_lastTick = 0;

	_parentsState = 0;
	for (int i = 0; i < 8; i++) {
		_channelData[i] = 0;
		_channelFn[i] = kProgNone;
	}
	for (int i = 0; i < 6; i++)
		_talkState[i] = 0;
	_ownNaf = 0;
	_programme = kProgNone;
	_talk2Done = 0;
	_draggedX = 0;
	_channel = 0;
	_jumpSlot = -1;
	_grabY = 0;
	_startDone = 0;
	_draggedState = 0;
	_startHeld = 0;
	_parentsX = 0;
	_opRun = 0;
	_op = -1;
	_opIdleTime = 0;
	_talk1Done = 0;
	_exitScene = 0;
	_grabSlot = -1;

	_hooks.user = this;
	_hooks.use = hookUse;
	_hooks.region = hookRegion;
	_hooks.findTarget = hookFindTarget;
	_hooks.hitTarget = hookHitTarget;
}

Scene3Room::~Scene3Room() {
	freeAll();
}

// ---- Resources ----

Slot &Scene3Room::slot(int id) {
	return _game->res().getSlot(kArea, CLIP(id, 0, (int)kAreaSlots - 1));
}

// The slot id of a request entry (the original reads the entry's +0x0c, e.g. DAT_004b0afc)
int16 Scene3Room::eid(uint32 entry) const {
	return (int16)_game->exe().readSint32(entry + 0x0c);
}

// 0x4586e0 (synchronous): every entry loads now. The list has no completion callbacks.
int Scene3Room::submit(uint32 va, int count, int scene) {
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
		if (e == kBackdrop)
			_backdrop = req.bitmap;
		if (e == kMap)
			_map = req.collision;
		if (req.hasPalette && req.paletteVA == kPalette)
			memcpy(_palette, req.palette, sizeof(_palette));
		_entries[e] = req;
	}
	return 0;
}

// 0x458dc0: frees backwards
void Scene3Room::release(uint32 va, int count) {
	for (int i = count - 1; i >= 0; i--) {
		uint32 e = va + i * 0x24;
		if (!_entries.contains(e))
			continue;
		Common::Array<Request> one;
		one.push_back(_entries[e]);
		if (one[0].bitmap == _backdrop)
			_backdrop = nullptr;
		if (one[0].collision) {
			_world.removeMap(one[0].collision);
			if (one[0].collision == _map)
				_map = nullptr;
		}
		_game->freeList(one);
		_entries.erase(e);
	}
}

void Scene3Room::freeAll() {
	_world = CollisionWorld();
	release(kList, kListCount);
}

// 0x459170 res_wait: 0 once the entry is loaded, 5 if it failed
int Scene3Room::wait(uint32 entry) const {
	return isLoaded(entry) ? 0 : 5;
}

// 0x459090 res_show_sprite(entry, z, 1, primary)
int Scene3Room::showSprite(uint32 entry, int z) {
	if (!isLoaded(entry))
		return 5;
	Slot &s = _game->res().getSlot(_game->exe().readSint32(entry + 0x08), eid(entry));
	if (s.sprite) {
		s.sprite->setDrawOrder((uint32)z);
		s.sprite->setDestination(_game->display().getPrimary());
		s.sprite->setVisibility(true);
	}
	return 0;
}

// 0x459100 res_show_bitmap(entry, z, 1, primary)
int Scene3Room::showBitmap(uint32 entry, int z) {
	if (!isLoaded(entry))
		return 5;
	BitmapRec *rec = _entries[entry].bitmap;
	if (rec && rec->object) {
		rec->object->setDrawOrder((uint32)z);
		rec->object->setDestination(_game->display().getPrimary());
		rec->object->setVisibility(true);
	}
	return 0;
}

// ---- Display helpers (area 4, no scroll) ----

// 0x430890
int16 Scene3Room::switchSlot(int newSlot, int oldSlot) {
	return (int16)_game->display().slotSwitch(kArea, (int16)newSlot, (int16)oldSlot);
}

// 0x430410 slot_place_dur(4, id, ref, &dur, pos, 1): returns the step's duration
int Scene3Room::place(int id, int ref) {
	int frame = 0, duration = 0;
	if (id < 0 || id >= kAreaSlots || ref < 0 || ref >= kAreaSlots)
		return 0;
	_game->display().slotPlace(kArea, id, ref, &frame, &duration, true, Common::Point(0, 0));
	return duration;
}

// 0x430410, then the slot's next deadline (now + duration * 41)
void Scene3Room::stepNext(int id, int ref, uint32 now) {
	int duration = place(id, ref);
	slot(id).deadline = now + (int16)duration * kMsPerStep;
}

// 0x430440 slot_attach_pos(4, id, ref, out, 1)
void Scene3Room::attachPos(int id, int ref, int out[2]) {
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

// ---- Sound ----

// 0x455880 snd_play(slot, 0)
void Scene3Room::playSound(int id) {
	if (id < 0 || id >= kAreaSlots)
		return;
	_soundCallback[id] = kCbNone;
	_game->playSlotSound(slot(id), nullptr);
}

// 0x455800 snd_play_cb(slot, 0, 100, cb): cb runs when the voice ends by itself
void Scene3Room::playSoundNotify(int id, uint32 callback) {
	if (id < 0 || id >= kAreaSlots)
		return;
	_game->playSlotSound(slot(id), nullptr);
	_soundCallback[id] = callback;
}

// 0x455930: a stopped voice never calls back (0x44d100)
void Scene3Room::stopSound(int id) {
	if (id < 0 || id >= kAreaSlots)
		return;
	_soundCallback[id] = kCbNone;
	_game->stopSlotSound(slot(id));
}

// 0x455950 -> 0x44d020: every slot voice stops
void Scene3Room::stopAllSounds() {
	for (int a = 0; a < kAreaCount; a++) {
		for (int i = 0; i < kAreaSlots; i++)
			_game->stopSlotSound(_game->res().getSlot(a, i));
	}
	for (int i = 0; i < kAreaSlots; i++)
		_soundCallback[i] = kCbNone;
}

// 0x455870 -> slot +0x60 (the original runs it on the sound thread when the voice ends)
void Scene3Room::checkSoundEnds() {
	for (int i = 0; i < kAreaSlots; i++) {
		if (_soundCallback[i] == kCbNone)
			continue;
		Slot &s = slot(i);
		if (!s.voice.active) {
			_soundCallback[i] = kCbNone;
			continue;
		}
		if (_game->isVoicePlaying(s.voice))
			continue;
		uint32 cb = _soundCallback[i];
		_soundCallback[i] = kCbNone;
		switch (cb) {
		case kCbTalk1:   // 0x464600
			_talk1Done = 1;
			break;
		case kCbTalk2:   // 0x464610
			_talk2Done = 1;
			break;
		case kCbStart:   // 0x464620
			_startDone = 1;
			break;
		default:
			break;
		}
	}
}

// ---- Ned ----

Ned *Scene3Room::ned() {
	return _game->ned();
}

// 0x454920(DAT_0051e1b0, input, &DAT_004c5750, &DAT_0051e178, now, 0, 0, DAT_0051dd10)
void Scene3Room::nedTick(uint32 input, uint32 now) {
	int f = 0;
	ned()->tick(_world, input, f, _nedPos, now, _scrollX, _scrollY, &_hooks);
}

// The loop inlined in 0x463be0 and 0x464040 (and the unreferenced copy 0x464630): Ned walks
// left until x <= 0x16c, drawn every pass, then turns right
void Scene3Room::nedWalkAway() {
	NightmareNedEngine *vm = _game->getEngine();
	while (kWalkToX < _nedPos.x && !vm->shouldQuit()) {
		nedTick(kInputLeft, _game->now());
		_game->display().draw();
		vm->pumpEvents();
		g_system->delayMillis(5);
	}
	nedTick(kInputRight, _game->now());
	_game->display().draw();
}

// 0x45a0f0(scene, -1), staying here when the target isn't registered yet
int Scene3Room::switchTo(int scene) {
	if (!_game->hasScene(scene, -1)) {
		if (_unportedExit != scene)
			debugC(1, kDebugLoading, "Scene 3:1: exit to scene %d (not ported), staying", scene);
		_unportedExit = scene;
		return 1;
	}
	_unportedExit = -1;
	_game->switchScene(scene, -1);
	return 0;
}

// ---- Scene methods ----

// m2 0x461640
int Scene3Room::load(int fromScene, int fromSub) {
	// Scene 3 keeps ned97000 open (DAT_0051f6c8); from anywhere else it is opened here
	if (fromScene == 3) {
		_ownNaf = 0;
	} else {
		if (!_game->getNaf(kNaf))
			return 5;
		_ownNaf = 1;
	}
	_channelSound = -1;
	_grabSlot = -1;
	_jumpSlot = -1;
	for (int i = 0; i < kProgCount; i++)
		runProgramme(i, 0, true);
	_talk1Done = 0;
	_opRun = 0;
	_startHeld = 0;
	_talk2Done = 0;
	_startDone = 0;
	_nedInput = 1;
	_programme = kProgNone;
	ned()->listLoad(kNedList);
	return submit(kList, kListCount, kNafScene);
}

// m3 0x461760
int Scene3Room::enter(int fromScene, int fromSub) {
	RdxSurface *primary = _game->display().getPrimary();
	primary->setClearColor(0);
	_game->display().draw();
	primary->setClearColor(-1);
	shuffle();
	ned()->setWrap1(false, 0, nullptr, nullptr);
	ned()->listShow(kNedList);
	int r = showBitmap(kBackdrop, 1000);
	if (r)
		return r;
	ned()->copyPalette(_palette);
	_game->display().applyPalette(_palette, kPalAll);
	if ((r = wait(kMap)) != 0)
		return r;

	// The world: the room's one map, no user region map
	_world = CollisionWorld();
	_world.setUser(nullptr);
	if (_map)
		_world.addMap(_map, _map->bounds.left, _map->bounds.top, _map->bounds.right, _map->bounds.bottom);

	if ((r = showSprite(kOp, 100)) != 0 || (r = showSprite(kHandset, 0x96)) != 0 ||
		(r = showSprite(kCards, 0x96)) != 0 || (r = showSprite(kSwitch0, 100)) != 0 ||
		(r = showSprite(kSwitch1, 100)) != 0 || (r = showSprite(kSwitch2, 100)) != 0 ||
		(r = showSprite(kButton, 100)) != 0)
		return r;

	// 0x45a560(2, 999, 2): 97999002.SST, applied by 0x462310
	_sst.clear();
	Common::String sstName = Common::String::format("%02d%03d0%02d.sst",
		(int16)_game->exe().readUint16(kSstAreas + 2 * kSstAreaIndex), 999, 2);
	Common::ScopedPtr<Common::SeekableReadStream> sst(_game->getEngine()->openFile(sstName));
	if (!sst || !loadPlacements(*sst, _sst)) {
		warning("Can't read %s", sstName.c_str());
		return 5;
	}
	for (const Placement &p : _sst)
		applyPlacement(this, p);

	// Ned stands at x 0x122 on the floor below y 0x1e0
	_nedPos.x = 0x122;
	_nedPos.y = 0x1e0;
	for (int y = 0x1e0; y >= 0; y--) {
		if (_map && _map->isSolid((int16)_nedPos.x, (int16)y)) {
			_nedPos.y = y;
			break;
		}
	}

	_targets.clear();
	switchSlot(eid(kCards), -1);
	place(eid(kCards), eid(kCards));
	switchSlot(eid(kHandset), -1);
	place(eid(kHandset), eid(kHandset));
	switchSlot(eid(kSwitch0), -1);
	place(eid(kSwitch0), eid(kSwitch0));
	switchSlot(eid(kSwitch1), -1);
	place(eid(kSwitch1), eid(kSwitch1));
	switchSlot(eid(kSwitch2), -1);
	place(eid(kSwitch2), eid(kSwitch2));
	switchSlot(eid(kButton), -1);
	slot(eid(kButton)).step = 1;
	place(eid(kButton), eid(kButton));
	_op = switchSlot(eid(kOp), -1);
	place(_op, eid(kOp));
	_opIdleTime = _game->now();
	pickChannel(true);
	targetAdd(eid(kSwitch0));
	targetAdd(eid(kSwitch1));
	targetAdd(eid(kSwitch2));
	targetAdd(eid(kButton));
	ned()->show(true);
	ned()->enable(true);
	nedTick(0, _game->now());
	if ((r = wait(kHum)) != 0)
		return r;
	playSound(eid(kHum));
	_game->_optionsAllowed = false;
	return 0;
}

// m4 0x461c80
int Scene3Room::leave(int toScene, int toSub) {
	// The debug overlay must not keep the maps freed below
	_game->display().setCollisionOverlay(Common::Array<CollisionMap *>(), Common::Point());
	_game->_optionsAllowed = false;
	_game->media().stopMusic();
	stopAllSounds();
	// 0x45a5f0(2, 999, 2, &DAT_0051e1d4)
	_sst.clear();
	_world = CollisionWorld();
	release(kList, kListCount);
	if (_ownNaf || toScene != 3)
		_game->closeNaf(kNaf);
	ned()->setWrap1(false, 0, nullptr, nullptr);
	ned()->listFree(kNedList);
	_targets.clear();
	return 0;
}

// m5 0x461d20
int Scene3Room::unload(int toScene, int toSub) {
	RdxSurface *primary = _game->display().getPrimary();
	primary->setClearColor(0);
	_game->display().draw();
	primary->setClearColor(-1);
	ned()->resetIdle();
	ned()->show(false);
	ned()->enable(false);
	return 0;
}

// m7 0x461d70
int Scene3Room::tick(uint32 input, uint32 now) {
	checkSoundEnds();
	int r = 1;
	if (_programme == kProgNone) {
		_game->_optionsAllowed = true;
	} else {
		_game->_optionsAllowed = false;
		r = runProgramme(_programme, now, false);
	}
	jumpTick(now);
	grabTick(now);
	operatorTick(now);
	if (!_nedInput)
		input = 0;
	nedTick(input, now);
	Common::Array<CollisionMap *> maps;
	if (_map)
		maps.push_back(_map);
	_game->display().setCollisionOverlay(maps, Common::Point(0, 0));
	_game->display().draw();
	debugC(3, kDebugGraphics, "Scene 3:1: input %x Ned %d,%d state %d slot %d op %d prog %d", input,
		_nedPos.x, _nedPos.y, ned()->getState(), ned()->getCurSlot(), _op, _programme);
	if (r == 0) {
		switchTo(_exitScene);
		return 0;
	}
	// The original ticks on every loop pass and waits for the flip; keep a short pace here
	uint32 t = g_system->getMillis();
	if (t < _lastTick + 10)
		g_system->delayMillis(_lastTick + 10 - t);
	_lastTick = g_system->getMillis();
	return 0;
}

// ---- The switchboard ----

// 0x4623d0: the eight programmes go to random free table slots, the last first;
// DAT_0051e128 gets 0xc..0x1a alongside (nothing reads it)
void Scene3Room::shuffle() {
	_game->crtSeed(_game->now());
	for (int i = 0; i < 8; i++) {
		_channelFn[i] = kProgNone;
		_channelData[i] = 0;
	}
	for (int i = kProgCount - 1; i >= 0; i--) {
		int k;
		do {
			int32 v = (int32)_game->crtRandom();
			k = v % 8;
		} while (_channelFn[k] != kProgNone);
		_channelFn[k] = i;
		_channelData[k] = 0xc + 2 * i;
	}
}

// The programme on line channel: *(0x4b0a48[channel].fn) (DAT_0051e150[k])
int Scene3Room::programmeAt(int channel) const {
	uint32 p = _game->exe().readUint32(kChannelTable + 0xc * channel);
	int k = (int)(p - kChannelFns) / 4;
	if (k < 0 || k >= 8)
		return kProgNone;
	return _channelFn[k];
}

// 0x462b60: the switches (step 0 = on) make the line number; its lit path replaces the old one
void Scene3Room::pickChannel(bool init) {
	const ExeImage &exe = _game->exe();
	int16 s0 = eid(kSwitch0), s1 = eid(kSwitch1), s2 = eid(kSwitch2);
	uint bits = slot(s0).step == 0 ? 1 : 0;
	if (slot(s1).step == 0)
		bits |= 2;
	if (slot(s2).step == 0)
		bits |= 4;
	int16 picture = (int16)exe.readUint16(kChannelTable + 4 + bits * 0xc);
	for (int i = 0; i < 8; i++) {
		if (eid(kPictureEntries[i]) == picture) {
			showSprite(kPictureEntries[i], 100);
			break;
		}
	}
	int16 old = init ? -1 : (int16)exe.readUint16(kChannelTable + 4 + _channel * 0xc);
	switchSlot(picture, old);
	slot(s0).step = (int16)(~bits & 1);
	slot(s1).step = (int16)(~(bits >> 1) & 1);
	slot(s2).step = (int16)(~(bits >> 2) & 1);
	place(picture, eid(kPicture0));
	place(s0, s0);
	place(s1, s1);
	place(s2, s2);
	_channel = bits;
}

// The programmes: the state functions DAT_0051e170 points at
int Scene3Room::runProgramme(int prog, uint32 now, bool reset) {
	switch (prog) {
	case kProg0:        // 0x462e00
		return talkProgramme(_talkState[0], 0x4b1144, 0x4b1168, true, reset);
	case kProg1:        // 0x4630c0
		return talkProgramme(_talkState[1], 0x4b118c, 0x4b11b0, false, reset);
	case kProg2:        // 0x4632a0
		return talkProgramme(_talkState[2], 0x4b11d4, 0x4b11f8, true, reset);
	case kProg3:        // 0x463560
		return talkProgramme(_talkState[3], 0x4b121c, 0x4b1240, false, reset);
	case kProgParents:  // 0x464040
		return parentsProgramme(now, reset);
	case kProgDragged:  // 0x463be0
		return draggedProgramme(now, reset);
	case kProg6:        // 0x463740
		return talkProgramme(_talkState[4], 0x4b1264, 0x4b1288, true, reset);
	case kProg7:        // 0x463a00
		return talkProgramme(_talkState[5], 0x4b12ac, 0x4b12d0, false, reset);
	default:
		return 1;
	}
}

/**
 * 0x462e00, 0x4630c0, 0x4632a0, 0x463560, 0x463740, 0x463a00: the caller speaks (voice1);
 * once the operator holds the phone he answers (voice2) and hangs up; then he laughs and
 * the monster (grab) or the swordsman comes for Ned
 */
int Scene3Room::talkProgramme(int &state, uint32 voice1, uint32 voice2, bool grab, bool reset) {
	if (reset) {
		state = 0;
		return 1;
	}
	int r;
	if (state == 0) {
		state = 1;
		if ((r = wait(voice1)) != 0)
			return r;
		_talk1Done = 0;
		playSoundNotify(eid(voice1), kCbTalk1);
		return 1;
	}
	if (_op == eid(kOpStart) && _talk1Done && state == 1) {
		state = 2;
		_talk1Done = 0;
		_opRun = 1;
		if ((r = wait(voice2)) != 0)
			return r;
		_talk2Done = 0;
		playSoundNotify(eid(voice2), kCbTalk2);
		return 1;
	}
	if (_op == eid(kOp) && _talk2Done && state == 2) {
		state = 0;
		if ((r = showSprite(kOpTalk, 100)) != 0)
			return r;
		_op = switchSlot(eid(kOpTalk), _op);
		if ((r = wait(kOpTalkSound)) != 0)
			return r;
		playSound(eid(kOpTalkSound));
		if (grab) {
			if ((r = showSprite(kGrab, 0x32)) != 0)
				return r;
			_grabSlot = switchSlot(eid(kGrab), -1);
			placeGrab();
			_grabY = (int16)slot(eid(kGrab)).y;
			if ((r = wait(kGrabSound)) != 0)
				return r;
			playSound(eid(kGrabSound));
		} else {
			if ((r = showSprite(kJump, 0x32)) != 0)
				return r;
			_jumpSlot = switchSlot(eid(kJump), -1);
			Slot &j = slot(eid(kJump));
			j.x = (int16)(j.w / 2 + (int16)_nedPos.x);
		}
		_programme = kProgNone;
	}
	return 1;
}

// The monster's x (the inline form of 0x464550): box 0 of step 3's frame centred on Ned's x
void Scene3Room::placeGrab() {
	Slot &s = slot(eid(kGrab));
	int frame = 0, duration = 0;
	if (s.sequence)
		s.sequence->getStep(3, frame, duration);
	int16 offX = 0;
	if (s.boxes && frame >= 0 && frame < (int)s.boxes->frames.size())
		offX = s.boxes->frames[frame].offX;
	int32 rect[4] = { -0x7fff, -0x7fff, -0x7fff, -0x7fff };
	if (s.boxes)
		s.boxes->getRawRect(frame, 0, rect);
	s.x = (offX - (rect[2] - rect[0]) / 2 - rect[0]) + _nedPos.x;
}

// 0x464040: Ned walks to the left, the parents come (178, 403), take him (179) and walk
// off to the left (404); then back to the corridor
int Scene3Room::parentsProgramme(uint32 now, bool reset) {
	if (reset) {
		_parentsSlot = -1;
		_parentsState = 0;
		return 1;
	}
	if (_parentsState == 0) {
		_opRun = 1;
		_parentsState = 1;
		return 1;
	}
	int r;
	if (_parentsState == 1 && _op == eid(kOp)) {
		nedWalkAway();
		if ((r = showSprite(kOpTalk, 100)) != 0)
			return r;
		_op = switchSlot(eid(kOpTalk), _op);
		if ((r = wait(kOpTalkSound)) != 0)
			return r;
		playSound(eid(kOpTalkSound));
		if ((r = showSprite(kParents0, 0x32)) != 0)
			return r;
		_parentsSlot = switchSlot(eid(kParents0), -1);
		stepNext(_parentsSlot, _parentsSlot, now);
		_parentsState = 2;
		return 1;
	}
	if (_parentsState != 2)
		return 1;
	Slot &s = slot(_parentsSlot);
	if (now <= s.deadline)
		return 1;
	s.step++;
	if (s.step < s.sequenceLength) {
		if (_parentsSlot == eid(kParents3))
			s.x -= 0x20;
	} else if (_parentsSlot == eid(kParents1)) {
		if ((r = showSprite(kParents2, 0x32)) != 0)
			return r;
		_parentsSlot = switchSlot(eid(kParents2), _parentsSlot);
	} else if (_parentsSlot == eid(kParents2)) {
		if ((r = wait(kParents2Sound)) != 0)
			return r;
		playSound(eid(kParents2Sound));
		ned()->show(false);
		ned()->enable(false);
		if ((r = showSprite(kParents3, 0x32)) != 0)
			return r;
		_parentsSlot = switchSlot(eid(kParents3), _parentsSlot);
		_parentsX = slot(_parentsSlot).x;
	} else if (_parentsSlot == eid(kParents0)) {
		if ((r = showSprite(kOpParents, 0x32)) != 0)
			return r;
		_op = switchSlot(eid(kOpParents), _op);
		if ((r = showSprite(kParents1, 0x32)) != 0)
			return r;
		_parentsSlot = switchSlot(eid(kParents1), _parentsSlot);
		if ((r = wait(kParents1Sound)) != 0)
			return r;
		playSound(eid(kParents1Sound));
	} else if (_parentsSlot == eid(kParents3)) {
		if (s.x < -399) {
			s.x = _parentsX;
			_parentsSlot = switchSlot(-1, _parentsSlot);
			_op = switchSlot(eid(kOp), _op);
			_exitScene = 3;
			_programme = kProgNone;
			_nedInput = 1;
			_parentsState = 0;
			return 0;
		}
		s.step = 0;
	}
	if (_parentsSlot != -1)
		stepNext(_parentsSlot, _parentsSlot, now);
	return 1;
}

// 0x463be0: Ned walks to the left, the patchwork man slides in (405), grabs him (176) and
// drags him off to the right (407); then out to the quilt
int Scene3Room::draggedProgramme(uint32 now, bool reset) {
	if (reset) {
		_draggedSlot = -1;
		_draggedState = 0;
		return 1;
	}
	if (_draggedState == 0) {
		_opRun = 1;
		_draggedState = 1;
		return 1;
	}
	int r;
	if (_draggedState == 1 && _op == eid(kOp)) {
		nedWalkAway();
		if ((r = showSprite(kOpTalk, 100)) != 0)
			return r;
		_op = switchSlot(eid(kOpTalk), _op);
		if ((r = wait(kOpTalkSound)) != 0)
			return r;
		playSound(eid(kOpTalkSound));
		if ((r = showSprite(kDragged0, 0x32)) != 0)
			return r;
		_draggedSlot = switchSlot(eid(kDragged0), -1);
		stepNext(_draggedSlot, _draggedSlot, now);
		_draggedState = 2;
		return 1;
	}
	if (_draggedState != 2)
		return 1;
	Slot &s = slot(_draggedSlot);
	if (now <= s.deadline)
		return 1;
	s.step++;
	if (s.step < s.sequenceLength) {
		if (_draggedSlot == eid(kDragged2)) {
			s.x += 0x20;
		} else if (_draggedSlot == eid(kDragged1) && s.step == 6) {
			if ((r = wait(kDragged1Sound)) != 0)
				return r;
			playSound(eid(kDragged1Sound));
		}
	} else if (_draggedSlot == eid(kDragged0)) {
		ned()->show(false);
		ned()->enable(false);
		if ((r = showSprite(kDragged1, 0x32)) != 0)
			return r;
		_draggedSlot = switchSlot(eid(kDragged1), _draggedSlot);
	} else if (_draggedSlot == eid(kDragged1)) {
		if ((r = showSprite(kDragged2, 0x32)) != 0)
			return r;
		_draggedSlot = switchSlot(eid(kDragged2), _draggedSlot);
		_draggedX = slot(_draggedSlot).x;
	} else if (_draggedSlot == eid(kDragged2)) {
		if (699 < s.x) {
			s.x = _draggedX;
			_draggedSlot = switchSlot(-1, _draggedSlot);
			_op = switchSlot(eid(kOp), _op);
			_exitScene = 0;
			_programme = kProgNone;
			_nedInput = 1;
			_draggedState = 0;
			return 0;
		}
		s.step = 0;
	}
	if (_draggedSlot != -1)
		stepNext(_draggedSlot, _draggedSlot, now);
	return 1;
}

// 0x4624c0: the operator. Idle (170) he fidgets (171) every 10 s. The button makes him pick
// up the phone (172), which holds at step 4 while the ring (259) plays, then starts the
// line's programme; once the programme lets him (DAT_0051e1bc) he hangs up and is idle again.
// 173 (laughing) and 174 loop until a programme puts him back.
void Scene3Room::operatorTick(uint32 now) {
	if (_op == -1)
		return;
	Slot *s = &slot(_op);
	if (now <= s->deadline)
		return;
	if (_op != eid(kOpStart)) {
		if (_op == eid(kOp) && _opIdleTime <= now) {
			showSprite(kOpIdle, 100);
			_op = switchSlot(eid(kOpIdle), _op);
		} else if (_op == eid(kOpIdle)) {
			s->step++;
			if (s->sequenceLength <= s->step) {
				_op = switchSlot(eid(kOp), _op);
				_opIdleTime = now + 10000;
			}
		} else if (_op == eid(kOpTalk)) {
			s->step++;
			if (s->sequenceLength <= s->step) {
				playSound(eid(kOpTalkSound));
				s->step = 0;
			}
			_opIdleTime = now + 10000;
		} else if (_op == eid(kOpParents)) {
			s->step++;
			if (s->sequenceLength <= s->step)
				s->step = 0;
			_opIdleTime = now + 10000;
		}
	} else {
		int16 step = s->step;
		bool run = false;
		if (step == 4) {
			if (!_startHeld) {
				_startHeld = 1;
				_startDone = 0;
				wait(kOpStartSound);
				playSoundNotify(eid(kOpStartSound), kCbStart);
			} else if (_startDone) {
				_startDone = 0;
				_programme = programmeAt(_channel);
			} else {
				run = true;
			}
		} else if (_startHeld && _startDone) {
			_startDone = 0;
			_programme = programmeAt(_channel);
		} else {
			run = true;
		}
		if (run) {
			if (!_opRun) {
				if (step < 4 && !_startHeld)
					s->step = step + 1;
			} else {
				_startHeld = 0;
				s->step++;
				if (s->sequenceLength <= s->step) {
					_opRun = 0;
					_op = switchSlot(eid(kOp), _op);
					_opIdleTime = now + 10000;
					Slot &b = slot(eid(kButton));
					b.step = b.step == 0 ? 1 : 0;
					place(eid(kButton), eid(kButton));
				}
			}
		}
		// The handset leaves the board at step 1 and is back at step 7
		if (slot(_op).step == 1)
			switchSlot(-1, eid(kHandset));
		if (slot(_op).step == 7) {
			switchSlot(eid(kHandset), -1);
			place(eid(kHandset), eid(kHandset));
		}
	}
	if (_op != -1) {
		int duration = place(_op, eid(kOp));
		Slot &o = slot(_op);
		if (_op == eid(kOpStart) && o.step == 4) {
			o.deadline = 0;
			return;
		}
		o.deadline = now + (int16)duration * kMsPerStep;
	}
}

// 0x4628a0: the monster grabs Ned (hidden from step 3 to step 0x26); step 2 is shown once
// 0x37 lower and step 0x26 once more at the old height
int Scene3Room::grabTick(uint32 now) {
	if (_grabSlot == -1)
		return 1;
	int16 id = eid(kGrab);
	Slot &s = slot(id);
	if (now <= s.deadline)
		return 1;
	s.step++;
	int16 step = s.step;
	if (s.sequenceLength <= step) {
		_grabSlot = switchSlot(-1, id);
		_nedInput = 1;
		_op = switchSlot(eid(kOp), _op);
		return 1;
	}
	if (step == 2 && s.y == _grabY) {
		s.y += 0x37;
		s.step = 1;
	} else if (step == 0x27 && s.y != _grabY) {
		s.y = _grabY;
		s.step = 0x26;
	} else if (step == 3) {
		ned()->show(false);
		ned()->enable(false);
	} else if (step == 0x26) {
		ned()->show(true);
		ned()->enable(true);
	}
	stepNext(id, id, now);
	return 1;
}

// 0x462a20: the swordsman; at step 0xc Ned jumps (action 0x24)
int Scene3Room::jumpTick(uint32 now) {
	if (_jumpSlot == -1)
		return 1;
	int16 id = eid(kJump);
	Slot &s = slot(id);
	if (s.deadline >= now)
		return 1;
	s.step++;
	if (s.sequenceLength <= s.step) {
		_jumpSlot = switchSlot(-1, id);
		_nedInput = 1;
		_op = switchSlot(eid(kOp), _op);
		return 1;
	}
	if (s.step == 0xc) {
		ned()->playAction(0x24, _nedPos, _scrollX, _scrollY);
		int r = wait(kJumpSound);
		if (r)
			return r;
		playSound(eid(kJumpSound));
	}
	stepNext(id, id, now);
	return 1;
}

// 0x462310 (olist_foreach on the .SST): an area-4 entry puts its slot at (x, y)
int Scene3Room::applyPlacement(Scene3Room *s, const Placement &p) {
	if (p.area == kArea && p.slot >= 0 && p.slot < kAreaSlots) {
		Slot &sl = s->slot(p.slot);
		sl.x = p.x;
		sl.y = p.y;
	}
	return 1;
}

// 0x4309c0 olist_add_sprite(&DAT_0051dd0c, 4, id, 1)
void Scene3Room::targetAdd(int id) {
	Placement p;
	memset(&p, 0, sizeof(p));
	p.area = kArea;
	p.slot = (int16)id;
	p.sub = 1;
	p.kind = 2;
	p.flip = 0;
	_targets.push_back(p);
}

// 0x462210: the switch or button in front of Ned, as a screen rect (box 0 of its frame)
int Scene3Room::findTarget(int x, int y, int16 *dir, int16 *found, int32 rect[4]) {
	*found = -1;
	int unused = 0;
	if (!ned()->findTarget(ned()->getFacing(), ned()->getCurSlot(), _targets, *dir, unused, *found))
		return 1;
	int16 id = *found;
	int pos[2] = { 0, 0 };
	if (id == eid(kSwitch0) || id == eid(kSwitch1) || id == eid(kSwitch2) || id == eid(kButton))
		attachPos(id, id, pos);
	Slot &s = slot(id);
	int frame = 0, duration = 0;
	if (s.sequence)
		s.sequence->getStep(s.step, frame, duration);
	if (s.boxes)
		s.boxes->getRawRect(frame, 0, rect);
	else
		rect[0] = rect[1] = rect[2] = rect[3] = -0x7fff;
	rect[0] += pos[0];
	rect[1] += pos[1];
	rect[3] += pos[1];
	rect[2] += pos[0];
	return 1;
}

// 0x461ea0 (+0x0c): zapping a switch changes the line and plays its jingle; zapping the
// button makes the operator pick up the phone and freezes Ned. Only while the operator is
// not already picking up and no programme runs.
int Scene3Room::hitTarget(int16 area, int16 found) {
	if (area != kArea || _op == eid(kOpStart) || _programme != kProgNone)
		return 1;
	if (found == eid(kSwitch0) || found == eid(kSwitch1) || found == eid(kSwitch2)) {
		stopSound(eid(kHum));
		Slot &sw = slot(found);
		sw.step = sw.step == 0 ? 1 : 0;
		if (_channelSound != -1)
			stopSound(_channelSound);
		pickChannel(false);
		wait(kChannelSoundLast);
		// The original tests 0x462e00 twice and never 0x463740: that line keeps the last jingle
		switch (programmeAt(_channel)) {
		case kProg0:
			_channelSound = 0xfc;
			break;
		case kProg1:
			_channelSound = 0xff;
			break;
		case kProg2:
			_channelSound = 0x100;
			break;
		case kProg3:
			_channelSound = 0xfa;
			break;
		case kProgParents:
			_channelSound = 0xfd;
			break;
		case kProgDragged:
			_channelSound = 0x101;
			break;
		case kProg7:
			_channelSound = 0x102;
			break;
		default:
			break;
		}
		if (_channelSound != -1)
			playSound(_channelSound);
		else
			debugC(1, kDebugSound, "Scene 3:1: line %d has no jingle yet (the original plays slot -1)", _channel);
	} else if (found == eid(kButton)) {
		stopSound(eid(kHum));
		if (_channelSound != -1)
			stopSound(_channelSound);
		_opRun = 0;
		showSprite(kOpStart, 100);
		_op = switchSlot(eid(kOpStart), _op);
		place(_op, eid(kOp));
		Slot &b = slot(found);
		b.step = b.step == 0 ? 1 : 0;
		place(found, found);
		_nedInput = 0;
	}
	return 1;
}

// ---- Hooks (DAT_0051dd10) ----

// 0x461e50 (+0x00): button 2 takes the direction 0x4500c0 found as state 0's slot
int Scene3Room::hookUse(void *user, int x, int y, int16 *state) {
	Scene3Room *s = (Scene3Room *)user;
	int16 dir = -1, found = -1;
	int32 rect[4] = { 0, 0, 0, 0 };
	s->findTarget(x, y, &dir, &found, rect);
	s->ned()->setUseSlot(dir);
	return 1;
}

// 0x461e40 (+0x04): every region takes state 0xe
int Scene3Room::hookRegion(void *user, int32 id, uint32 flags, int x, int y, int16 *state) {
	*state = 0xe;
	return 1;
}

// 0x4621d0 (+0x08): the area is 4; the direction is dropped
void Scene3Room::hookFindTarget(void *user, int x, int y, int16 *a, int16 *b, int32 rect[4]) {
	Scene3Room *s = (Scene3Room *)user;
	*a = 4;
	int16 dir = 4;
	s->findTarget(x, y, &dir, b, rect);
}

// 0x461ea0 (+0x0c)
int Scene3Room::hookHitTarget(void *user, int x, int y, int16 *a, int16 *b, const int32 rect[4]) {
	return ((Scene3Room *)user)->hitTarget(*a, *b);
}

} // End of namespace NightmareNed
