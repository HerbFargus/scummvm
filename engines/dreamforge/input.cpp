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
#include "common/textconsole.h"

#include "dreamforge/act1_click.h"
#include "dreamforge/act1_hooks.h"
#include "dreamforge/act1_post.h"
#include "dreamforge/act1_use.h"
#include "dreamforge/act2.h"
#include "dreamforge/actor.h"
#include "dreamforge/detection.h"
#include "dreamforge/dfa.h"
#include "dreamforge/dreamforge.h"
#include "dreamforge/logic.h"
#include "dreamforge/scene.h"
#include "dreamforge/script.h"

namespace DreamForge {

// What a click does depends on the cursor, i.e. on the mask bit of what's under it
// (C.PRG 0x21140). Each verb has a pipeline per kind of record, and each pipeline a
// "before" and an "after" act handler table.
struct VerbTables {
	uint16 mask;
	uint32 pre[3], post[3];  // layer, hotspot, object
};

enum Verb {
	kVerb8 = 0,     // cursor 1
	kVerbLook = 1,  // cursor 2
	kVerb10 = 2,    // cursor 3
	kVerb20 = 3,    // cursor 4
	kVerbUse = 4,   // cursor 5, or something held
	kVerb40 = 5     // cursor 6
};

static const VerbTables kVerbs[] = {
	{ 0x08, { 0xDFA48, 0xDFA38, 0xDFA58 }, { 0xDFA18, 0xDFA08, 0xDFA28 } },  // 0x27fe0 0x27af0 0x28520
	{ 0x04, { 0xDF9E8, 0xDF9D8, 0xDF9F8 }, { 0xDF9B8, 0xDF9A8, 0xDF9C8 } },  // 0x269c0 0x273c0 0x26ed0
	{ 0x10, { 0xDFAF8, 0xDFB08, 0xDFB18 }, { 0xDFAC8, 0xDFAD8, 0xDFAE8 } },  // 0x29a40 0x29f70 0x2a450
	{ 0x20, { 0xDFB58, 0xDFB68, 0xDFB78 }, { 0xDFB28, 0xDFB38, 0xDFB48 } },  // 0x2a950 0x2ae80 0x2b360
	{ 0x02, { 0xDF94C, 0xDF93C, 0xDF95C }, { 0xDF90C, 0xDF8FC, 0xDF91C } },  // 0x25e20 0x258c0 0x263d0
	{ 0x40, { 0xDFA98, 0xDFAA8, 0xDFAB8 }, { 0xDFA68, 0xDFA78, 0xDFA88 } }   // 0x28a30 0x28fb0 0x294e0
};

// The act's handler in one of the per-act tables; 'none' when the act has none.
// Only act 1 is ported; most of its "before" entries are 0x35ae0, which returns 0.
int DreamForgeEngine::callActHandler(uint32 table, int a, int b, int c, int none) {
	if (!_scene)
		return none;
	if (_scene->_act == 2)
		return callAct2Handler(table, a, b, c, none);
	if (_scene->_act != 1)
		return none;
	Logic &L = *_logic;
	switch (table) {
	case 0x88370:
		return act1_40cb0(L, a) ? 1 : 0;
	case 0x6DE38:
		act1_3f780(L);
		return 0;
	case 0xDF020:
		return act1_43d60(L, a, b, c);
	case 0xDF030:
		return act1_46c50(L, a);
	case 0xDF010:
		act1_3d200(L);
		return 0;
	case 0xE1304:
		act1_3c970(L);
		return 0;
	case 0xDF92C:
		act1_3c610(L);
		return 0;
	case 0xDF93C:
		return act1_3c860(L, a, b);
	case 0xDF94C:
		return act1_3c890(L, a, b);
	case 0xDF96C:
		return act1_3c7e0(L, a, b);
	case 0xDF97C:
		return act1_3c820(L, a, b);
	case 0xDF9A8:
		act1_37540(L, a);
		return 0;
	case 0xDF9B8:
		act1_379c0(L, a);
		return 0;
	case 0xDF9C8:
		act1_371d0(L, a);
		return 0;
	case 0xDFA08:
		act1_37e80(L, a);
		return 0;
	case 0xDFA18:
		act1_37e20(L, a);
		return 0;
	case 0xDFA28:
		act1_37e50(L, a);
		return 0;
	case 0xDFAC8:
		return act1_35af0(L, a);
	case 0xDFB28:
		return act1_37ea0(L, a);
	case 0xDFB38:
		return act1_3bed0(L, a);
	case 0xDFB48:
		return act1_3c5b0(L, a);
	case 0xDF8FC:
	case 0xDF91C:
		// 0x3c8d0: using something on a hotspot or an object just drops it
		dropHeldItem();
		return 0;
	case 0xDF90C: {
		int result = 0;
		act1_3c8e0(L, &result, c, a, b);
		return result;
	}
	case 0xDF95C: case 0xDF98C:
	case 0xDF9D8: case 0xDF9E8: case 0xDF9F8:
	case 0xDFA38: case 0xDFA48: case 0xDFA58:
	case 0xDFA68: case 0xDFA78: case 0xDFA88:
	case 0xDFA98: case 0xDFAA8: case 0xDFAB8:
	case 0xDFAD8: case 0xDFAE8: case 0xDFAF8:
	case 0xDFB08: case 0xDFB18:
	case 0xDFB58: case 0xDFB68: case 0xDFB78:
		return 0;  // 0x35ae0
	default:
		warning("No act 1 handler for table 0x%x", table);
		return none;
	}
}

// Act 2 (the finale) fills most of its tables with stubs
int DreamForgeEngine::callAct2Handler(uint32 table, int a, int b, int c, int none) {
	Logic &L = *_logic;
	switch (table) {
	case 0x88370:
		return act2_48b40(L, a) ? 1 : 0;
	case 0x6DE38:
		act2_48a80(L);
		return 0;
	case 0xDF020:
	case 0xDF030:
		return 1;  // 0x48e30
	case 0xDF010:
	case 0xE1304:
	case 0xDF92C:
		return 0;  // 0x48710
	case 0xDF8FC:
	case 0xDF90C:
	case 0xDF91C:
		dropHeldItem();  // 0x48720
		return 0;
	case 0xDFA08:
		// 0x486f0, 0x48690, 0x486c0: the talk depth, read by id as an array index as the original does
		L.var(0xE0AD4) = L.hotspotTopAt(a) + L.hotspotHeightAt(a) - 1;
		return 0;
	case 0xDFA18:
		L.var(0xE0AD4) = L.layerDepthAt(a);
		return 0;
	case 0xDFA28:
		L.var(0xE0AD4) = L.objectDepthAt(a);
		return 0;
	default:
		return 0;  // 0x48670 and 0x48680
	}
}

// 0x257a0
void DreamForgeEngine::dropHeldItem() {
	callActHandler(0xDF92C, 0, 0, 0, 0);
	_logic->var(0x64A34) = -1;
	_logic->var(0x64A38) = -1;
	_logic->var(0x64A3C) = -1;
	_heldItem = -1;
}

void DreamForgeEngine::postInput(int y, int x, int code) {
	InputEvent e = { y, x, code };
	_input.push(e);
}

void DreamForgeEngine::processInput() {
	while (!_input.empty() && !_inScript && !shouldQuit()) {
		InputEvent e = _input.pop();
		_inScript = true;
		dispatchInput(e);
		_inScript = false;
		_dirty = true;
	}
}

void DreamForgeEngine::clickAndWait(int y, int x) {
	if (x < _scrollX || x >= _scrollX + kScreenWidth)
		setScroll(x - kScreenWidth / 2);
	Common::Point screen(x - _scrollX, y);
	int code = cursorFor(screen);
	debugC(1, kDebugScript, "test click at (%d, %d), cursor %d", x, y, code);
	postInput(y, x, code);
	uint32 end = g_system->getMillis() + 30000;
	do {
		processInput();
		runStep();
		checkRegions();
		checkArrivals();
	} while ((!_input.empty() || _actors[0]->isWalking() || _actors[1]->isWalking() || _actors[0]->_arrivalCode != -1 ||
		_actors[1]->_arrivalCode != -1) && !shouldQuit() && g_system->getMillis() < end);
}

// 0x18d00: a puppy at the end of a deferred walk posts its click again
void DreamForgeEngine::checkArrivals() {
	for (int n = 0; n < 2; n++) {
		Actor *a = _actors[n];
		if (a->_arrivalCode == -1 || a->isWalking() || a->_busy)
			continue;
		postInput(a->_arrivalY, a->_arrivalX, a->_arrivalCode + 0x80);
		a->_arrivalCode = -1;
	}
}

// 0x1f590: the hotspot under (y, x) with a bit of mask (by index); -2 / -3 for puppy 1 / 2
int DreamForgeEngine::hotspotIndexAt(int y, int x, uint16 mask) const {
	if (!_scene)
		return -1;
	if (_checkPuppies) {
		// TODO: the original tests the puppies' pixels (0x124b0, 0x12430, 0x16400)
		for (int n = 1; n <= 2; n++) {
			const Actor *a = _actors[n - 1];
			if (!a->_hidden && a->drawRect(0, _walkMap.scaleAt(this, a->_y, a->_x), getRefScale()).contains(x, y))
				return n == 1 ? -2 : -3;
		}
	}
	for (uint i = 0; i < _scene->_hotspots.size(); i++) {
		const Hotspot &h = _scene->_hotspots[i];
		if ((h.mask & mask) && h.rect.contains(Common::Point(x, y)) && conditionsHold(h.data.data() + 0x496, 9))
			return i;
	}
	return -1;
}

// 0x1faa0: the front-most shown layer under (y, x) with a bit of mask, by its pixels
int DreamForgeEngine::layerIndexAt(int y, int x, uint16 mask) const {
	if (!_scene)
		return -1;
	int best = -1, bestDepth = INT_MIN;
	for (uint i = 0; i < _scene->_layers.size(); i++) {
		const Layer *l = _scene->_layers[i];
		if (l->data.size() < 0x284 || !l->surface.getPixels())
			continue;
		uint16 m = READ_LE_UINT16(l->data.data() + 0x270);
		if (!(m & mask) || !l->rect.contains(Common::Point(x, y)) || !conditionsHold(l->data.data() + 0x272, 9))
			continue;
		int px = x - l->rect.left, py = y - l->rect.top;
		if (px >= l->surface.w || py >= l->surface.h || !*(const byte *)l->surface.getBasePtr(px, py))
			continue;
		int depth = (int32)READ_LE_UINT32(l->data.data() + 0x10);
		if (depth >= bestDepth) {
			best = i;
			bestDepth = depth;
		}
	}
	return best;
}

// 0x1fbe0: the front-most object under (y, x) with a bit of mask
int DreamForgeEngine::objectIndexAt(int y, int x, bool temporary, uint16 mask) const {
	if (!_scene)
		return -1;
	int best = -1, bestDepth = INT_MIN;
	for (uint i = 0; i < _scene->_objects.size(); i++) {
		const SceneObject *o = _scene->_objects[i];
		if (o->temporary && !temporary)
			continue;
		if (o->data.size() < 0x284 || !o->dfa || o->frame < 0 || (o->flags2 & 0x80))
			continue;
		uint16 m = READ_LE_UINT16(o->data.data() + 0x270);
		if (!(m & mask) || !conditionsHold(o->data.data() + 0x272, 9))
			continue;
		if (!o->rect.contains(Common::Point(x, y)))
			continue;
		if (o->depth >= bestDepth) {
			best = i;
			bestDepth = o->depth;
		}
	}
	return best;
}

Record *DreamForgeEngine::recordById(int kind, int id) {
	if (!_scene)
		return nullptr;
	if (kind == kKindLayer) {
		for (auto *l : _scene->_layers)
			if (l->id == id)
				return l;
	} else if (kind == kKindHotspot) {
		for (auto &h : _scene->_hotspots)
			if (h.id == id)
				return &h;
	} else {
		for (auto *o : _scene->_objects)
			if (o->id == id)
				return o;
	}
	return nullptr;
}

int32 *DreamForgeEngine::recordState(int kind, int id) {
	if (id < 0 || id >= kStateCount)
		return nullptr;
	if (kind == kKindLayer)
		return &_layerState[id];
	if (kind == kKindHotspot)
		return &_hotspotState[id];
	return &_objectState[id];
}

// 0x20d90: the act's pre-filter sees every click first; 0 means it took it
int DreamForgeEngine::preFilterClick(int y, int x) {
	int o = objectIndexAt(y, x, false, 0xFFFF);
	int objId = o >= 0 ? _scene->_objects[o]->id : o;
	int l = layerIndexAt(y, x, 0xFFFF);
	int layerId = l >= 0 ? _scene->_layers[l]->id : l;
	int h = hotspotIndexAt(y, x, 0xFFFF);
	int hotId = h >= 0 ? _scene->_hotspots[h].id : h;
	if (hotId == -3 || hotId == -2)
		return 1;
	return callActHandler(0xDF020, objId, layerId, hotId, 1);
}

static bool atWalkPoint(DreamForgeEngine *vm, const Record &rec, int g) {
	const Actor *a = vm->getActor(g);
	return rec.walkY[g - 1] == -1 || (a->_y == rec.walkY[g - 1] && a->_x == rec.walkX[g - 1]);
}

static bool bothAtWalkPoints(DreamForgeEngine *vm, const Record &rec) {
	return rec.walkY[0] == -1 || rec.walkY[1] == -1 || (atWalkPoint(vm, rec, 1) && atWalkPoint(vm, rec, 2));
}

/**
 * One verb on one record (0x258c0, 0x25e20, 0x263d0, 0x269c0, 0x273c0, 0x26ed0,
 * 0x27af0 .. 0x2b360): if the puppy that acts isn't at the record's walk-to point
 * it walks there and the click comes back when it arrives; otherwise the record's
 * script runs with its visit count, then the act's "after" handler.
 */
int DreamForgeEngine::runVerb(int verb, int kind, int id, int y, int x, int code) {
	const VerbTables &t = kVerbs[verb];
	int held = _heldItem;
	// 0x25830 / 0x25d90 / 0x26340: can the held thing be used here at all?
	if (verb == kVerbUse && held != -1) {
		static const uint32 kValid[3] = { 0xDF97C, 0xDF96C, 0xDF98C };
		if (!callActHandler(kValid[kind], id, held, 0, 1)) {
			dropHeldItem();
			return 0;
		}
	}
	Record *rec = id >= 0 ? recordById(kind, id) : nullptr;
	bool inPlace = true;
	if (rec) {
		switch (rec->who) {
		case 1:
		case 2:
			inPlace = atWalkPoint(this, *rec, rec->who);
			break;
		case 3:
			inPlace = bothAtWalkPoints(this, *rec);
			break;
		case 4:
			if (_currentActor == 0)
				inPlace = false;
			else if (_currentActor < 3)
				inPlace = atWalkPoint(this, *rec, _currentActor);
			else
				inPlace = bothAtWalkPoints(this, *rec);
			break;
		default:
			break;
		}
	}
	bool forced = callActHandler(t.pre[kind], id, held, 0, 1) != 0;
	if (forced)
		inPlace = true;
	if (rec) {
		_logic->stopWalking(_currentActor);  // 0x18940
		if (rec->who != 4)
			_currentActor = rec->who;
		if (!inPlace && (((_currentActor == 1 || _currentActor == 3) && rec->walkY[0] == -1) ||
				((_currentActor == 2 || _currentActor == 3) && rec->walkY[1] == -1) || _currentActor == 0 || _currentActor == 4))
			inPlace = true;
	}

	if (!inPlace) {
		// 0x1b3f0: a walk that posts this click again on arrival
		for (int n = 1; n <= 2; n++) {
			if ((_currentActor != n && _currentActor != 3) || rec->walkY[n - 1] == -1)
				continue;
			Actor *a = _actors[n - 1];
			if (a->walkTo(rec->walkY[n - 1], rec->walkX[n - 1])) {
				a->_arrivalCode = code;
				a->_arrivalY = y;
				a->_arrivalX = x;
			}
		}
		return 0;
	}

	// TODO: 0x17160 (the stop-walking animation) and 0x1c3a0 (the idle puppy turns to the other)
	if (rec) {
		int g = _currentActor;
		if (g >= 1 && g <= 2 && rec->walkPose[g - 1] != kDirKeep && !forced) {
			actorTurn(*_actors[g - 1], rec->walkPose[g - 1]);
			_logic->step();
		}
		int32 *state = recordState(kind, id);
		int visits = state ? ++*state : 1;
		debugC(1, kDebugScript, "verb %d on %s %d \"%s\", visit %d", verb, kind == kKindLayer ? "layer" :
			kind == kKindHotspot ? "hotspot" : "object", id, rec->name.c_str(), visits);
		ScriptRunner runner(this);
		runner.run(rec->script(), visits, Common::Point(rec->rect.left + rec->rect.width() / 2, rec->rect.top + rec->rect.height() / 2));
	}
	return callActHandler(t.post[kind], id, held, code, 0);
}

// 0x1f320: a layer (or a puppy in front of it), then a hotspot, then an object with the mask
int DreamForgeEngine::verbClick(int verb, int y, int x, int code) {
	uint16 mask = kVerbs[verb].mask;
	bool tryHotspot = true, tryObject = true;
	int r = 0;
	int l = layerIndexAt(y, x, mask);
	if (l != -1) {
		int h = hotspotIndexAt(y, x, mask);
		int v = (h == -2 || h == -3) ? runVerb(verb, kKindHotspot, h, y, x, code) :
			runVerb(verb, kKindLayer, _scene->_layers[l]->id, y, x, code);
		if (v) {
			tryHotspot = tryObject = false;
			r = 1;
		}
	}
	if (tryHotspot && _scene) {
		int h = hotspotIndexAt(y, x, mask);
		if (h != -1 && runVerb(verb, kKindHotspot, h >= 0 ? _scene->_hotspots[h].id : h, y, x, code)) {
			tryObject = false;
			r = 1;
		}
	}
	if (tryObject && _scene) {
		int o = objectIndexAt(y, x, false, mask);
		if (o != -1 && runVerb(verb, kKindObject, _scene->_objects[o]->id, y, x, code))
			r = 1;
	}
	return r;
}

// 0x1fe00: a click (mask 4) on one named layer, hotspot or object
void DreamForgeEngine::lookClick(int y, int x, int code) {
	int l = layerIndexAt(y, x, 4);
	if (l != -1 && !_scene->_layers[l]->name.empty()) {
		runVerb(kVerbLook, kKindLayer, _scene->_layers[l]->id, y, x, code);
		return;
	}
	int h = hotspotIndexAt(y, x, 4);
	if (h >= 0 && !_scene->_hotspots[h].name.empty()) {
		runVerb(kVerbLook, kKindHotspot, _scene->_hotspots[h].id, y, x, code);
		return;
	}
	int o = objectIndexAt(y, x, true, 4);
	if (o != -1 && !_scene->_objects[o]->name.empty())
		runVerb(kVerbLook, kKindObject, _scene->_objects[o]->id, y, x, code);
}

// 0x20f40: a plain click walks puppy 1 there
void DreamForgeEngine::walkClick(int y, int x, bool arrow) {
	if (!_walkMap.isWalkable(this, y, x) && !arrow)
		return;
	_logic->stopWalking(_currentActor);
	if (!arrow) {
		_currentActor = 1;
		_actors[0]->walkTo(y, x);
		return;
	}
	// TODO: the branch where puppy 2 runs the act's handler instead (0xdf040, flag 0x10, 0x48460)
	// An exit: the other puppy turns its way and the one that goes walks to its walk-to point
	_currentActor = 1;
	int h = hotspotIndexAt(y, x, 0xFFFF);
	if (h >= 0) {
		const Hotspot &rec = _scene->_hotspots[h];
		int g = (rec.who == 1 || rec.who == 2) ? rec.who ^ 3 : 2;
		Actor *other = _actors[g - 1];
		int dir = rec.walkPose[g - 1];
		if (dir > 7)
			dir = other->directionTo(y, x);
		actorTurn(*other, dir);
		g ^= 3;
		_currentActor = g;
		if (rec.walkY[g - 1] != -1) {
			// The original reads puppy 1's point whichever puppy goes
			y = rec.walkY[0];
			x = rec.walkX[0];
		}
	}
	_actors[_currentActor - 1]->walkTo(y, x);
}

// 0x1fef0: use (mask 2) or a held item, or a click on a puppy
void DreamForgeEngine::useClick(int y, int x, int code) {
	int32 &layer = _logic->var(0x64A34), &hot = _logic->var(0x64A38), &obj = _logic->var(0x64A3C);
	bool open = true;
	int l = layerIndexAt(y, x, 2);
	if (l != -1) {
		if (_scene->_layers[l]->name.empty()) {
			layer = -1;
		} else {
			hot = obj = -1;
			layer = l;
			open = false;
		}
	}
	if (open) {
		int h = hotspotIndexAt(y, x, 2);
		if (h != -1) {
			hot = h;
			if (h <= -2) {
				layer = obj = -1;
				open = false;
			} else if (_scene->_hotspots[h].name.empty()) {
				hot = -1;
			} else {
				layer = obj = -1;
				open = false;
			}
		}
	}
	if (open) {
		int o = objectIndexAt(y, x, false, 2);
		if (o != -1) {
			if (_scene->_objects[o]->name.empty()) {
				obj = -1;
			} else {
				layer = hot = -1;
				obj = o;
			}
		}
	}
	if (hot == -1 && layer == -1 && obj == -1) {
		dropHeldItem();
		return;
	}
	if (hot == -3 && _heldItem == -1) {
		// Puppy 2 clicked
		callActHandler(0xDF010, 0, 0, 0, 0);
	} else if (hot == -2 && _heldItem == -1) {
		// Puppy 1 clicked: he barks (standing up first if he's sitting), then the act's handler
		int g = _currentActor;
		_currentActor = 1;
		Actor *a = _actors[0];
		if (a->_posture == kSitting) {
			actorPlay(*a, kSlotStandUp);
			actorPlay(*a, kSlotStandIdle);
		}
		actorPlay(*a, a->_posture == kLying ? kSlotLieBark : kSlotBark);
		a->idle();
		_currentActor = g;
		callActHandler(0xE1304, 0, 0, 0, 0);
	} else if (hot == -1) {
		if (layer != -1)
			runVerb(kVerbUse, kKindLayer, _scene->_layers[layer]->id, y, x, code);
		else if (obj != -1)
			runVerb(kVerbUse, kKindObject, _scene->_objects[obj]->id, y, x, code);
	} else {
		runVerb(kVerbUse, kKindHotspot, hot >= 0 ? _scene->_hotspots[hot].id : (int)hot, y, x, code);
	}
}

// 0x21140
void DreamForgeEngine::dispatchInput(const InputEvent &e) {
	int code = e.code;
	if (code >= 0x80) {
		_checkPuppies = false;
		code -= 0x80;
	}
	int raw = code;
	bool arrow = false;
	if (code >= 8 && code < 0x10) {
		arrow = true;
		code = 0;
	}
	if (code > 0x11)
		code = 5;
	debugC(1, kDebugScript, "input (%d, %d) code %d%s", e.y, e.x, code, arrow ? " (exit)" : "");
	if (preFilterClick(e.y, e.x)) {
		switch (code) {
		case 0:
		case 0x10:
		case 0x11: {
			// 0x20d10: the act may take over a click on an exit
			bool walk = true;
			if (arrow) {
				int h = hotspotIndexAt(e.y, e.x, 1);
				if (h >= 0)
					walk = callActHandler(0xDF030, _scene->_hotspots[h].id, 0, 0, 1) != 0;
			}
			if (walk)
				walkClick(e.y, e.x, arrow);
			else
				_logic->step();
			break;
		}
		case 1:
			verbClick(kVerb8, e.y, e.x, raw);
			break;
		case 2:
			lookClick(e.y, e.x, raw);
			break;
		case 3:
			verbClick(kVerb10, e.y, e.x, raw);
			break;
		case 4:
			verbClick(kVerb20, e.y, e.x, raw);
			break;
		case 5:
			useClick(e.y, e.x, raw);
			break;
		case 6:
			verbClick(kVerb40, e.y, e.x, raw);
			break;
		default:
			break;
		}
	}
	_checkPuppies = true;
}

} // End of namespace DreamForge
