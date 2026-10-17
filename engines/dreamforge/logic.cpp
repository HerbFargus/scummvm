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
#include "common/endian.h"
#include "common/events.h"
#include "common/system.h"
#include "common/textconsole.h"

#include "dreamforge/actor.h"
#include "dreamforge/detection.h"
#include "dreamforge/dfa.h"
#include "dreamforge/dreamforge.h"
#include "dreamforge/logic.h"
#include "dreamforge/scene.h"
#include "dreamforge/sound.h"

namespace DreamForge {

Logic::Logic(DreamForgeEngine *vm) : _vm(vm), _dummyDir(0) {
}

void Logic::set(int flag) {
	_vm->setFlag(flag, true);
}

void Logic::clear(int flag) {
	_vm->setFlag(flag, false);
}

bool Logic::isSet(int flag) const {
	return _vm->isFlagSet(flag);
}

int32 &Logic::var(uint32 addr) {
	// Globals the engine itself keeps
	switch (addr) {
	case 0xE0FEC:
		return _vm->_newGame;
	case 0x6D264:
		return _vm->_counter3D;
	case 0x6D260:
		return _vm->_roomsSeen;
	default:
		return _vars[addr];
	}
}

void Logic::clearVars() {
	_vars.clear();
}

void Logic::syncVars(Common::Serializer &s) {
	uint32 count = _vars.size();
	s.syncAsUint32LE(count);
	if (s.isSaving()) {
		for (auto &v : _vars) {
			uint32 addr = v._key;
			int32 value = v._value;
			s.syncAsUint32LE(addr);
			s.syncAsSint32LE(value);
		}
	} else {
		_vars.clear();
		for (uint32 i = 0; i < count; i++) {
			uint32 addr = 0;
			int32 value = 0;
			s.syncAsUint32LE(addr);
			s.syncAsSint32LE(value);
			_vars[addr] = value;
		}
	}
}

int Logic::act() const {
	return _vm->getScene() ? _vm->getScene()->_act : 0;
}

int Logic::area() const {
	return _vm->getScene() ? _vm->getScene()->_area : 0;
}

int Logic::room() const {
	return _vm->getScene() ? _vm->getScene()->_room : 0;
}

int Logic::sceneWidth() const {
	return _vm->getScene() ? (int)_vm->getScene()->_width : (int)kScreenWidth;
}

uint32 Logic::ticks() const {
	return _vm->ticks();
}

int &Logic::guy() {
	return _vm->_currentActor;
}

int &Logic::guyY(int g) {
	Actor *a = _vm->getActor(g);
	return a ? a->_y : _dummyDir;
}

int &Logic::guyX(int g) {
	Actor *a = _vm->getActor(g);
	return a ? a->_x : _dummyDir;
}

int Logic::guyDir(int g) {
	Actor *a = _vm->getActor(g);
	return a ? a->_dir : 0;
}

int &Logic::held() {
	return _vm->_heldItem;
}

void Logic::setDir(int dir) {
	for (int n = 1; n <= 2; n++)
		if (guy() == n || guy() == 3)
			_vm->getActor(n)->setDirection(dir);
}

void Logic::oneShot(int slot, int dir) {
	Actor *a = _vm->getActor(guy());
	if (!a)
		return;
	a->setDirection(dir);
	_vm->actorPlay(*a, slot);
}

void Logic::walk(int y, int x, int dir) {
	_vm->actorWalk(guy(), y, x, dir >= 0 && dir < 8 ? dir : -1);
}

void Logic::turn(int from, int to) {
	Actor *a = _vm->getActor(guy());
	if (a)
		_vm->actorTurn(*a, to);
}

void Logic::say(int voice, int g, bool quick) {
	_vm->actorSay(g, voice, quick);
}

void Logic::banter(int set) {
	_vm->banter(set);
}

void Logic::hide(int g) {
	_vm->hideActors(g);
}

void Logic::show(int g) {
	_vm->showActors(g);
}

bool Logic::isHidden(int g) {
	Actor *a = _vm->getActor(g);
	return a && a->_hidden;
}

bool Logic::isLying(int g) {
	Actor *a = _vm->getActor(g);
	return a && a->_posture == kLying;
}

bool Logic::isSitting(int g) {
	Actor *a = _vm->getActor(g);
	return a && a->_posture == kSitting;
}

void Logic::clearLying(int g) {
	Actor *a = _vm->getActor(g);
	if (a && a->_posture == kLying)
		a->_posture = kStanding;
}

bool Logic::isWalking(int g) {
	Actor *a = _vm->getActor(g);
	return a && a->isWalking();
}

bool Logic::isBusy(int g) {
	Actor *a = _vm->getActor(g);
	return a && (a->isWalking() || a->_busy);
}

void Logic::stopWalking(int g) {
	for (int n = 1; n <= 2; n++)
		if (g == n || g == 3)
			_vm->getActor(n)->stopWalking();
}

bool Logic::atPos(int y, int x, int g) {
	Actor *a = _vm->getActor(g);
	return a && a->_y == y && a->_x == x;
}

void Logic::setPos(int y, int x) {
	Actor *a = _vm->getActor(guy());
	if (a) {
		a->_y = y;
		a->_x = x;
	}
}

void Logic::faceEachOther() {
	Actor *p1 = _vm->getActor(1), *p2 = _vm->getActor(2);
	_vm->actorTurn(*p1, p1->directionTo(p2->_y, p2->_x));
	_vm->actorTurn(*p2, p2->directionTo(p1->_y, p1->_x));
}

void Logic::guy2FacesGuy1() {
	Actor *p1 = _vm->getActor(1), *p2 = _vm->getActor(2);
	_vm->actorTurn(*p2, p2->directionTo(p1->_y, p1->_x));
}

int Logic::dirTo(int y0, int x0, int y1, int x1) {
	int d = directionFromDelta(y1 - y0, x1 - x0);
	return d < 0 ? 0 : d;
}

void Logic::sceneAnim(int cine, int top, int left, int flip, int delay, int depth, int noWait, int hideGuy) {
	_vm->playSceneAnim(cine, top, left, flip != 0, delay, depth, noWait != 0, hideGuy);
}

int Logic::cutscene(int n, int mode, int a, int b, int c, int d, int e) {
	// TODO: the mode arguments (what's on screen before and after, 0x15650)
	return _vm->playCutscene(n);
}

void Logic::changeScene(int y1, int x1, int d1, int y2, int x2, int d2, int act, int area, int room) {
	_vm->getActor(1)->place(y1, x1, d1);
	_vm->getActor(2)->place(y2, x2, d2);
	_vm->loadScene(act, area, room);
}

void Logic::redraw(int reload, int present) {
	_vm->redraw();
}

int Logic::recState(int id, int type) {
	Scene *s = _vm->getScene();
	if (!s || id < 0 || id >= kStateCount)
		return -1;
	if (type == kRecLayer) {
		for (auto *l : s->_layers)
			if (l->id == id)
				return _vm->_layerState[id];
	} else if (type == kRecObject) {
		for (auto *o : s->_objects)
			if (o->id == id)
				return _vm->_objectState[id];
	} else if (type == kRecHotspot) {
		for (auto &h : s->_hotspots)
			if (h.id == id)
				return _vm->_hotspotState[id];
	}
	return -1;
}

int Logic::hotspotIdAt(int index) {
	Scene *s = _vm->getScene();
	return s && index >= 0 && index < (int)s->_hotspots.size() ? s->_hotspots[index].id : -1;
}

int Logic::hotspotTopAt(int index) {
	Scene *s = _vm->getScene();
	return s && index >= 0 && index < (int)s->_hotspots.size() ? s->_hotspots[index].rect.top : 0;
}

int Logic::hotspotHeightAt(int index) {
	Scene *s = _vm->getScene();
	return s && index >= 0 && index < (int)s->_hotspots.size() ? s->_hotspots[index].rect.height() : 0;
}

int Logic::layerIdAt(int index) {
	Scene *s = _vm->getScene();
	return s && index >= 0 && index < (int)s->_layers.size() ? s->_layers[index]->id : -1;
}

int Logic::layerDepthAt(int index) {
	Scene *s = _vm->getScene();
	if (!s || index < 0 || index >= (int)s->_layers.size() || s->_layers[index]->data.size() < 0x14)
		return 0;
	return (int32)READ_LE_UINT32(s->_layers[index]->data.data() + 0x10);
}

int Logic::objectDepthAt(int index) {
	Scene *s = _vm->getScene();
	return s && index >= 0 && index < (int)s->_objects.size() ? s->_objects[index]->depth : 0;
}

void Logic::setObjectIdAt(int index, int id) {
	Scene *s = _vm->getScene();
	if (s && index >= 0 && index < (int)s->_objects.size())
		s->_objects[index]->id = id;
}

int Logic::objectCount() {
	Scene *s = _vm->getScene();
	return s ? (int)s->_objects.size() : 0;
}

void Logic::dropHeld() {
	_vm->dropHeldItem();
}

void Logic::loadWalkMap(int act, int area, int room) {
	Scene *s = _vm->getScene();
	Scene other;
	if (!s || !other.load(act, area, room))
		return;
	s->_walkMap = other._walkMap;
	s->_edges = other._edges;
	_vm->_walkMap.load(s->_walkMap);
}

void Logic::roomDone() {
	_vm->roomEventDone();
}

static SceneObject *findObject(DreamForgeEngine *vm, int id) {
	Scene *s = vm->getScene();
	if (s)
		for (auto *o : s->_objects)
			if (o->id == id)
				return o;
	return nullptr;
}

void Logic::objFrame(int id, int frame) {
	SceneObject *o = findObject(_vm, id);
	if (!o || !o->dfa)
		return;
	o->flags &= ~0x10;
	o->frame = -1;
	o->dfa->rewind();
	if (frame != -1)
		while (o->frame != frame && o->frame < o->dfa->getFrameCount() - 1)
			_vm->advanceObject(*o);
	_vm->markDirty();
}

void Logic::objPos(int id, int top, int left) {
	SceneObject *o = findObject(_vm, id);
	if (o)
		o->rect.moveTo(left, top);
	_vm->markDirty();
}

void Logic::objHide(int id) {
	SceneObject *o = findObject(_vm, id);
	if (o)
		o->flags2 |= 0x80;
	_vm->markDirty();
}

void Logic::objShow(int id) {
	SceneObject *o = findObject(_vm, id);
	if (o)
		o->flags2 &= ~0x80;
	_vm->markDirty();
}

void Logic::objLoop(int id) {
	SceneObject *o = findObject(_vm, id);
	if (o)
		o->flags = (o->flags & ~0x10) | 0x08;
}

void Logic::objNoLoop(int id) {
	SceneObject *o = findObject(_vm, id);
	if (o)
		o->flags &= ~0x08;
}

void Logic::objRewind(int id) {
	SceneObject *o = findObject(_vm, id);
	if (!o || !o->dfa)
		return;
	o->frame = -1;
	o->flags &= ~0x10;
	o->dfa->rewind();
	_vm->advanceObject(*o);
	_vm->markDirty();
}

void Logic::objStop(int id) {
	SceneObject *o = findObject(_vm, id);
	if (o)
		o->flags |= 0x10;
}

void Logic::objNow(int id) {
	SceneObject *o = findObject(_vm, id);
	if (o)
		o->nextTick = 0;
}

void Logic::objTimer(int id, uint32 tick) {
	SceneObject *o = findObject(_vm, id);
	if (o)
		o->nextTick = tick;
}

bool Logic::objDone(int id) {
	SceneObject *o = findObject(_vm, id);
	return o && (o->flags & 0x10);
}

bool Logic::objCond(int id) {
	SceneObject *o = findObject(_vm, id);
	return o && (o->temporary || _vm->conditionsHold(o->data.data() + 0x272, 9));
}

int Logic::objFrameOf(int id) {
	SceneObject *o = findObject(_vm, id);
	return o ? o->frame : -1;
}

int Logic::voice(int id) {
	return _vm->getSound()->startVoice(id);
}

int Logic::sfx(int id, int volume, int loop) {
	return _vm->getSound()->startSfx(id, volume, loop != 0);
}

void Logic::waitSound(int handle, int keepGoing) {
	while (_vm->getSound()->isPlaying(handle) && !_vm->shouldQuit()) {
		if (keepGoing) {
			_vm->runStep();
		} else {
			Common::Event event;
			while (g_system->getEventManager()->pollEvent(event)) {
			}
			g_system->delayMillis(10);
		}
	}
}

void Logic::stopSound(int handle) {
	_vm->getSound()->stop(handle);
}

bool Logic::soundPlaying(int handle) {
	return _vm->getSound()->isPlaying(handle);
}

void Logic::music(int track, int loop) {
	_vm->getSound()->playMusic(track, loop != 0);
}

void Logic::stopMusic() {
	_vm->getSound()->stopMusic();
}

int Logic::rand15() {
	return _vm->getRandom15();
}

void Logic::step() {
	uint32 target = _vm->_nextStep;
	while (_vm->_nextStep == target && !_vm->shouldQuit())
		_vm->runStep();
}

void Logic::fadePalette(int first, int count, int r, int g, int b, int speed) {
	debugC(1, kDebugScript, "TODO: palette fade %d+%d to (%d, %d, %d), speed %d", first, count, r, g, b, speed);
}

void Logic::todo(const char *what) {
	warning("Room code not ported yet: %s", what);
}

} // End of namespace DreamForge
