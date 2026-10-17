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

#include "common/algorithm.h"
#include "common/debug.h"
#include "common/debug-channels.h"
#include "common/endian.h"
#include "common/textconsole.h"

#include "mspam/compound.h"
#include "mspam/detection.h"
#include "mspam/media.h"
#include "mspam/runtime.h"
#include "mspam/sound.h"

namespace MSPAM {

enum {
	kMaxFrameTime = 100,       // ms of game time per frame at most
	kMaxSegmentsPerUpdate = 64,
	kMaxListsPerUpdate = 5000,
	kDoubleClickTime = 500,
	kFadeFull = 256
};

/** Palette transitions (TREPAL): the first word of the description */
enum {
	kFadeToBlack = 1,
	kFadeFromBlack = 2
};

const PathSegment *ElementState::segment() const {
	if (seg < 0 || seg >= (int)def->segments.size())
		return nullptr;
	return &def->segments[seg];
}

// The speed percentage stretches the segment's time, but not its frame rate
uint32 ElementState::segmentDuration() const {
	const PathSegment *s = segment();
	if (!s)
		return 0;
	return (uint32)((uint64)s->duration * 100 / MAX<uint32>(speed, 1));
}

int LayoutState::find(const Common::String &name) const {
	for (uint i = 0; i < elements.size(); i++) {
		if (elements[i].def->name.equalsIgnoreCase(name))
			return i;
	}
	return -1;
}

Runtime::Runtime(MediaManager *media, const CompoundFile &tdf, const Title *title, Audio::Mixer *mixer) :
	_media(media), _tdf(tdf), _title(title), _layout(nullptr), _nextSerial(1), _reenter(false), _quit(false),
	_now(0), _lastUpdate(0), _blockedUntil(0), _leaving(false), _fadeFrom(kFadeFull), _fadeTo(kFadeFull),
	_fadeLevel(kFadeFull), _fadeStart(0), _fadeDuration(0), _skitsOpen(false), _lastDownTime(0),
	_random("mspam") {
	_sound = new SoundManager(mixer, media);
	_eval = new Evaluator(_vars, *this);
	memset(_palette, 0, sizeof(_palette));
	MediaManager::setStaticColors(_palette);
	memcpy(_screenPalette, _palette, sizeof(_palette));
}

Runtime::~Runtime() {
	delete _sound;
	delete _eval;
	for (auto &it : _cache)
		delete it._value;
}

Common::String Runtime::getLayoutName() const {
	return _layout ? _layout->layout->_name : Common::String();
}

bool Runtime::isIdle() const {
	return _queue.empty() && _now >= _blockedUntil && _pendingLayout.empty() && _fadeLevel == _fadeTo;
}

void Runtime::start(const Common::String &layout) {
	requestLayout(layout);
	switchLayout();
}

// Layouts

LayoutState *Runtime::loadLayoutState(const Common::String &name) {
	int index = _title->findLayout(name);
	if (index < 0)
		return nullptr;
	Layout *layout = _title->loadLayout(_tdf, index);
	if (!layout)
		return nullptr;
	LayoutState *ls = new LayoutState();
	ls->layout = layout;
	ls->elements.resize(layout->_elements.size());
	for (uint i = 0; i < layout->_elements.size(); i++) {
		Element &def = layout->_elements[i];
		ElementState &e = ls->elements[i];
		e.def = &def;
		e.visible = def.visible;
		e.x = def.x;
		e.y = def.y;
		e.z = def.z;
		e.rank = 0;
		e.seg = def.segments.empty() ? -1 : 0;
		for (uint s = 0; s < def.segments.size(); s++) {
			if (def.segments[s].id == def.initialSegment)
				e.seg = s;
		}
		e.running = false;
		e.elapsed = 0;
		e.animTicks = 0;
		e.speed = def.speed ? def.speed : 100;
		e.iterations = def.iterations;
		e.iterationsLeft = 0;
		e.soundId = 0;
	}
	for (uint r = 0; r < layout->_siteOrder.size(); r++)
		ls->elements[layout->_siteOrder[r]].rank = r;
	// The site is the top-left corner of the first picture; the path runs from its registration point
	for (ElementState &e : ls->elements) {
		const PathSegment *seg = e.segment();
		const PibList *list = seg ? _media->getPibList(seg->pibList) : nullptr;
		const Pib *pib = list ? list->getFrame(0) : nullptr;
		if (pib) {
			e.x += pib->regX;
			e.y += pib->regY;
		}
	}
	return ls;
}

void Runtime::requestLayout(const Common::String &name) {
	if (_leaving) {
		debugC(1, kDebugActions, "Ignoring a layout change to %s while leaving", name.c_str());
		return;
	}
	if (_title->findLayout(name) < 0) {
		warning("No layout '%s' to go to", name.c_str());
		return;
	}
	_pendingLayout = name;
}

// The old layout runs its leave list (without waiting for palette transitions),
// its queued lists are dropped, then the new layout's enter list is queued
void Runtime::switchLayout() {
	Common::String name = _pendingLayout;
	_pendingLayout.clear();
	if (name.empty())
		return;
	bool reenter = _reenter;
	_reenter = false;

	if (_layout) {
		debugC(1, kDebugActions, "Leaving %s for %s", _layout->layout->_name.c_str(), name.c_str());
		if (_layout->layout->_lists.size() > 1) {
			_leaving = true;
			QueuedList item;
			item.serial = _layout->serial;
			item.owner = -1;
			item.list = &_layout->layout->_lists[1];
			item.pos = 0;
			runList(item);
			_leaving = false;
		}
		for (ElementState &e : _layout->elements)
			_sound->stopChannel(e.sound);
		_queue.clear();
		_blockedUntil = 0;
		Common::String old = _layout->layout->_name;
		if (_layout->lowPriority || _layout->reset) {
			_cache.erase(old);
			delete _layout;
		}
		_layout = nullptr;
		_media->clearCache();
	}

	if (reenter)
		debugC(1, kDebugActions, "Coming back to %s", name.c_str());
	LayoutState *ls = _cache.contains(name) ? _cache[name] : nullptr;
	if (!ls) {
		ls = loadLayoutState(name);
		if (!ls) {
			warning("Layout %s doesn't load", name.c_str());
			return;
		}
		_cache[name] = ls;
	}
	ls->serial = _nextSerial++;
	ls->reset = false;
	_layout = ls;
	_visited[ls->layout->_name]++;
	debugC(1, kDebugActions, "Entering %s", ls->layout->_name.c_str());

	if (ls->layout->_paletteId && !_media->loadPalette(ls->layout->_paletteId, _palette))
		warning("Layout %s: palette %x is missing", ls->layout->_name.c_str(), ls->layout->_paletteId);
	// A layout that doesn't fade itself in shows at once
	bool fades = false;
	if (!ls->layout->_lists.empty()) {
		for (const Action &a : ls->layout->_lists[0].actions) {
			if (a.targetKind < kTargetTitle && a.id == 0 && a.target.equalsIgnoreCase(ls->layout->_name))
				fades = true;
		}
		queueList(-1, ls->layout->_lists[0]);
	}
	if (!fades)
		_fadeLevel = _fadeFrom = _fadeTo = kFadeFull;
}

// Action lists and events

void Runtime::queueList(int owner, const ActionList &list, bool front) {
	if (list.actions.empty())
		return;
	QueuedList item;
	item.serial = _layout->serial;
	item.owner = owner;
	item.list = &list;
	item.pos = 0;
	if (front)
		_queue.push_front(item);
	else
		_queue.push_back(item);
}

void Runtime::fireEvent(int el, uint16 event, uint32 segmentId) {
	ElementState &e = _layout->elements[el];
	for (const Trigger &t : e.def->triggers) {
		if (t.kind != 1 || t.event != event)
			continue;
		if (t.segmentId && t.segmentId != segmentId)
			continue;
		debugC(2, kDebugActions, "%s: %s %x", e.def->name.c_str(), getEventName(event, 1), segmentId);
		queueList(el, t.list);
	}
}

void Runtime::processQueue() {
	for (int n = 0; n < kMaxListsPerUpdate; n++) {
		if (_queue.empty() || _now < _blockedUntil || _quit)
			return;
		QueuedList item = _queue.front();
		_queue.pop_front();
		if (!_layout || item.serial != _layout->serial)
			continue;
		runList(item);
		if (item.pos < item.list->actions.size()) {
			// Waiting for a palette transition
			_queue.push_front(item);
			return;
		}
		if (!_pendingLayout.empty())
			switchLayout();
	}
	warning("Action lists keep queueing more lists");
}

void Runtime::runList(QueuedList &item) {
	while (item.pos < item.list->actions.size() && !_quit) {
		const Action &a = item.list->actions[item.pos++];
		executeAction(item.owner, a);
		if (_now < _blockedUntil && !_leaving)
			return;
	}
}

void Runtime::executeAction(int owner, const Action &a) {
	if (gDebugLevel >= 1 && DebugMan.isDebugChannelEnabled(kDebugActions)) {
		Common::String who = owner >= 0 ? _layout->elements[owner].def->name : _layout->layout->_name;
		debugC(1, kDebugActions, "%s: %s", who.c_str(), describeAction(a, _layout->layout).c_str());
	}
	if (a.targetKind == kTargetTitle) {
		titleAction(a);
		return;
	}
	if (a.targetKind == kTargetSound) {
		soundAction(a);
		return;
	}
	// AL::FINDTARGET: up targetKind levels from the owner (an element is level 0,
	// a layout 1), then the child with the target name
	int level = (owner >= 0 ? 0 : 1) + a.targetKind - 1;
	if (level <= 0) {
		int el = _layout->find(a.target);
		if (el < 0) {
			if (!_warned.contains(a.target))
				warning("%s: no element '%s'", _layout->layout->_name.c_str(), a.target.c_str());
			_warned[a.target] = true;
			return;
		}
		elementAction(el, a);
	} else if (level == 1) {
		layoutAction(a);
	} else if (level == 2) {
		viewAction(a);
	} else {
		unimplemented(a, "unknown target");
	}
}

void Runtime::unimplemented(const Action &a, const char *what) {
	Common::String name = Common::String::format("%s (%s)", getActionName(a, _layout->layout).c_str(), what);
	if (!_unimplemented.contains(name))
		debugC(1, kDebugActions, "Unimplemented: %s", name.c_str());
	_unimplemented[name]++;
}

static int32 paramValue(const Action &a, uint i) {
	return i < a.params.size() ? (int32)a.params[i].value : 0;
}

static Common::String paramString(const Action &a, uint i) {
	return i < a.params.size() ? a.params[i].str : Common::String();
}

void Runtime::elementAction(int el, const Action &a) {
	ElementState &e = _layout->elements[el];
	if (e.def->isVideo()) {
		switch (a.id) {
		case 7:
			setVisible(el, true);
			break;
		case 8:
			setVisible(el, false);
			break;
		case 16:
		case 20:
			// Until M4 plays them, videos finish as soon as they start
			debugC(1, kDebugActions, "Video %s (%s) skipped", e.def->name.c_str(), _media->getVideoPath(e.def->videoId).toString().c_str());
			unimplemented(a, "video");
			fireEvent(el, kEventVideoPlay);
			fireEvent(el, kEventVideoStop);
			break;
		case 17:
		case 18:
		case 19:
		case 21:
			break;
		default:
			unimplemented(a, "video");
			break;
		}
		return;
	}

	switch (a.id) {
	case 7:
		setVisible(el, true);
		break;
	case 8:
		setVisible(el, false);
		break;
	case 9:
		startSprite(el);
		break;
	case 10:
		stopSprite(el);
		break;
	case 11:
		e.speed = MAX<int32>(paramValue(a, 0), 1);
		break;
	case 12:
		e.iterations = paramValue(a, 0);
		e.iterationsLeft = e.iterations;
		break;
	case 13:
		// The action's segment numbers count from 1, the expressions' .pa from 0
		setPathSegment(el, paramValue(a, 0) - 1);
		break;
	default:
		unimplemented(a, "sprite");
		break;
	}
}

void Runtime::layoutAction(const Action &a) {
	switch (a.id) {
	case 0:
		changePalette(paramValue(a, 0), a.params.size() > 1 ? &a.params[1] : nullptr, true);
		break;
	case 1:
	case 2:
		// The transition for the next layout change; layout changes cut for now
		break;
	default:
		unimplemented(a, "layout");
		break;
	}
}

void Runtime::viewAction(const Action &a) {
	switch (a.id) {
	case 0:
		requestLayout(paramString(a, 0));
		break;
	case 1: {
		int count = _title->_layouts.size();
		int index = _title->findLayout(getLayoutName()) + paramValue(a, 0);
		requestLayout(_title->_layouts[(index % count + count) % count]);
		break;
	}
	case 2: {
		Common::String name = paramString(a, 0);
		bool low = paramString(a, 1).equalsIgnoreCase("Low");
		for (auto &it : _cache) {
			if (name == "*" || it._key.equalsIgnoreCase(name))
				it._value->lowPriority = low;
		}
		break;
	}
	case 3:
	case 4: {
		Common::String name = paramString(a, 0);
		if (a.id == 4) {
			int count = _title->_layouts.size();
			int index = _title->findLayout(getLayoutName()) + paramValue(a, 0);
			name = _title->_layouts[(index % count + count) % count];
		}
		if (_layout && _layout->layout->_name.equalsIgnoreCase(name)) {
			_layout->reset = true;
		} else if (_cache.contains(name)) {
			delete _cache[name];
			_cache.erase(name);
		}
		break;
	}
	default:
		unimplemented(a, "view");
		break;
	}
}

void Runtime::titleAction(const Action &a) {
	switch (a.id) {
	case 0x00: // Show View
	case 0x01: // Hide View
	case 0x0f: // Set View: the title has one view
	case 0x17: // Debug: Comment
	case 0x18: // Debug: Level
	case 0x1c: // Idle Mode
	case 0x1e: // Stop idle/transition
		break;
	case 0x02:
		_quit = true;
		break;
	case 0x03: case 0x04: case 0x05: case 0x06: case 0x07: case 0x08: case 0x09: case 0x0a:
	case 0x10: case 0x11: case 0x14: case 0x15:
		unimplemented(a, "bus flight");
		break;
	case 0x0b:
		_eval->execute(_eval->test(paramString(a, 0)) ? paramString(a, 1) : paramString(a, 2));
		break;
	case 0x0c:
		_eval->execute(paramString(a, 0));
		break;
	case 0x0d: {
		// Eval: Start Sprite Expr: the sprite named by a variable (unused by the title)
		int el = _layout->find(_vars.get(paramString(a, 0)));
		if (el >= 0)
			startSprite(el);
		break;
	}
	case 0x0e: {
		// Set Path Seg Mid: a path segment's PibList (unused by the title)
		int el = _layout->find(paramString(a, 0));
		int seg = paramValue(a, 1);
		if (el >= 0 && seg >= 0 && seg < (int)_layout->elements[el].def->segments.size())
			_layout->elements[el].def->segments[seg].pibList = paramValue(a, 2);
		break;
	}
	case 0x12: {
		// Eval: Rand number (FUN_1008_5eea): a value up to the maximum that the
		// uniqueness variable's bit set hasn't had yet
		uint max = CLIP<int32>(paramValue(a, 1), 0, 31);
		uint32 mask = _vars.randomMask(paramString(a, 2));
		uint32 all = max == 31 ? 0xffffffff : (2u << max) - 1;
		if ((mask & all) == all)
			mask = 0;
		uint value;
		do {
			value = _random.getRandomNumber(max);
		} while (mask & (1u << value));
		_vars.set(paramString(a, 0), Common::String::format("%u", value));
		_vars.randomMask(paramString(a, 2)) = mask | (1u << value);
		break;
	}
	case 0x13:
		_vars.free(paramString(a, 0));
		break;
	case 0x16:
		// Exec: Start Game: until M6, the arcade game counts as won with its coin;
		// the title then comes back to the layout as after LobotomyGameResult
		debugC(1, kDebugActions, "BUSGAME '%s' skipped", paramString(a, 0).c_str());
		unimplemented(a, "busgame");
		_eval->execute("GVarGameWin=1");
		_eval->execute("GVarCoin=1");
		_reenter = true;
		requestLayout(getLayoutName());
		break;
	case 0x19: {
		int32 value = _eval->execute(paramString(a, 0));
		if (value >= 0 && value + 1 < (int32)a.params.size())
			_eval->execute(paramString(a, value + 1));
		break;
	}
	case 0x1a: {
		// Eval: Enum Sprites: every element matching the name (a trailing '%'
		// matches a prefix) goes into the variable; '#' in the expression
		// becomes the element's index
		Common::String pattern = paramString(a, 1);
		bool prefix = pattern.hasSuffix("%");
		if (prefix)
			pattern.deleteLastChar();
		Common::String expr = paramString(a, 2);
		for (uint i = 0; i < _layout->elements.size(); i++) {
			const Common::String &name = _layout->elements[i].def->name;
			if (prefix ? !name.hasPrefix(pattern) : name != pattern)
				continue;
			_vars.set(paramString(a, 0), name);
			Common::String e = expr;
			size_t hash = e.findFirstOf('#');
			if (hash != Common::String::npos)
				e = Common::String(e.c_str(), hash) + Common::String::format("%u", i) + Common::String(e.c_str() + hash + 1);
			_eval->execute(e);
		}
		break;
	}
	case 0x1d:
		// Start idle/transition: through the empty Transition layout, which goes on to GVarLayout
		_eval->execute("GVarLayout='" + paramString(a, 0));
		requestLayout("Transition");
		break;
	case 0x1f:
		_eval->execute("GVarLayout='" + _vars.get(paramString(a, 0)));
		requestLayout("Transition");
		break;
	case 0x20:
		// Start transition layout: frees the skits and the flight
		_skits.clear();
		_skitsOpen = false;
		break;
	case 0x21:
		requestLayout(_vars.get(paramString(a, 0)));
		break;
	case 0x22:
		_skits.clear();
		_skits.push_back(Skit());
		_skitsOpen = true;
		break;
	case 0x23: {
		// AVI Skit Start: until M4 the skit ends at once, running its expressions
		uint n = paramValue(a, 0);
		unimplemented(a, "video");
		if (n >= _skits.size()) {
			warning("No AVI skit %d", n);
			break;
		}
		Skit skit = _skits[n];
		for (const Common::String &expr : skit.exprs) {
			if (!expr.empty() && expr != " ")
				_eval->execute(expr);
		}
		if (!skit.stopExpr.empty() && skit.stopExpr != " ")
			_eval->execute(skit.stopExpr);
		break;
	}
	case 0x24:
		if (!_skits.empty())
			_skits.back().exprs.push_back(paramString(a, 6));
		break;
	case 0x25:
		if (!_skits.empty()) {
			_skits.back().stopExpr = paramString(a, 0);
			_skits.push_back(Skit());
		}
		break;
	case 0x26:
		_skits.clear();
		_skitsOpen = false;
		break;
	default:
		unimplemented(a, "title");
		break;
	}
}

void Runtime::soundAction(const Action &a) {
	Common::String group = paramString(a, 1);
	switch (a.id) {
	case 0x200:
		// Mix 0 = "Mix Audio", 1 = "Don't Mix Audio"; flags 1 = Auto Kill, 2 = Auto Loop
		_sound->play(paramValue(a, 0), group, paramValue(a, 2) == 1, paramValue(a, 3));
		break;
	case 0x201:
		_sound->add(paramValue(a, 0), group, paramValue(a, 2) == 1);
		break;
	case 0x202:
		_sound->playGroup(paramString(a, 0));
		break;
	case 0x203:
		_sound->stopGroup(paramString(a, 0));
		break;
	case 0x205:
	case 0x206:
		_sound->pauseGroup(paramString(a, 0), a.id == 0x205);
		break;
	case 0x207:
		_sound->killGroup(paramString(a, 0));
		break;
	case 0x208:
		_sound->killAll();
		break;
	default:
		unimplemented(a, "sound");
		break;
	}
}

// Sprites

void Runtime::setVisible(int el, bool visible) {
	ElementState &e = _layout->elements[el];
	if (e.visible == visible)
		return;
	e.visible = visible;
	fireEvent(el, visible ? kEventShow : kEventHide);
}

void Runtime::startSprite(int el) {
	ElementState &e = _layout->elements[el];
	if (e.running || !e.segment())
		return;
	e.running = true;
	e.iterationsLeft = MAX<uint32>(e.iterations, 1);
	e.animTicks = 0;
	fireEvent(el, kEventSpriteStart);
	beginSegment(el);
}

void Runtime::stopSprite(int el) {
	ElementState &e = _layout->elements[el];
	if (!e.running)
		return;
	e.running = false;
	_sound->stopChannel(e.sound);
	fireEvent(el, kEventSpriteStop);
}

void Runtime::setPathSegment(int el, int index) {
	ElementState &e = _layout->elements[el];
	if (index < 0 || index >= (int)e.def->segments.size()) {
		debugC(1, kDebugActions, "%s has no path segment %d", e.def->name.c_str(), index);
		return;
	}
	e.seg = index;
	e.elapsed = 0;
	e.animTicks = 0;
	if (e.running)
		beginSegment(el);
}

void Runtime::beginSegment(int el) {
	ElementState &e = _layout->elements[el];
	const PathSegment *seg = e.segment();
	e.elapsed = 0;
	if (!(seg->flags & kSegContinueFrames))
		e.animTicks = 0;
	if (seg->sound && (!(seg->flags & kSegContinueSound) || !_sound->isPlaying(e.sound))) {
		_sound->playChannel(e.sound, seg->sound);
		e.soundId = seg->sound;
	}
	fireEvent(el, kEventPathSegBegin, seg->id);
}

// A running sprite plays its path segments in order, each for its duration at
// the sprite's speed, with its animation at the segment's frame rate. The path
// repeats for the number of iterations, then the sprite stops.
void Runtime::updateSprite(int el, uint32 dt) {
	ElementState &e = _layout->elements[el];
	for (int guard = 0; e.running && guard < kMaxSegmentsPerUpdate; guard++) {
		const PathSegment *seg = e.segment();
		uint32 duration = e.segmentDuration();
		uint32 step = e.elapsed < duration ? MIN(dt, duration - e.elapsed) : 0;
		e.elapsed += step;
		e.animTicks += step * seg->frames;
		dt -= step;
		if (e.elapsed < duration)
			return;
		fireEvent(el, kEventPathSegEnd, seg->id);
		if (e.seg + 1 < (int)e.def->segments.size()) {
			e.seg++;
		} else if (e.iterationsLeft > 1) {
			e.iterationsLeft--;
			e.seg = 0;
		} else {
			e.running = false;
			fireEvent(el, kEventSpriteStop);
			return;
		}
		beginSegment(el);
	}
}

// Sprite positions are those of the registration point. Each segment starts
// where the one before ends and goes to its end point, relative to its start
// (flag 1) or absolute.
void Runtime::getPathPoint(const ElementState &e, int32 &x, int32 &y, int32 &z) const {
	int64 px = e.x, py = e.y, pz = e.z;
	for (int i = 0; i <= e.seg && i < (int)e.def->segments.size(); i++) {
		const PathSegment &seg = e.def->segments[i];
		int64 tx = seg.x, ty = seg.y, tz = seg.z;
		if (seg.flags & kSegRelative) {
			tx += px;
			ty += py;
			tz += pz;
		}
		if (i == e.seg) {
			uint32 duration = e.segmentDuration();
			uint32 t = MIN(e.elapsed, duration);
			if (!duration)
				t = duration = e.elapsed ? 1 : 0;
			if (!duration)
				break;
			tx = px + (tx - px) * t / duration;
			ty = py + (ty - py) * t / duration;
			tz = pz + (tz - pz) * t / duration;
		}
		px = tx;
		py = ty;
		pz = tz;
	}
	x = (int32)px;
	y = (int32)py;
	z = (int32)pz;
}

Common::Point Runtime::getPosition(const ElementState &e) const {
	if (e.def->isVideo())
		return Common::Point(e.x, e.y);
	int32 x, y, z;
	getPathPoint(e, x, y, z);
	return Common::Point(x, y);
}

int32 Runtime::getZ(const ElementState &e) const {
	if (e.def->isVideo())
		return e.z;
	int32 x, y, z;
	getPathPoint(e, x, y, z);
	return z;
}

// The top-left corner of the current frame
Common::Point Runtime::getOrigin(const ElementState &e, const Pib *pib) const {
	Common::Point pos = getPosition(e);
	if (pib) {
		pos.x -= pib->regX;
		pos.y -= pib->regY;
	}
	return pos;
}

// The frame for the animation position: through the frame map's hold ticks
// (or the pictures in order), looping or holding the last frame
const Pib *Runtime::getFrame(const ElementState &e) const {
	const PathSegment *seg = e.segment();
	const PibList *list = seg ? _media->getPibList(seg->pibList) : nullptr;
	if (!list || list->pibs.empty())
		return nullptr;
	bool loop = seg->flags & kSegLoopFrames;
	uint32 tick = e.animTicks / 1000;
	if (list->frames.empty()) {
		uint n = list->pibs.size();
		return list->pibs[loop ? tick % n : MIN<uint32>(tick, n - 1)];
	}
	uint32 total = 0;
	for (const PibList::Frame &f : list->frames)
		total += f.hold;
	uint index = list->frames.size() - 1;
	if (total) {
		if (tick >= total)
			tick = loop ? tick % total : total;
		for (uint i = 0; i < list->frames.size(); i++) {
			if (tick < list->frames[i].hold) {
				index = i;
				break;
			}
			tick -= list->frames[i].hold;
		}
	} else {
		index = 0;
	}
	uint pib = list->frames[index].pib;
	return pib < list->pibs.size() ? list->pibs[pib] : nullptr;
}

void Runtime::sortElements() {
	Common::Array<uint> &order = _layout->order;
	order.resize(_layout->elements.size());
	for (uint i = 0; i < order.size(); i++)
		order[i] = i;
	Common::Array<int32> z(order.size());
	for (uint i = 0; i < order.size(); i++)
		z[i] = getZ(_layout->elements[i]);
	const Common::Array<ElementState> &el = _layout->elements;
	Common::sort(order.begin(), order.end(), [&](uint a, uint b) {
		if (z[a] != z[b])
			return z[a] < z[b];
		return el[a].rank < el[b].rank;
	});
}

int Runtime::findElementState(const Common::String &name) const {
	return _layout ? _layout->find(name) : -1;
}

// A visible sprite is hit inside one of its current segment's hotspots or on an
// opaque pixel; while it moves, only if the segment says so (FUN_1048_4aa2)
int Runtime::hitTest(const ElementState &e, int x, int y, uint32 &hotspotId) const {
	hotspotId = 0;
	if (!e.visible)
		return 0;
	if (e.def->isVideo())
		return x >= e.x && y >= e.y && x < e.x + (int)e.def->width && y < e.y + (int)e.def->height;
	const PathSegment *seg = e.segment();
	if (!seg)
		return 0;
	if (e.running && (seg->x || seg->y) && !(seg->flags & kSegHotWhileMoving))
		return 0;
	const Pib *pib = getFrame(e);
	Common::Point origin = getOrigin(e, pib);
	int lx = x - origin.x, ly = y - origin.y;
	for (const Hotspot &h : seg->hotspots) {
		if (h.contains(lx, ly)) {
			hotspotId = h.id;
			return 1;
		}
	}
	return pib && pib->isOpaque(lx, ly);
}

// Mouse messages go to the frontmost element under the mouse that has a
// matching trigger; elements without one let the message through
void Runtime::mouseEvent(uint16 msg, int x, int y) {
	if (!_layout || _now < _blockedUntil || !_pendingLayout.empty())
		return;
	if (msg == kEventLButtonDown) {
		if (_now - _lastDownTime < kDoubleClickTime && ABS(x - _lastDownPos.x) < 4 && ABS(y - _lastDownPos.y) < 4) {
			msg = kEventLButtonDblClk;
			_lastDownTime = 0;
		} else {
			_lastDownTime = _now;
			_lastDownPos = Common::Point(x, y);
		}
	}
	sortElements();
	for (uint idx : _layout->order) {
		const ElementState &e = _layout->elements[idx];
		uint32 hotspot;
		if (!hitTest(e, x, y, hotspot))
			continue;
		const PathSegment *seg = e.segment();
		uint32 segId = seg ? seg->id : 0;
		bool matched = false;
		for (const Trigger &t : e.def->triggers) {
			if (t.kind != 0 || t.event != msg)
				continue;
			if (t.segmentId && t.segmentId != segId)
				continue;
			if (t.hotspotId && t.hotspotId != hotspot)
				continue;
			if (!matched && msg != kEventMouseMove)
				debugC(1, kDebugInput, "%s at %d,%d: %s (segment %x, hotspot %x)", getEventName(msg, 0), x, y,
					e.def->name.c_str(), segId, hotspot);
			matched = true;
			queueList(idx, t.list);
		}
		if (matched)
			return;
	}
	if (msg != kEventMouseMove)
		debugC(2, kDebugInput, "%s at %d,%d: nothing", getEventName(msg, 0), x, y);
}

void Runtime::escape() {
	if (!_layout || _vars.find("GVarESC") < 0)
		return;
	Common::String expr = _vars.get("GVarESC");
	debugC(1, kDebugInput, "Escape: %s", expr.c_str());
	_eval->execute(expr);
}

bool Runtime::findElement(const Common::String &name, uint32 hotspotId, Common::Point &pt) const {
	int el = findElementState(name);
	if (el < 0)
		return false;
	const ElementState &e = _layout->elements[el];
	if (e.def->isVideo()) {
		pt = Common::Point(e.x + e.def->width / 2, e.y + e.def->height / 2);
		return true;
	}
	const PathSegment *seg = e.segment();
	if (!seg)
		return false;
	const Pib *pib = getFrame(e);
	Common::Point pos = getOrigin(e, pib);
	for (const Hotspot &h : seg->hotspots) {
		if (hotspotId && h.id != hotspotId)
			continue;
		// A point inside the polygon near its bounding box centre
		Common::Point c((h.bounds.left + h.bounds.right) / 2, (h.bounds.top + h.bounds.bottom) / 2);
		for (int dy = 0; dy < h.bounds.height() / 2; dy++) {
			for (int dx = 0; dx < h.bounds.width() / 2; dx++) {
				for (int s = 0; s < 4; s++) {
					int px = c.x + ((s & 1) ? -dx : dx), py = c.y + ((s & 2) ? -dy : dy);
					if (h.contains(px, py)) {
						pt = Common::Point(pos.x + px, pos.y + py);
						return true;
					}
				}
			}
		}
	}
	if (hotspotId)
		return false;
	if (!pib)
		return false;
	int cx = pib->surface.w / 2, cy = pib->surface.h / 2;
	for (int r = 0; r < MAX(pib->surface.w, pib->surface.h); r++) {
		for (int dy = -r; dy <= r; dy++) {
			for (int dx = -r; dx <= r; dx++) {
				if (pib->isOpaque(cx + dx, cy + dy)) {
					pt = Common::Point(pos.x + cx + dx, pos.y + cy + dy);
					return true;
				}
			}
		}
	}
	return false;
}

// Test aid: the visible elements that react to clicks, front to back
void Runtime::listClickable() const {
	debug("Clickable in %s:", getLayoutName().c_str());
	Common::Array<uint> order = _layout->order;
	for (uint idx : order) {
		const ElementState &e = _layout->elements[idx];
		if (!e.visible)
			continue;
		const PathSegment *seg = e.segment();
		uint32 segId = seg ? seg->id : 0;
		for (const Trigger &t : e.def->triggers) {
			if (t.kind != 0 || (t.event != kEventLButtonUp && t.event != kEventLButtonDown) || (t.segmentId && t.segmentId != segId))
				continue;
			Common::Point pt;
			bool at = findElement(e.def->name, t.hotspotId, pt);
			Common::String first = t.list.actions.empty() ? "-" : describeAction(t.list.actions[0], _layout->layout);
			debug("  %s %x%s: %s", e.def->name.c_str(), t.hotspotId, at ? Common::String::format(" at %d,%d", pt.x, pt.y).c_str() : " (no point)", first.c_str());
		}
	}
}

// ScriptHost

bool Runtime::hasElement(const Common::String &name) {
	return findElementState(name) >= 0;
}

int32 Runtime::getProperty(const Common::String &element, int prop) {
	int el = findElementState(element);
	if (el < 0)
		return 0;
	ElementState &e = _layout->elements[el];
	const PathSegment *seg = e.segment();
	switch (prop) {
	case kPropPibList:
		return seg ? seg->pibList : 0;
	case kPropSound:
		return seg ? seg->sound : 0;
	case kPropPathSeg:
		return MAX(e.seg, 0);
	case kPropShow:
		return e.visible;
	case kPropX:
		return e.def->isVideo() ? e.x : getPosition(e).x;
	case kPropY:
		return e.def->isVideo() ? e.y : getPosition(e).y;
	case kPropZ:
		return getZ(e);
	case kPropDuration:
		return seg ? seg->duration : 0;
	case kPropWidth:
	case kPropHeight: {
		if (e.def->isVideo())
			return prop == kPropWidth ? e.def->width : e.def->height;
		const Pib *pib = getFrame(e);
		return pib ? (prop == kPropWidth ? pib->surface.w : pib->surface.h) : 0;
	}
	default:
		return 0;
	}
}

void Runtime::setProperty(const Common::String &element, int prop, int32 value) {
	int el = findElementState(element);
	if (el < 0)
		return;
	ElementState &e = _layout->elements[el];
	PathSegment *seg = (e.seg >= 0 && e.seg < (int)e.def->segments.size()) ? &e.def->segments[e.seg] : nullptr;
	switch (prop) {
	case kPropPibList:
		if (seg)
			seg->pibList = value;
		break;
	case kPropSound:
		if (seg)
			seg->sound = value;
		break;
	case kPropPathSeg:
		setPathSegment(el, value);
		break;
	case kPropStart:
	case kPropPlay:
		if (e.def->isVideo()) {
			if (value) {
				debugC(1, kDebugActions, "Video %s (%s) skipped", e.def->name.c_str(), _media->getVideoPath(e.def->videoId).toString().c_str());
				fireEvent(el, kEventVideoPlay);
				fireEvent(el, kEventVideoStop);
			}
		} else if (value) {
			startSprite(el);
		} else {
			stopSprite(el);
		}
		break;
	case kPropShow:
		setVisible(el, value != 0);
		break;
	case kPropX:
	case kPropY:
		if (e.def->isVideo()) {
			(prop == kPropX ? e.x : e.y) = value;
		} else {
			Common::Point pos = getPosition(e);
			if (prop == kPropX)
				e.x += value - pos.x;
			else
				e.y += value - pos.y;
		}
		break;
	case kPropZ:
		e.z += value - getZ(e);
		break;
	case kPropDuration:
		if (seg)
			seg->duration = value;
		break;
	case kPropWidth:
		e.def->width = value;
		break;
	case kPropHeight:
		e.def->height = value;
		break;
	default:
		break;
	}
}

// Palette transitions (TREPAL): the description is { type, steps, duration in ms }

void Runtime::changePalette(uint32 id, const ActionParam *transition, bool block) {
	int type = 0;
	uint32 duration = 0;
	if (transition && transition->blob.size() >= 28) {
		type = READ_LE_UINT32(&transition->blob[16]);
		duration = READ_LE_UINT32(&transition->blob[24]);
	}
	if (id && !_media->loadPalette(id, _palette))
		warning("Palette %x is missing", id);
	if (type == kFadeToBlack) {
		_fadeFrom = _fadeLevel;
		_fadeTo = 0;
	} else if (type == kFadeFromBlack) {
		_fadeFrom = 0;
		_fadeTo = kFadeFull;
	} else {
		_fadeFrom = _fadeTo = kFadeFull;
		duration = 0;
	}
	_fadeLevel = _fadeFrom;
	_fadeStart = _now;
	_fadeDuration = duration;
	// The transition runs before the rest of the list
	if (block && !_leaving && duration)
		_blockedUntil = _now + duration;
	updatePalette(_now);
}

void Runtime::updatePalette(uint32 now) {
	if (_fadeLevel != _fadeTo) {
		uint32 t = now - _fadeStart;
		if (!_fadeDuration || t >= _fadeDuration)
			_fadeLevel = _fadeTo;
		else
			_fadeLevel = _fadeFrom + (_fadeTo - _fadeFrom) * (int)t / (int)_fadeDuration;
	}
	for (int i = 0; i < 256 * 3; i++)
		_screenPalette[i] = _palette[i] * _fadeLevel / kFadeFull;
}

// The frame

void Runtime::update(uint32 now) {
	if (!_lastUpdate)
		_lastUpdate = now;
	uint32 dt = MIN<uint32>(now - _lastUpdate, kMaxFrameTime);
	_lastUpdate = now;
	_now += dt;

	processQueue();
	if (_layout && _now >= _blockedUntil) {
		for (uint i = 0; i < _layout->elements.size(); i++) {
			if (_layout->elements[i].running)
				updateSprite(i, dt);
		}
		processQueue();
	}
	if (!_pendingLayout.empty() && _queue.empty())
		switchLayout();
	_sound->update();
	updatePalette(_now);
}

void Runtime::render(Graphics::Surface &dst) {
	dst.fillRect(Common::Rect(dst.w, dst.h), 0);
	if (!_layout)
		return;
	sortElements();
	for (int i = _layout->order.size() - 1; i >= 0; i--) {
		const ElementState &e = _layout->elements[_layout->order[i]];
		if (!e.visible || !e.def->isSprite())
			continue;
		const Pib *pib = getFrame(e);
		if (!pib)
			continue;
		Common::Point pos = getOrigin(e, pib);
		for (int y = 0; y < pib->surface.h; y++) {
			int dy = pos.y + y;
			if (dy < 0 || dy >= dst.h)
				continue;
			const byte *src = (const byte *)pib->surface.getBasePtr(0, y);
			byte *out = (byte *)dst.getBasePtr(0, dy);
			for (int x = 0; x < pib->surface.w; x++) {
				int dx = pos.x + x;
				if (dx >= 0 && dx < dst.w && pib->isOpaque(x, y))
					out[dx] = src[x];
			}
		}
	}
}

void Runtime::logStatistics() const {
	Common::String visited;
	for (const auto &it : _visited)
		visited += Common::String::format(" %s", it._key.c_str());
	debug("Layouts visited:%s", visited.c_str());
	if (_unimplemented.empty())
		debug("No unimplemented actions ran");
	for (const auto &it : _unimplemented)
		debug("Unimplemented: %s x%u", it._key.c_str(), it._value);
}

} // End of namespace MSPAM
