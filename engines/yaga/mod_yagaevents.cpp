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
#include "common/endian.h"
#include "common/ptr.h"
#include "common/events.h"
#include "common/system.h"
#include "common/tokenizer.h"

#include "yaga/detection.h"
#include "yaga/display.h"
#include "yaga/modules.h"
#include "yaga/pyhost.h"
#include "yaga/yaga.h"
#include "yaga/python/internal.h"

// yagaevents: the engine's main loop. boot.py ends in StartEventLoop, and
// from then on the game runs from timer ticks (game code, then drawing)
// and input events, delivered to the receivers the scripts register.

namespace Yaga {

using namespace Python;

enum {
	kClassTimer = 0,
	kClassRenderTarget = 1,
	kClassMouse = 2,
	kClassKeyboard = 3,
	kClassGamepad = 4,
	kClassScene = 5
};

// Event types are numbered apart across the enums: scene_helogo tests
// eventType against IEVENT_BUTTON_DOWN without checking the class
enum {
	kInputButtonDown = 0,
	kInputButtonUp = 1,
	kInputButtonPress = 2,
	kInputAxisX = 3,
	kInputAxisY = 4,
	kInputAxisZ = 5,
	kRenderTargetClose = 16,
	kRenderTargetActivated = 17,
	kRenderTargetModeToggle = 18,
	kTimerTick = 32
};

// The engine's codes for keys that are not characters; anything below
// KEY_CODES_BEGIN is a character
enum {
	kKeyEscape = 0x100,
	kKeyF1 = 0x101,
	kKeyBackspace = 0x10D,
	kKeyTab = 0x10E,
	kKeyEnter = 0x10F,
	kKeyShift = 0x110,
	kKeyControl = 0x111,
	kKeyAlt = 0x112,
	kKeyUp = 0x113,
	kKeyDown = 0x114,
	kKeyLeft = 0x115,
	kKeyRight = 0x116
};

static const ConstDef g_eventClasses[] = {
	{ "CLASS_TIMER", kClassTimer }, { "CLASS_RENDER_TARGET", kClassRenderTarget },
	{ "CLASS_MOUSE", kClassMouse }, { "CLASS_KEYBOARD", kClassKeyboard },
	{ "CLASS_GAMEPAD", kClassGamepad }, { "CLASS_SCENE", kClassScene },
	{ nullptr, 0 }
};

static const ConstDef g_inputEvents[] = {
	{ "IEVENT_BUTTON_DOWN", kInputButtonDown }, { "IEVENT_BUTTON_UP", kInputButtonUp },
	{ "IEVENT_BUTTON_PRESS", kInputButtonPress }, { "IEVENT_AXIS_POS_X", kInputAxisX },
	{ "IEVENT_AXIS_POS_Y", kInputAxisY }, { "IEVENT_AXIS_POS_Z", kInputAxisZ },
	{ nullptr, 0 }
};

static const ConstDef g_renderTargetEvents[] = {
	{ "IEVENT_RT_CLOSE", kRenderTargetClose }, { "IEVENT_RT_ACTIVATED", kRenderTargetActivated },
	{ "IEVENT_RT_MODE_TOGGLE", kRenderTargetModeToggle },
	{ nullptr, 0 }
};

static const ConstDef g_timerEvents[] = {
	{ "TIMER_TICK", kTimerTick },
	{ nullptr, 0 }
};

static const ConstDef g_receiverReturns[] = {
	{ "EVENT_NOT_HANDLED", 0 }, { "EVENT_HANDLED", 1 },
	{ nullptr, 0 }
};

static const ConstDef g_sourceFlags[] = {
	{ "IFLAGS_RAW_BUTTONS", 1 }, { "IFLAGS_TRANSLATE_BUTTONS", 2 },
	{ nullptr, 0 }
};

static const ConstDef g_keyCodes[] = {
	{ "KEY_CODES_BEGIN", kKeyEscape }, { "KEY_ESCAPE", kKeyEscape },
	{ "KEY_F1", kKeyF1 }, { "KEY_F2", kKeyF1 + 1 }, { "KEY_F3", kKeyF1 + 2 }, { "KEY_F4", kKeyF1 + 3 },
	{ "KEY_F5", kKeyF1 + 4 }, { "KEY_F6", kKeyF1 + 5 }, { "KEY_F7", kKeyF1 + 6 }, { "KEY_F8", kKeyF1 + 7 },
	{ "KEY_F9", kKeyF1 + 8 }, { "KEY_F10", kKeyF1 + 9 }, { "KEY_F11", kKeyF1 + 10 }, { "KEY_F12", kKeyF1 + 11 },
	{ "KEY_BACKSPACE", kKeyBackspace }, { "KEY_TAB", kKeyTab }, { "KEY_ENTER", kKeyEnter },
	{ "KEY_SHIFT", kKeyShift }, { "KEY_CONTROL", kKeyControl }, { "KEY_ALT", kKeyAlt },
	{ "KEY_UP", kKeyUp }, { "KEY_DOWN", kKeyDown }, { "KEY_LEFT", kKeyLeft }, { "KEY_RIGHT", kKeyRight },
	{ nullptr, 0 }
};

static Type g_eventType("yagaevents.Event", &g_objectType);
static Type g_receiverType("yagaevents.IEventReciever", &g_objectType);
static Type g_timerType("yagaevents.Timer", &g_objectType);
static Type g_sourceType("yagaevents.EventSource", &g_objectType);
static Type g_managerType("yagaevents.EventManager", &g_objectType);

// ---- Events --------------------------------------------------------------------------

static ObjRef newEvent(int32 eventClass, int32 eventType, Object *value, int32 elementID = 0, int32 deviceID = 0) {
	ObjRef e(new EngineObject(&g_eventType));
	setEngineAttr(e.get(), "eventClass", newInt(eventClass).get());
	setEngineAttr(e.get(), "eventType", newInt(eventType).get());
	setEngineAttr(e.get(), "value", value);
	setEngineAttr(e.get(), "elementID", newInt(elementID).get());
	setEngineAttr(e.get(), "deviceID", newInt(deviceID).get());
	return e;
}

static ObjRef eventNew(Type *, Tuple *args, Dict *) {
	if (!checkArgs("Event", args, 2, 5))
		return ObjRef();
	int32 v[5] = { 0, 0, 0, 0, 0 };
	for (uint32 i = 0; i < args->_size; i++)
		if (i != 2 && !asInt(args->get(i), &v[i]))
			return ObjRef();
	ObjRef value = args->_size > 2 ? ObjRef(args->get(2)) : ObjRef(newInt(0));
	return newEvent(v[0], v[1], value.get(), v[3], v[4]);
}

static ObjRef eventRepr(Object *o) {
	Common::String s = "<Event";
	static const char *const names[] = { "eventClass", "eventType", "value", "elementID" };
	for (uint i = 0; i < ARRAYSIZE(names); i++) {
		Object *v = o->getDict()->getItemString(names[i]);
		ObjRef r = v ? repr(v) : ObjRef();
		if (!r) {
			clearError();
			continue;
		}
		s += Common::String::format(" %s=%s", names[i], ((Str *)r.get())->_data);
	}
	return newStr(s + ">");
}

// ---- IEventReciever: the base of the game's receivers ---------------------------------

static ObjRef receiverInit(Object *, Tuple *, Dict *) {
	return ObjRef(g_none);
}

static ObjRef receiverRaise(Object *, Tuple *, Dict *) {
	return newInt(0);
}

static const MethodDef g_receiverMethods[] = {
	{ "__init__", receiverInit, kMethVarArgs | kMethKeywords, nullptr },
	{ "Raise", receiverRaise, kMethVarArgs, nullptr },
	{ nullptr, nullptr, 0, nullptr }
};

static ObjRef receiverNew(Type *type, Tuple *, Dict *) {
	return ObjRef(new HeapObject(type));
}

// ---- Timers ----------------------------------------------------------------------------

struct TimerObject : public EngineObject {
	uint32 _next, _last;
	explicit TimerObject(Type *t) : EngineObject(t), _next(0), _last(0) {
		setEngineAttr(this, "tickFrequency", newFloat(30.0).get());
		setEngineAttr(this, "eventReciever", g_none);
	}
};

static ObjRef timerTick(Object *, Tuple *, Dict *) {
	return ObjRef(g_none);
}

static const MethodDef g_timerMethods[] = {
	{ "Tick", timerTick, kMethVarArgs, nullptr },
	{ nullptr, nullptr, 0, nullptr }
};

// ---- The event manager -----------------------------------------------------------------

struct Receiver {
	ObjRef object;
	Common::Array<int32> classes; // empty: every class
};

struct EventManagerObject : public EngineObject {
	Common::Array<TimerObject *> _timers; // each holds a reference
	Common::Array<Receiver> _receivers;
	Ref<Dict> _sources;
	bool _running;
	explicit EventManagerObject(Type *t) : EngineObject(t), _running(false) {
		_sources = newDict();
		setEngineAttr(this, "maxLoopFrequency", newFloat(30.0).get());
	}
};

// The manager is a singleton: input_manager makes its own with
// yagaevents.EventManager() and registers on that
static EventManagerObject *g_manager = nullptr;

static ObjRef managerNew(Type *, Tuple *, Dict *) {
	if (!g_manager) {
		g_manager = new EventManagerObject(&g_managerType);
		incRef(g_manager);
	}
	return ObjRef(g_manager);
}

static ObjRef managerCreateTimer(Object *, Tuple *, Dict *) {
	return ObjRef(new TimerObject(&g_timerType));
}

static TimerObject *timerArg(const char *fname, Tuple *args) {
	if (!checkArgs(fname, args, 1, 1))
		return nullptr;
	if (!isInstanceOf(args->get(0), &g_timerType)) {
		setErrorFormat(g_excTypeError, "%s() expects a Timer, got %.100s", fname, typeName(args->get(0)));
		return nullptr;
	}
	return (TimerObject *)args->get(0);
}

static ObjRef managerInstallTimer(Object *self, Tuple *args, Dict *) {
	EventManagerObject *em = (EventManagerObject *)self;
	TimerObject *t = timerArg("InstallTimer", args);
	if (!t)
		return ObjRef();
	for (uint i = 0; i < em->_timers.size(); i++)
		if (em->_timers[i] == t)
			return ObjRef(g_none);
	incRef(t);
	t->_next = t->_last = g_system->getMillis();
	em->_timers.push_back(t);
	return ObjRef(g_none);
}

static ObjRef managerRemoveTimer(Object *self, Tuple *args, Dict *) {
	EventManagerObject *em = (EventManagerObject *)self;
	TimerObject *t = timerArg("RemoveTimer", args);
	if (!t)
		return ObjRef();
	for (uint i = 0; i < em->_timers.size(); i++) {
		if (em->_timers[i] == t) {
			em->_timers.remove_at(i);
			decRef(t);
			break;
		}
	}
	return ObjRef(g_none);
}

// RegisterEventReciever(classes, receiver): classes is a UInt32Seq
static ObjRef managerRegister(Object *self, Tuple *args, Dict *) {
	EventManagerObject *em = (EventManagerObject *)self;
	if (!checkArgs("RegisterEventReciever", args, 2, 2))
		return ObjRef();
	Receiver r;
	r.object = args->get(1);
	ObjRef it = getIter(args->get(0));
	if (it) {
		for (;;) {
			ObjRef item = iterNext(it.get());
			if (!item)
				break;
			int32 c;
			if (asInt(item.get(), &c))
				r.classes.push_back(c);
			else
				clearError();
		}
	}
	if (errorOccurred())
		clearError();
	for (uint i = 0; i < em->_receivers.size(); i++) {
		if (em->_receivers[i].object == r.object) {
			em->_receivers[i] = r;
			return ObjRef(g_none);
		}
	}
	em->_receivers.push_back(r);
	debugC(2, kDebugScript, "yagaevents: receiver %s registered for %u classes", typeName(r.object.get()), r.classes.size());
	return ObjRef(g_none);
}

static ObjRef managerUnregister(Object *self, Tuple *args, Dict *) {
	EventManagerObject *em = (EventManagerObject *)self;
	if (!checkArgs("UnregisterEventReciever", args, 1, 2))
		return ObjRef();
	for (uint i = 0; i < em->_receivers.size(); i++) {
		if (em->_receivers[i].object.get() == args->get(0)) {
			em->_receivers.remove_at(i);
			break;
		}
	}
	return ObjRef(g_none);
}

static ObjRef newSource(int32 eventClass, int32 index) {
	ObjRef s(new EngineObject(&g_sourceType));
	setEngineAttr(s.get(), "eventClass", newInt(eventClass).get());
	setEngineAttr(s.get(), "index", newInt(index).get());
	setEngineAttr(s.get(), "handled", newInt(0).get());
	setEngineAttr(s.get(), "flags", newInt(0).get());
	return s;
}

// One mouse and one keyboard, and no gamepads
static ObjRef managerGetSource(Object *self, Tuple *args, Dict *) {
	EventManagerObject *em = (EventManagerObject *)self;
	if (!checkArgs("GetEventSource", args, 1, 2))
		return ObjRef();
	int32 eventClass, index = 0;
	if (!asInt(args->get(0), &eventClass) || (args->_size > 1 && !asInt(args->get(1), &index)))
		return ObjRef();
	if (index != 0 || eventClass == kClassGamepad)
		return ObjRef(g_none);
	Ref<Int> key = newInt(eventClass);
	Object *s = em->_sources->getItem(key.get());
	if (s)
		return ObjRef(s);
	ObjRef created = newSource(eventClass, index);
	em->_sources->setItem(key.get(), created.get());
	return created;
}

// IEventSource(renderTarget): the window's own events
static ObjRef moduleEventSource(Object *, Tuple *args, Dict *) {
	if (!checkArgs("IEventSource", args, 0, 1))
		return ObjRef();
	return newSource(kClassRenderTarget, 0);
}

static ObjRef managerNoop(Object *, Tuple *, Dict *) {
	return ObjRef(g_none);
}

// Hands an event to every receiver registered for its class. A receiver
// that raises is reported and skipped, as a C++ engine would carry on
static void dispatch(EventManagerObject *em, int32 eventClass, int32 eventType, int32 value, int32 elementID = 0, int32 deviceID = 0) {
	ObjRef event = newEvent(eventClass, eventType, newInt(value).get(), elementID, deviceID);
	Common::Array<Receiver> receivers = em->_receivers;
	for (uint i = 0; i < receivers.size(); i++) {
		const Receiver &r = receivers[i];
		bool wanted = r.classes.empty();
		for (uint j = 0; j < r.classes.size() && !wanted; j++)
			wanted = r.classes[j] == eventClass;
		if (!wanted)
			continue;
		ObjRef result = callMethod(r.object.get(), "Raise", 1, event.get());
		if (!result) {
			warning("yaga: input receiver raised: %s", formatError().c_str());
			clearError();
		}
	}
}

static void dispatchMouseAt(EventManagerObject *em, int x, int y) {
	dispatch(em, kClassMouse, kInputAxisX, x);
	dispatch(em, kClassMouse, kInputAxisY, y);
}

// What a raw key event carries: a Windows virtual-key code, so letters are upper case
static int32 rawKeyCode(const Common::KeyState &k) {
	switch (k.keycode) {
	case Common::KEYCODE_ESCAPE: return kKeyEscape;
	case Common::KEYCODE_BACKSPACE: return kKeyBackspace;
	case Common::KEYCODE_TAB: return kKeyTab;
	case Common::KEYCODE_RETURN:
	case Common::KEYCODE_KP_ENTER: return kKeyEnter;
	case Common::KEYCODE_LSHIFT:
	case Common::KEYCODE_RSHIFT: return kKeyShift;
	case Common::KEYCODE_LCTRL:
	case Common::KEYCODE_RCTRL: return kKeyControl;
	case Common::KEYCODE_LALT:
	case Common::KEYCODE_RALT: return kKeyAlt;
	case Common::KEYCODE_UP: return kKeyUp;
	case Common::KEYCODE_DOWN: return kKeyDown;
	case Common::KEYCODE_LEFT: return kKeyLeft;
	case Common::KEYCODE_RIGHT: return kKeyRight;
	case Common::KEYCODE_SEMICOLON: return 0xBA;
	case Common::KEYCODE_EQUALS: return 0xBB;
	case Common::KEYCODE_COMMA: return 0xBC;
	case Common::KEYCODE_MINUS: return 0xBD;
	case Common::KEYCODE_PERIOD: return 0xBE;
	case Common::KEYCODE_SLASH: return 0xBF;
	case Common::KEYCODE_BACKQUOTE: return 0xC0;
	case Common::KEYCODE_LEFTBRACKET: return 0xDB;
	case Common::KEYCODE_BACKSLASH: return 0xDC;
	case Common::KEYCODE_RIGHTBRACKET: return 0xDD;
	case Common::KEYCODE_QUOTE: return 0xDE;
	default:
		break;
	}
	if (k.keycode >= Common::KEYCODE_F1 && k.keycode <= Common::KEYCODE_F12)
		return kKeyF1 + (k.keycode - Common::KEYCODE_F1);
	if (k.keycode >= Common::KEYCODE_a && k.keycode <= Common::KEYCODE_z)
		return 'A' + (k.keycode - Common::KEYCODE_a);
	if (k.keycode == Common::KEYCODE_SPACE || (k.keycode >= Common::KEYCODE_0 && k.keycode <= Common::KEYCODE_9))
		return k.keycode;
	return -1;
}

// What the translated press carries: the character typed, or the code for
// Enter, Backspace, Tab and Escape (the name entry screen reads those)
static int32 typedKeyCode(const Common::KeyState &k) {
	int32 raw = rawKeyCode(k);
	if (raw == kKeyEnter || raw == kKeyBackspace || raw == kKeyTab || raw == kKeyEscape)
		return raw;
	if (k.ascii >= 32 && k.ascii < 127)
		return k.ascii;
	return -1;
}

// Returns whether the mouse moved
static bool pumpInput(EventManagerObject *em) {
	Common::Event e;
	bool moved = false;
	while (g_system->getEventManager()->pollEvent(e)) {
		switch (e.type) {
		case Common::EVENT_MOUSEMOVE:
			dispatchMouseAt(em, e.mouse.x, e.mouse.y);
			moved = true;
			break;
		case Common::EVENT_LBUTTONDOWN:
		case Common::EVENT_LBUTTONUP:
		case Common::EVENT_RBUTTONDOWN:
		case Common::EVENT_RBUTTONUP: {
			bool down = e.type == Common::EVENT_LBUTTONDOWN || e.type == Common::EVENT_RBUTTONDOWN;
			bool right = e.type == Common::EVENT_RBUTTONDOWN || e.type == Common::EVENT_RBUTTONUP;
			if (down)
				skipVideos();
			dispatch(em, kClassMouse, down ? kInputButtonDown : kInputButtonUp, down ? 1 : 0, right ? 1 : 0);
			break;
		}
		case Common::EVENT_KEYDOWN: {
			int32 raw = rawKeyCode(e.kbd);
			if (!e.kbdRepeat)
				skipVideos();
			if (raw >= 0 && !e.kbdRepeat)
				dispatch(em, kClassKeyboard, kInputButtonDown, 1, raw);
			int32 typed = typedKeyCode(e.kbd);
			if (typed >= 0)
				dispatch(em, kClassKeyboard, kInputButtonPress, 1, typed);
			break;
		}
		case Common::EVENT_KEYUP: {
			int32 raw = rawKeyCode(e.kbd);
			if (raw >= 0)
				dispatch(em, kClassKeyboard, kInputButtonUp, 1, raw);
			break;
		}
		default:
			break;
		}
	}
	return moved;
}

// Test hooks, so a run can be checked without anyone at the controls:
// yaga_ticks=N stops after N game ticks, yaga_shots=a,b,... saves the screen
// after those ticks into yaga_shot_dir, yaga_clicks=tick:x:y,... clicks, and
// yaga_probe=<python> runs a line of Python after the yaga_probe_ticks ticks
struct TestHooks {
	uint32 stopAfter;
	Common::Array<uint32> shots;
	Common::Array<uint32> probes;
	Common::String probeCode;
	Common::Array<uint32> clicks; // tick, x, y in threes
	Common::Path shotDir;

	TestHooks() : stopAfter(0) {
		if (ConfMan.hasKey("yaga_ticks"))
			stopAfter = ConfMan.getInt("yaga_ticks");
		Common::StringTokenizer shotList(ConfMan.get("yaga_shots"), ",");
		while (!shotList.empty())
			shots.push_back(atoi(shotList.nextToken().c_str()));
		Common::StringTokenizer clickList(ConfMan.get("yaga_clicks"), ",:");
		while (!clickList.empty())
			clicks.push_back(atoi(clickList.nextToken().c_str()));
		Common::StringTokenizer probeList(ConfMan.get("yaga_probe_ticks"), ",");
		while (!probeList.empty())
			probes.push_back(atoi(probeList.nextToken().c_str()));
		probeCode = ConfMan.get("yaga_probe");
		if (ConfMan.hasKey("yaga_shot_dir"))
			shotDir = ConfMan.getPath("yaga_shot_dir");
	}

	void beforeTick(EventManagerObject *em, uint32 tick) {
		for (uint i = 0; i + 2 < clicks.size(); i += 3) {
			if (clicks[i] != tick)
				continue;
			debug(1, "yaga: test click at (%u, %u), tick %u", clicks[i + 1], clicks[i + 2], tick);
			dispatchMouseAt(em, clicks[i + 1], clicks[i + 2]);
			skipVideos();
			dispatch(em, kClassMouse, kInputButtonDown, 1);
			dispatch(em, kClassMouse, kInputButtonUp, 0);
		}
	}

	void afterTick(uint32 tick) {
		for (uint i = 0; i < probes.size(); i++) {
			if (probes[i] != tick || probeCode.empty())
				continue;
			debug(1, "yaga: probe at tick %u", tick);
			if (!Interpreter::instance()->runString(probeCode.c_str(), "<probe>"))
				warning("yaga: probe failed: %s", Interpreter::instance()->lastError().c_str());
		}
		for (uint i = 0; i < shots.size(); i++) {
			if (shots[i] != tick || shotDir.empty())
				continue;
			Common::Path path = shotDir.appendComponent(Common::String::format("%s-%04u.png", g_engine->getGameId().c_str(), tick));
			if (g_display && g_display->saveScreenshot(path))
				debug(1, "yaga: saved %s", path.toString(Common::Path::kNativeSeparator).c_str());
			else
				warning("yaga: could not save a screenshot at tick %u", tick);
		}
	}
};

static double attrDouble(Object *o, const char *name, double fallback) {
	Object *v = o->getDict()->getItemString(name);
	double d;
	if (!v || !asDouble(v, &d)) {
		clearError();
		return fallback;
	}
	return d;
}

// ---- Event streams --------------------------------------------------------------------

// A compiled event stream (.evb; the game asks for .evt). Little-endian:
// size, count, then per event: time (float), type, parameter, 1.0f, an
// uninitialised word, 0, group count, and the groups, each a name and
// key/value fields with NUL-terminated strings
enum {
	kEventLipsync = 31415,  // parameter: the mouth shape (phoneme mask)
	kEventAnimation = 15500 // groups: AnimName, SoundName and Channel
};

// Every mouth shape some talking animation draws. Anything else (silence
// is 0) matches no head layer either, so it is drawn as the rest pose
static const uint32 kMouthShapesDrawn = 0x7bf;

struct StreamField {
	Common::String key, value;
};

struct StreamGroup {
	Common::String name;
	Common::Array<StreamField> fields;
};

struct StreamEvent {
	float time;
	uint32 type, param;
	Common::Array<StreamGroup> groups;
};

struct StreamData {
	Common::Array<StreamEvent> events;
};

static bool readCString(const byte *data, uint32 size, uint32 &pos, uint32 len, Common::String &out) {
	if (pos + len + 1 > size)
		return false;
	out = Common::String((const char *)data + pos, len);
	pos += len + 1;
	return true;
}

// Strict: every shipped stream fills its stated size exactly
static StreamData *parseStream(const byte *data, uint32 size) {
	if (size < 8 || READ_LE_UINT32(data) != size)
		return nullptr;
	uint32 count = READ_LE_UINT32(data + 4);
	uint32 pos = 8;
	Common::ScopedPtr<StreamData> s(new StreamData());
	for (uint32 i = 0; i < count; i++) {
		if (pos + 28 > size)
			return nullptr;
		StreamEvent e;
		e.time = READ_LE_FLOAT32(data + pos);
		e.type = READ_LE_UINT32(data + pos + 4);
		e.param = READ_LE_UINT32(data + pos + 8);
		uint32 groups = READ_LE_UINT32(data + pos + 24);
		pos += 28;
		for (uint32 g = 0; g < groups; g++) {
			if (pos + 8 > size)
				return nullptr;
			StreamGroup group;
			uint32 nameLen = READ_LE_UINT32(data + pos), fields = READ_LE_UINT32(data + pos + 4);
			pos += 8;
			if (!readCString(data, size, pos, nameLen, group.name))
				return nullptr;
			for (uint32 f = 0; f < fields; f++) {
				if (pos + 8 > size)
					return nullptr;
				StreamField field;
				uint32 keyLen = READ_LE_UINT32(data + pos), valueLen = READ_LE_UINT32(data + pos + 4);
				pos += 8;
				if (!readCString(data, size, pos, keyLen, field.key) || !readCString(data, size, pos, valueLen, field.value))
					return nullptr;
				group.fields.push_back(field);
			}
			e.groups.push_back(group);
		}
		s->events.push_back(e);
	}
	if (pos != size)
		return nullptr;
	return s.release();
}

static Type g_streamType("yagaevents.IEventStream", &g_objectType);
static Type g_playbackType("yagaevents.EventStreamPlayback", &g_objectType);
static Type g_elementType("yagaevents.Element", &g_objectType);
static Type g_attrType("yagaevents.Attribute", &g_objectType);

struct StreamObject : public EngineObject {
	Common::SharedPtr<StreamData> _data; // empty for a stream that cannot be read
	explicit StreamObject(Type *t) : EngineObject(t) {}
};

// IEventStream(resource)
static ObjRef streamNew(Type *t, Tuple *args, Dict *) {
	if (!checkArgs("IEventStream", args, 1, 1))
		return ObjRef();
	Str *data = resourceData(args->get(0));
	if (!data)
		return ObjRef();
	Ref<StreamObject> s(new StreamObject(t));
	StreamData *parsed = parseStream((const byte *)data->_data, data->_len);
	if (!parsed)
		debugC(1, kDebugScript, "yagaevents: %s is not an event stream", ((ResourceObject *)args->get(0))->_path.c_str());
	s->_data = Common::SharedPtr<StreamData>(parsed ? parsed : new StreamData());
	return ObjRef(s.get());
}

// EventData(i): the groups on event i, as character.CAnimReciever walks them
static ObjRef eventData(const StreamData *data, int32 index) {
	if (!data || index < 0 || index >= (int32)data->events.size())
		return ObjRef(g_none);
	const StreamEvent &e = data->events[index];
	Ref<List> groups = newList();
	for (uint i = 0; i < e.groups.size(); i++) {
		ObjRef element(new EngineObject(&g_elementType));
		setEngineAttr(element.get(), "name", newStr(e.groups[i].name).get());
		Ref<List> attrs = newList();
		for (uint j = 0; j < e.groups[i].fields.size(); j++) {
			ObjRef attr(new EngineObject(&g_attrType));
			setEngineAttr(attr.get(), "key", newStr(e.groups[i].fields[j].key).get());
			setEngineAttr(attr.get(), "name", newStr(e.groups[i].fields[j].key).get());
			setEngineAttr(attr.get(), "value", newStr(e.groups[i].fields[j].value).get());
			attrs->append(attr.get());
		}
		setEngineAttr(element.get(), "attrs", attrs.get());
		groups->append(element.get());
	}
	return ObjRef(groups.get());
}

static ObjRef streamEventData(Object *self, Tuple *args, Dict *) {
	int32 index;
	if (!checkArgs("EventData", args, 1, 1) || !asInt(args->get(0), &index))
		return ObjRef();
	return eventData(((StreamObject *)self)->_data.get(), index);
}

static int32 streamLength(Object *o) {
	return ((StreamObject *)o)->_data->events.size();
}

static const MethodDef g_streamMethods[] = {
	{ "EventData", streamEventData, kMethVarArgs, nullptr },
	{ nullptr, nullptr, 0, nullptr }
};

static SequenceMethods g_streamAsSequence;

// A stream being played: timed off the clock, like the sound it goes with,
// so a slow frame cannot walk a mouth out of step with the voice
struct PlaybackObject;
static Common::Array<PlaybackObject *> g_playbacks;
static int32 g_nextPlaybackId = 1;

struct PlaybackObject : public EngineObject {
	int32 _identity;
	bool _started;
	uint32 _startMillis;
	double _offset;
	uint32 _fired;

	explicit PlaybackObject(Type *t) : EngineObject(t), _identity(g_nextPlaybackId++), _started(false), _startMillis(0), _offset(0), _fired(0) {
		setEngineAttr(this, "stream", g_none);
		// CAnimReciever answers only events whose deviceID is this
		setEngineAttr(this, "identity", newInt(_identity).get());
		g_playbacks.push_back(this);
	}
	~PlaybackObject() override {
		for (uint i = 0; i < g_playbacks.size(); i++) {
			if (g_playbacks[i] == this) {
				g_playbacks.remove_at(i);
				break;
			}
		}
	}

	const StreamData *data() {
		Object *s = getDict()->getItemString("stream");
		return (s && isInstanceOf(s, &g_streamType)) ? ((StreamObject *)s)->_data.get() : nullptr;
	}

	double elapsed() const {
		return (g_system->getMillis() - _startMillis) / 1000.0 + _offset;
	}
};

static ObjRef managerCreatePlayback(Object *, Tuple *args, Dict *) {
	if (!checkArgs("CreateEventStreamPlayback", args, 0, 1))
		return ObjRef();
	ObjRef p(new PlaybackObject(&g_playbackType));
	setEngineAttr(p.get(), "name", args->_size ? args->get(0) : newStr("").get());
	return p;
}

static ObjRef playbackRun(Object *self, Tuple *, Dict *) {
	PlaybackObject *p = (PlaybackObject *)self;
	p->_started = true;
	p->_startMillis = g_system->getMillis();
	p->_fired = 0;
	return ObjRef(g_none);
}

static ObjRef playbackStop(Object *self, Tuple *, Dict *) {
	((PlaybackObject *)self)->_started = false;
	return ObjRef(g_none);
}

static ObjRef playbackSeek(Object *self, Tuple *args, Dict *) {
	double d;
	if (args->_size && asDouble(args->get(0), &d))
		((PlaybackObject *)self)->_offset = d;
	clearError();
	return ObjRef(g_none);
}

// EventData is asked of the playback, not the stream
static ObjRef playbackEventData(Object *self, Tuple *args, Dict *) {
	int32 index;
	if (!checkArgs("EventData", args, 1, 1) || !asInt(args->get(0), &index))
		return ObjRef();
	return eventData(((PlaybackObject *)self)->data(), index);
}

static const MethodDef g_playbackMethods[] = {
	{ "Run", playbackRun, kMethVarArgs, nullptr },
	{ "Stop", playbackStop, kMethVarArgs, nullptr },
	{ "Seek", playbackSeek, kMethVarArgs, nullptr },
	{ "EventData", playbackEventData, kMethVarArgs, nullptr },
	{ nullptr, nullptr, 0, nullptr }
};

// How long past its last event a stream keeps speaking: long enough that
// its final shape (usually the mouth closing) is always shown
static const double kStreamTail = 0.25;

bool playbackMouthShape(Object *o, uint32 *mask) {
	if (!isInstanceOf(o, &g_playbackType))
		return false;
	PlaybackObject *p = (PlaybackObject *)o;
	const StreamData *data = p->data();
	if (!p->_started || !data || data->events.empty())
		return false;
	double t = p->elapsed();
	if (t > data->events.back().time + kStreamTail)
		return false;
	bool found = false;
	for (uint i = 0; i < data->events.size() && data->events[i].time <= t; i++) {
		if (data->events[i].type != kEventLipsync)
			continue;
		uint32 m = data->events[i].param;
		*mask = (m & kMouthShapesDrawn) ? m : 1;
		found = true;
	}
	return found;
}

// Raises each stream event as it comes due, once and in order: receivers
// count them to find each event's data. They go to receivers registered
// for the event's type, carrying the playback's identity
static void pumpStreams(EventManagerObject *em) {
	Common::Array<ObjRef> live;
	for (uint i = 0; i < g_playbacks.size(); i++)
		live.push_back(ObjRef(g_playbacks[i]));
	for (uint i = 0; i < live.size(); i++) {
		PlaybackObject *p = (PlaybackObject *)live[i].get();
		const StreamData *data = p->data();
		if (!p->_started || !data)
			continue;
		double t = p->elapsed();
		while (p->_started && p->_fired < data->events.size() && data->events[p->_fired].time <= t) {
			const StreamEvent &e = data->events[p->_fired++];
			if (e.type != kEventLipsync)
				dispatch(em, e.type, e.type, e.param, 0, p->_identity);
		}
	}
}

// The loop: input first, then every timer that has come due, in the order
// installed (the game's code timer before its render timer)
static ObjRef managerStartLoop(Object *self, Tuple *, Dict *) {
	EventManagerObject *em = (EventManagerObject *)self;
	debug(1, "yaga: event loop started, %u timers", em->_timers.size());
	TestHooks hooks;
	uint32 ticks = 0;
	uint32 now = g_system->getMillis();
	for (uint i = 0; i < em->_timers.size(); i++)
		em->_timers[i]->_next = em->_timers[i]->_last = now;
	em->_running = true;

	while (em->_running && !g_engine->shouldQuit()) {
		// The pointer is the hardware cursor, which the backend only moves
		// when the screen is updated: do that as it moves, not only when the
		// game draws (10 to 15 times a second)
		if (pumpInput(em))
			g_system->updateScreen();
		pumpStreams(em);
		advanceSprites();
		now = g_system->getMillis();
		uint32 due = now + 50;
		bool ticked = false;
		Common::Array<TimerObject *> timers = em->_timers;
		for (uint i = 0; i < timers.size() && em->_running; i++) {
			Ref<TimerObject> t(timers[i]);
			double frequency = attrDouble(t.get(), "tickFrequency", 30.0);
			uint32 period = frequency > 0 ? (uint32)(1000.0 / frequency) : 1000;
			if (period == 0)
				period = 1;
			if ((int32)(now - t->_next) >= 0) {
				// The tick carries the seconds since the last one
				double delta = (now - t->_last) / 1000.0;
				t->_last = now;
				t->_next += period;
				if ((int32)(t->_next - now) <= 0)
					t->_next = now + period; // fell behind: drop the missed ticks
				if (i == 0) {
					hooks.beforeTick(em, ticks + 1);
					ticked = true;
				}
				Object *receiver = t->getDict()->getItemString("eventReciever");
				if (receiver && receiver != g_none) {
					ObjRef event = newEvent(kClassTimer, kTimerTick, newFloat(delta).get());
					if (!callMethod(receiver, "Raise", 1, event.get())) {
						em->_running = false;
						return ObjRef();
					}
				}
			}
			if ((int32)(t->_next - due) < 0)
				due = t->_next;
		}
		if (ticked) {
			ticks++;
			hooks.afterTick(ticks);
			if (hooks.stopAfter && ticks >= hooks.stopAfter) {
				debug(1, "yaga: stopping after %u ticks", ticks);
				break;
			}
		}
		uint32 after = g_system->getMillis();
		if ((int32)(due - after) > 0)
			g_system->delayMillis(MIN<uint32>(due - after, 10));
	}
	em->_running = false;
	debug(1, "yaga: event loop ended after %u ticks", ticks);
	return ObjRef(g_none);
}

static ObjRef managerStopLoop(Object *self, Tuple *, Dict *) {
	((EventManagerObject *)self)->_running = false;
	return ObjRef(g_none);
}

static const MethodDef g_managerMethods[] = {
	{ "CreateTimer", managerCreateTimer, kMethVarArgs, nullptr },
	{ "InstallTimer", managerInstallTimer, kMethVarArgs, nullptr },
	{ "RemoveTimer", managerRemoveTimer, kMethVarArgs, nullptr },
	{ "RegisterEventReciever", managerRegister, kMethVarArgs, nullptr },
	{ "UnregisterEventReciever", managerUnregister, kMethVarArgs, nullptr },
	{ "GetEventSource", managerGetSource, kMethVarArgs, nullptr },
	{ "SuspendLocalTime", managerNoop, kMethVarArgs, nullptr },
	{ "ResumeLocalTime", managerNoop, kMethVarArgs, nullptr },
	{ "StartEventLoop", managerStartLoop, kMethVarArgs, nullptr },
	{ "StopEventLoop", managerStopLoop, kMethVarArgs, nullptr },
	{ "CreateEventStreamPlayback", managerCreatePlayback, kMethVarArgs, nullptr },
	{ nullptr, nullptr, 0, nullptr }
};

static const MethodDef g_eventSourceDef = { "IEventSource", moduleEventSource, kMethVarArgs, nullptr };

static void fillYagaevents(Module *m) {
	if (!(g_eventType._flags & kTypeReady)) {
		g_eventType.newObj = eventNew;
		g_eventType.repr = eventRepr;
		readyEngineType(&g_eventType, nullptr, nullptr, true);
		g_receiverType._flags |= kTypeBaseType | kTypeHasDict;
		g_receiverType.methods = g_receiverMethods;
		g_receiverType.newObj = receiverNew;
		g_receiverType.getAttr = genericGetAttr;
		g_receiverType.setAttr = genericSetAttr;
		typeReady(&g_receiverType);
		readyEngineType(&g_timerType, g_timerMethods, nullptr, true);
		readyEngineType(&g_sourceType, nullptr, nullptr, true);
		g_managerType.newObj = managerNew;
		readyEngineType(&g_managerType, g_managerMethods, nullptr, true);
		g_streamType.newObj = streamNew;
		g_streamAsSequence.length = streamLength;
		g_streamType.asSequence = &g_streamAsSequence;
		readyEngineType(&g_streamType, g_streamMethods, nullptr, true);
		readyEngineType(&g_playbackType, g_playbackMethods, nullptr, true);
		readyEngineType(&g_elementType, nullptr, nullptr, false);
		readyEngineType(&g_attrType, nullptr, nullptr, false);
	}
	addConstants(m, "EEventClass", g_eventClasses);
	addConstants(m, "EInputEvent", g_inputEvents);
	addConstants(m, "ERenderTargetEvent", g_renderTargetEvents);
	addConstants(m, "ETimerEvent", g_timerEvents);
	addConstants(m, "ERecieverReturn", g_receiverReturns);
	addConstants(m, "EventSourceFlags", g_sourceFlags);
	addConstants(m, "KeyCodes", g_keyCodes);
	moduleAddObject(m, "Event", &g_eventType);
	moduleAddObject(m, "IEventReciever", &g_receiverType);
	moduleAddObject(m, "Timer", &g_timerType);
	moduleAddObject(m, "EventSource", &g_sourceType);
	moduleAddObject(m, "EventManager", &g_managerType);
	moduleAddObject(m, "IEventStream", &g_streamType);
	moduleAddObject(m, "EventStreamPlayback", &g_playbackType);
	ObjRef f(new BuiltinFunction(&g_eventSourceDef, nullptr));
	moduleAddObject(m, "IEventSource", f.get());
}

void registerYagaevents() {
	g_manager = nullptr; // a fresh one for each run
	registerEngineModule("yagaevents", fillYagaevents);
}

} // End of namespace Yaga
