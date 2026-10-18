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
#include "common/events.h"
#include "common/file.h"
#include "common/fs.h"
#include "common/str-array.h"
#include "common/tokenizer.h"
#include "common/system.h"
#include "common/textconsole.h"
#include "engines/util.h"
#include "graphics/cursorman.h"
#include "graphics/paletteman.h"
#include "graphics/wincursor.h"
#include "image/png.h"

#include "mspam/busgame.h"
#include "mspam/detection.h"
#include "mspam/layout.h"
#include "mspam/media.h"
#include "mspam/mspam.h"
#include "mspam/runtime.h"

namespace MSPAM {

enum {
	kColorHotspot = 249,  // static red
	kColorVideo = 251,    // static yellow
	kColorHidden = 253    // static magenta
};

MSPAMEngine::MSPAMEngine(OSystem *syst, const ADGameDescription *gameDesc) : Engine(syst),
	_gameDescription(gameDesc), _media(nullptr), _title(nullptr), _layout(nullptr), _layoutIndex(-1),
	_showHidden(false), _showHotspots(false) {
	memset(_palette, 0, sizeof(_palette));
	MediaManager::setStaticColors(_palette);
}

MSPAMEngine::~MSPAMEngine() {
	delete _layout;
	delete _title;
	delete _media;
	_screen.free();
}

bool MSPAMEngine::hasFeature(EngineFeature f) const {
	return f == kSupportsReturnToLauncher;
}

// MSBSOLAR.INI's [Media] section: msbsolar.mdf/cmf (the CMF stays on the disc) and
// msbamb.mdf/cmf (installed); the AVIs live in the 0\ and 1\ folders
bool MSPAMEngine::loadTitle() {
	const Common::FSNode gameDir(ConfMan.getPath("path"));
	SearchMan.addSubDirectoryMatching(gameDir, "msbsolar", 0, 2);

	_media = new MediaManager();
	if (!_media->addIndex(Common::Path("MSBSOLAR.MDF"), Common::Path("MSBSOLAR.CMF"))) {
		GUIErrorMessage("Can't open MSBSOLAR.MDF/CMF");
		return false;
	}
	if (!_media->addIndex(Common::Path("MSBAMB.MDF"), Common::Path("MSBAMB.CMF")))
		warning("Can't open MSBAMB.MDF/CMF, the ambient sound will be missing");

	Common::File *tdf = new Common::File();
	if (!tdf->open(Common::Path("MSBSOLAR.TDF")) || !_tdf.open(tdf)) {
		GUIErrorMessage("Can't read MSBSOLAR.TDF");
		return false;
	}
	_title = new Title();
	if (!_title->load(_tdf)) {
		GUIErrorMessage("MSBSOLAR.TDF has no title view");
		return false;
	}
	return true;
}

bool MSPAMEngine::showLayout(int index) {
	int count = _title->_layouts.size();
	index = (index % count + count) % count;
	Layout *layout = _title->loadLayout(_tdf, index);
	if (!layout)
		return false;
	delete _layout;
	_layout = layout;
	_layoutIndex = index;
	_media->clearCache();
	if (_layout->_paletteId && !_media->loadPalette(_layout->_paletteId, _palette))
		warning("Layout %s: palette %x is missing", _layout->_name.c_str(), _layout->_paletteId);
	redraw();
	return true;
}

// Elements are drawn back to front (descending z) at their site position. Until
// the action runtime exists, sprites show the first frame of their initial path segment.
void MSPAMEngine::renderLayout(const Layout &layout, Graphics::Surface &dst, bool showHidden, bool showHotspots) {
	dst.fillRect(Common::Rect(dst.w, dst.h), 0);
	for (int i = layout._siteOrder.size() - 1; i >= 0; i--) {
		const Element &e = layout._elements[layout._siteOrder[i]];
		if (!e.visible && !showHidden)
			continue;
		if (!e.isSprite())
			continue;
		const PathSegment *seg = e.getCurrentSegment();
		const PibList *list = seg ? _media->getPibList(seg->pibList) : nullptr;
		const Pib *pib = list ? list->getFrame(0) : nullptr;
		if (!pib)
			continue;
		for (int y = 0; y < pib->surface.h; y++) {
			int dy = e.y + y;
			if (dy < 0 || dy >= dst.h)
				continue;
			const byte *src = (const byte *)pib->surface.getBasePtr(0, y);
			byte *out = (byte *)dst.getBasePtr(0, dy);
			for (int x = 0; x < pib->surface.w; x++) {
				int dx = e.x + x;
				if (dx >= 0 && dx < dst.w && pib->isOpaque(x, y))
					out[dx] = src[x];
			}
		}
	}

	if (!showHotspots)
		return;
	for (uint idx : layout._siteOrder) {
		const Element &e = layout._elements[idx];
		if (!e.visible && !showHidden)
			continue;
		if (e.isVideo()) {
			Common::Rect r(e.x, e.y, e.x + e.width, e.y + e.height);
			r.clip(Common::Rect(dst.w, dst.h));
			if (!r.isEmpty())
				dst.frameRect(r, kColorVideo);
			continue;
		}
		const PathSegment *seg = e.getCurrentSegment();
		if (!seg)
			continue;
		for (const Hotspot &h : seg->hotspots) {
			for (uint k = 0; k < h.points.size(); k++) {
				const Common::Point &a = h.points[k], &b = h.points[(k + 1) % h.points.size()];
				int x0 = CLIP<int>(e.x + a.x, 0, dst.w - 1), y0 = CLIP<int>(e.y + a.y, 0, dst.h - 1);
				int x1 = CLIP<int>(e.x + b.x, 0, dst.w - 1), y1 = CLIP<int>(e.y + b.y, 0, dst.h - 1);
				dst.drawLine(x0, y0, x1, y1, e.visible ? kColorHotspot : kColorHidden);
			}
		}
	}
}

void MSPAMEngine::redraw() {
	if (!_layout)
		return;
	renderLayout(*_layout, _screen, _showHidden, _showHotspots);
	g_system->getPaletteManager()->setPalette(_palette, 0, 256);
	g_system->copyRectToScreen(_screen.getPixels(), _screen.pitch, 0, 0, kScreenWidth, kScreenHeight);
	g_system->updateScreen();
}

// Front to back: a visible sprite is hit on an opaque pixel or inside one of its
// current segment's hotspots, a visible video inside its rectangle. Mouse triggers
// match when their path segment and hotspot filters are 0 or equal the hit ones.
void MSPAMEngine::hitTest(int x, int y) {
	if (!_layout)
		return;
	debugC(1, kDebugInput, "Click at %d,%d in %s", x, y, _layout->_name.c_str());
	bool dispatched = false;
	for (uint idx : _layout->_siteOrder) {
		const Element &e = _layout->_elements[idx];
		if (!e.visible)
			continue;
		int lx = x - e.x, ly = y - e.y;
		const PathSegment *seg = nullptr;
		const Hotspot *hot = nullptr;
		bool hit = false;
		if (e.isVideo()) {
			hit = lx >= 0 && ly >= 0 && lx < (int)e.width && ly < (int)e.height;
		} else {
			seg = e.getCurrentSegment();
			if (!seg)
				continue;
			for (const Hotspot &h : seg->hotspots) {
				if (h.contains(lx, ly)) {
					hot = &h;
					break;
				}
			}
			const PibList *list = _media->getPibList(seg->pibList);
			const Pib *pib = list ? list->getFrame(0) : nullptr;
			hit = hot || (pib && pib->isOpaque(lx, ly));
		}
		if (!hit)
			continue;

		Common::String where = Common::String::format("%s (z %d", e.name.c_str(), e.z);
		if (seg)
			where += Common::String::format(", segment %x", seg->id);
		if (hot)
			where += Common::String::format(", hotspot %x", hot->id);
		where += ")";
		int matches = 0;
		for (const Trigger &t : e.triggers) {
			if (t.kind != 0 || (t.event != kEventLButtonDown && t.event != kEventLButtonUp))
				continue;
			if (t.segmentId && (!seg || t.segmentId != seg->id))
				continue;
			if (t.hotspotId && (!hot || t.hotspotId != hot->id))
				continue;
			if (!matches++)
				debugC(1, kDebugInput, "  %s%s", dispatched ? "(covered) " : "", where.c_str());
			debugC(1, kDebugInput, "    %s -> list of %d actions:", getEventName(t.event, t.kind), t.list.actions.size());
			for (const Action &a : t.list.actions)
				debugC(1, kDebugInput, "      %s", describeAction(a, _layout).c_str());
		}
		if (!matches)
			debugC(1, kDebugInput, "  %s%s: no mouse trigger", dispatched ? "(covered) " : "", where.c_str());
		else
			dispatched = true;
	}
	if (!dispatched)
		debugC(1, kDebugInput, "  nothing would trigger");
}

bool MSPAMEngine::saveShot(const Common::String &path) {
	Common::DumpFile out;
	if (!out.open(Common::Path(path, '/'), true))
		return false;
	return Image::writePNG(out, _screen, _palette);
}

// Test hook: every layout as <dir>/<NN>_<name>.png
void MSPAMEngine::saveAllShots(const Common::String &dir) {
	for (uint i = 0; i < _title->_layouts.size() && !shouldQuit(); i++) {
		if (!showLayout(i)) {
			warning("Layout %s doesn't load", _title->_layouts[i].c_str());
			continue;
		}
		saveShot(Common::String::format("%s/%02d_%s.png", dir.c_str(), i, _title->_layouts[i].c_str()));
	}
}

// Test hook: a text dump of every layout's model
void MSPAMEngine::dumpLayouts(const Common::String &path) {
	Common::DumpFile out;
	if (!out.open(Common::Path(path, '/'), true))
		return;
	out.writeString(Common::String::format("View %s %dx%d, %d layouts\n", _title->_viewName.c_str(),
		_title->_width, _title->_height, _title->_layouts.size()));
	int failures = 0;
	for (uint i = 0; i < _title->_layouts.size(); i++) {
		Common::ScopedPtr<Layout> l(_title->loadLayout(_tdf, i));
		if (!l) {
			out.writeString(Common::String::format("\n[%d] %s FAILED\n", i, _title->_layouts[i].c_str()));
			failures++;
			continue;
		}
		out.writeString(Common::String::format("\n[%d] %s: palette %x, flags %x, priority %x, transition %x (%d %d %d), %d elements\n",
			i, l->_name.c_str(), l->_paletteId, l->_flags, l->_priority, l->_transClass,
			l->_transParams[0], l->_transParams[1], l->_transParams[2], l->_elements.size()));
		for (uint n = 0; n < l->_lists.size(); n++) {
			const ActionList &al = l->_lists[n];
			out.writeString(Common::String::format("  layout list %d (%s, flags %x/%x):\n", n,
				n == 0 ? "enter" : n == 1 ? "leave" : "?", al.flags, al.flags2));
			for (const Action &a : al.actions)
				out.writeString("    " + describeAction(a, l.get()) + "\n");
		}
		for (uint idx : l->_siteOrder) {
			const Element &e = l->_elements[idx];
			if (e.isVideo()) {
				out.writeString(Common::String::format("  #%d %s: video %dx%d at %d,%d z %d%s, AVI %x (%s), %d %d flags %x\n",
					idx, e.name.c_str(), e.width, e.height, e.x, e.y, e.z, e.visible ? "" : " hidden", e.videoId,
					_media->getVideoPath(e.videoId).toString().c_str(), e.videoParam, e.videoParam2, e.videoFlags));
			} else {
				out.writeString(Common::String::format("  #%d %s: sprite at %d,%d z %d%s, speed %d, iterations %d, segment %x, flags %x\n",
					idx, e.name.c_str(), e.x, e.y, e.z, e.visible ? "" : " hidden", e.speed, e.iterations,
					e.initialSegment, e.spriteFlags));
				for (const PathSegment &s : e.segments) {
					out.writeString(Common::String::format("    segment %x: pibs %x, sound %x/%d, to %d,%d,%d, flags %x, %d ms, %d frames, cursor %d",
						s.id, s.pibList, s.sound, s.soundParam, s.x, s.y, s.z, s.flags, s.duration, s.frames, s.cursor));
					if (s.hasTransition)
						out.writeString(Common::String::format(", transition %x %d %d", s.transClass, s.transDuration, s.transParam));
					out.writeString("\n");
					for (const Hotspot &h : s.hotspots)
						out.writeString(Common::String::format("      hotspot %x: (%d,%d)-(%d,%d), %d points, cursor %d\n",
							h.id, h.bounds.left, h.bounds.top, h.bounds.right, h.bounds.bottom, h.points.size(), h.cursor));
				}
			}
			for (const Trigger &t : e.triggers) {
				out.writeString(Common::String::format("    on %s", getEventName(t.event, t.kind)));
				if (t.segmentId)
					out.writeString(Common::String::format(" segment %x", t.segmentId));
				if (t.hotspotId)
					out.writeString(Common::String::format(" hotspot %x", t.hotspotId));
				out.writeString(Common::String::format(" (flags %x/%x):\n", t.list.flags, t.list.flags2));
				for (const Action &a : t.list.actions)
					out.writeString("      " + describeAction(a, l.get()) + "\n");
			}
		}
	}
	debug("Layout dump written to %s, %d failures", path.c_str(), failures);
}

void MSPAMEngine::viewer() {
	// PgUp/PgDn (or P/N) change layout, Home goes to the classroom, H toggles the
	// hotspot outlines, A shows hidden elements; clicks log what they would trigger
	while (!shouldQuit()) {
		Common::Event event;
		bool dirty = false;
		while (g_system->getEventManager()->pollEvent(event)) {
			if (event.type == Common::EVENT_LBUTTONDOWN) {
				hitTest(event.mouse.x, event.mouse.y);
				continue;
			}
			if (event.type != Common::EVENT_KEYDOWN)
				continue;
			switch (event.kbd.keycode) {
			case Common::KEYCODE_PAGEUP:
			case Common::KEYCODE_p:
				showLayout(_layoutIndex - 1);
				debug("Layout %d: %s", _layoutIndex, _layout->_name.c_str());
				break;
			case Common::KEYCODE_PAGEDOWN:
			case Common::KEYCODE_n:
				showLayout(_layoutIndex + 1);
				debug("Layout %d: %s", _layoutIndex, _layout->_name.c_str());
				break;
			case Common::KEYCODE_HOME:
				showLayout(MAX(0, _title->findLayout("Classroom")));
				break;
			case Common::KEYCODE_h:
				_showHotspots = !_showHotspots;
				dirty = true;
				break;
			case Common::KEYCODE_a:
				_showHidden = !_showHidden;
				dirty = true;
				break;
			default:
				break;
			}
		}
		if (dirty)
			redraw();
		g_system->delayMillis(10);
	}
}

// Development aid: when "mspam_test_input" names a file, its lines are run as a
// test script, injecting input through the event queue:
//   wait <ms>          wait
//   idle               wait until no action lists or transitions are pending
//   layout <name> [ms] wait until the layout is showing and idle (60 s at most)
//   click <x> <y>      click at screen coordinates
//   clickon <name> [hotspot ID in hex]
//                      click a visible element (on a hotspot, or its picture)
//   move <x> <y>       move the mouse
//   key <esc|space|enter>
//   keydown <left|right|up|down|space>, keyup <...>
//                      hold and release a key (for the arcade game)
//   game [ms]          wait until the arcade game runs (60 s at most)
//   shot <path>        save a PNG of the screen
//   list               log the elements that react to clicks
//   quit               quit
class TestInputScript {
public:
	explicit TestInputScript(const Common::Path &path) : _line(0), _waitUntil(0), _waitStart(0) {
		Common::File f;
		if (!f.open(Common::FSNode(path))) {
			warning("Test input: can't open '%s'", path.toString().c_str());
			return;
		}
		while (!f.eos()) {
			Common::String line = f.readLine();
			line.trim();
			if (!line.empty() && !line.hasPrefix("#"))
				_lines.push_back(line);
		}
	}

	bool update(Runtime *runtime, const Graphics::Surface &screen, const byte *palette, bool inGame, uint32 now) {
		Common::EventManager *events = g_system->getEventManager();
		while (_line < _lines.size() && now >= _waitUntil) {
			Common::String cmd = _lines[_line], arg;
			size_t space = cmd.findFirstOf(' ');
			if (space != Common::String::npos) {
				arg = cmd.substr(space + 1);
				cmd = cmd.substr(0, space);
			}
			int x = 0, y = 0;
			if (cmd == "wait") {
				_waitUntil = now + atoi(arg.c_str());
			} else if (cmd == "idle") {
				if (!runtime->isIdle())
					return true;
			} else if (cmd == "layout") {
				// layout <name> [timeout ms, default 60000]
				Common::StringArray words = Common::StringTokenizer(arg).split();
				uint32 timeout = words.size() > 1 ? atoi(words[1].c_str()) : 60000;
				if (!_waitStart)
					_waitStart = now;
				bool there = !inGame && !words.empty() && runtime->getLayoutName().equalsIgnoreCase(words[0]) && runtime->isIdle();
				if (!there && now - _waitStart < timeout)
					return true;
				_waitStart = 0;
				if (there)
					debug(1, "Test input: layout %s", arg.c_str());
				else
					warning("Test input: still in %s, not %s", runtime->getLayoutName().c_str(), arg.c_str());
			} else if (cmd == "click" || cmd == "clickon") {
				Common::Point pt;
				if (cmd == "click") {
					sscanf(arg.c_str(), "%d %d", &x, &y);
					pt = Common::Point(x, y);
				} else {
					Common::StringArray words = Common::StringTokenizer(arg).split();
					uint hotspot = 0;
					if (words.size() > 1)
						sscanf(words[1].c_str(), "%x", &hotspot);
					if (words.empty() || !runtime->findElement(words[0], hotspot, pt)) {
						warning("Test input: can't click '%s' in %s", arg.c_str(), runtime->getLayoutName().c_str());
						_line++;
						continue;
					}
					debug(1, "Test input: clicking %s at %d,%d", arg.c_str(), pt.x, pt.y);
				}
				pushMouse(events, Common::EVENT_MOUSEMOVE, pt);
				pushMouse(events, Common::EVENT_LBUTTONDOWN, pt);
				pushMouse(events, Common::EVENT_LBUTTONUP, pt);
				_waitUntil = now + 100;
			} else if (cmd == "move") {
				sscanf(arg.c_str(), "%d %d", &x, &y);
				pushMouse(events, Common::EVENT_MOUSEMOVE, Common::Point(x, y));
			} else if (cmd == "key") {
				Common::Event ev;
				bool esc = arg == "esc", enter = arg == "enter";
				ev.type = Common::EVENT_KEYDOWN;
				ev.kbd = Common::KeyState(esc ? Common::KEYCODE_ESCAPE : enter ? Common::KEYCODE_RETURN : Common::KEYCODE_SPACE,
					esc ? 27 : enter ? 13 : ' ');
				events->pushEvent(ev);
				ev.type = Common::EVENT_KEYUP;
				events->pushEvent(ev);
			} else if (cmd == "keydown" || cmd == "keyup") {
				static const struct { const char *name; Common::KeyCode key; } keys[] = {
					{ "left", Common::KEYCODE_LEFT }, { "right", Common::KEYCODE_RIGHT }, { "up", Common::KEYCODE_UP },
					{ "down", Common::KEYCODE_DOWN }, { "space", Common::KEYCODE_SPACE }
				};
				Common::Event ev;
				ev.type = cmd == "keydown" ? Common::EVENT_KEYDOWN : Common::EVENT_KEYUP;
				for (const auto &k : keys)
					if (arg == k.name)
						ev.kbd = Common::KeyState(k.key, k.key == Common::KEYCODE_SPACE ? ' ' : 0);
				events->pushEvent(ev);
			} else if (cmd == "game") {
				uint32 timeout = arg.empty() ? 60000 : atoi(arg.c_str());
				if (!_waitStart)
					_waitStart = now;
				if (!inGame && now - _waitStart < timeout)
					return true;
				_waitStart = 0;
				if (!inGame)
					warning("Test input: the arcade game didn't start");
			} else if (cmd == "shot") {
				Common::DumpFile out;
				if (out.open(Common::FSNode(Common::Path(arg, Common::Path::kNativeSeparator))))
					Image::writePNG(out, screen, palette);
				else
					warning("Test input: can't write '%s'", arg.c_str());
			} else if (cmd == "list") {
				runtime->listClickable();
			} else if (cmd == "quit") {
				return false;
			} else {
				warning("Test input: unknown command '%s'", cmd.c_str());
			}
			_line++;
		}
		return true;
	}

private:
	static void pushMouse(Common::EventManager *events, Common::EventType type, const Common::Point &pt) {
		Common::Event ev;
		ev.type = type;
		ev.mouse = pt;
		events->pushEvent(ev);
	}

	Common::StringArray _lines;
	uint _line;
	uint32 _waitUntil;
	uint32 _waitStart;
};

void MSPAMEngine::runGame(const Common::String &start) {
	Runtime runtime(_media, _tdf, _title, _mixer);
	Common::ScopedPtr<TestInputScript> test;
	if (ConfMan.hasKey("mspam_test_input"))
		test.reset(new TestInputScript(Common::Path(ConfMan.get("mspam_test_input"), Common::Path::kNativeSeparator)));

	runtime.start(start);
	// Test hook: the arcade game of a planet straight away
	if (ConfMan.hasKey("mspam_busgame"))
		runtime.startBusGame("busgame1 -p " + ConfMan.get("mspam_busgame"));
	while (!shouldQuit() && !runtime.quitRequested()) {
		Common::Event event;
		while (g_system->getEventManager()->pollEvent(event)) {
			switch (event.type) {
			case Common::EVENT_MOUSEMOVE:
				runtime.mouseEvent(kEventMouseMove, event.mouse.x, event.mouse.y);
				break;
			case Common::EVENT_LBUTTONDOWN:
				runtime.mouseEvent(kEventLButtonDown, event.mouse.x, event.mouse.y);
				break;
			case Common::EVENT_LBUTTONUP:
				runtime.mouseEvent(kEventLButtonUp, event.mouse.x, event.mouse.y);
				break;
			case Common::EVENT_RBUTTONDOWN:
				runtime.mouseEvent(kEventRButtonDown, event.mouse.x, event.mouse.y);
				break;
			case Common::EVENT_RBUTTONUP:
				runtime.mouseEvent(kEventRButtonUp, event.mouse.x, event.mouse.y);
				break;
			case Common::EVENT_KEYDOWN:
				if (event.kbd.keycode == Common::KEYCODE_ESCAPE || event.kbd.keycode == Common::KEYCODE_SPACE)
					runtime.escape();
				break;
			default:
				break;
			}
		}

		uint32 now = g_system->getMillis();
		Common::String planet;
		bool lizIntro;
		if (runtime.takeBusGame(planet, lizIntro)) {
			int result = playBusGame(planet, lizIntro, &runtime, test.get());
			if (result < 0)
				break;
			runtime.busGameEnded(result);
			continue;
		}
		runtime.update(now);
		runtime.render(_screen);
		if (test && !test->update(&runtime, _screen, runtime.getPalette(), false, now))
			break;
		g_system->getPaletteManager()->setPalette(runtime.getPalette(), 0, 256);
		g_system->copyRectToScreen(_screen.getPixels(), _screen.pitch, 0, 0, kScreenWidth, kScreenHeight);
		g_system->updateScreen();
		g_system->delayMillis(10);
	}
	runtime.logStatistics();
}

// The arcade game in place of the title's window; -1 when the engine quits
int MSPAMEngine::playBusGame(const Common::String &planet, bool lizIntro, Runtime *runtime, TestInputScript *test) {
	BusGame game(_mixer, planet, lizIntro, false);
	if (!game.load())
		return game.getResult();
	while (!game.isDone()) {
		if (shouldQuit())
			return -1;
		Common::Event event;
		while (g_system->getEventManager()->pollEvent(event)) {
			switch (event.type) {
			case Common::EVENT_KEYDOWN:
				game.keyDown(event.kbd.keycode);
				if (event.kbd.ascii)
					game.charTyped(event.kbd.ascii);
				break;
			case Common::EVENT_KEYUP:
				game.keyUp(event.kbd.keycode);
				break;
			case Common::EVENT_LBUTTONDOWN:
				game.mouseDown(event.mouse.x, event.mouse.y);
				break;
			default:
				break;
			}
		}
		uint32 now = g_system->getMillis();
		game.update(now);
		game.render(_screen);
		if (test && !test->update(runtime, _screen, game.getPalette(), true, now))
			return -1;
		g_system->getPaletteManager()->setPalette(game.getPalette(), 0, 256);
		g_system->copyRectToScreen(_screen.getPixels(), _screen.pitch, 0, 0, kScreenWidth, kScreenHeight);
		g_system->updateScreen();
		g_system->delayMillis(10);
	}
	debugC(1, kDebugActions, "BUSGAME %s ends: result %d", planet.c_str(), game.getResult());
	return game.getResult();
}

Common::Error MSPAMEngine::run() {
	initGraphics(kScreenWidth, kScreenHeight);
	_screen.create(kScreenWidth, kScreenHeight, Graphics::PixelFormat::createFormatCLUT8());

	// The title's cursor table and its window class both use the standard Windows arrow
	Graphics::Cursor *cursor = Graphics::makeDefaultWinCursor();
	CursorMan.replaceCursor(cursor);
	delete cursor;
	CursorMan.showMouse(true);

	if (!loadTitle())
		return Common::kNoGameDataFoundError;

	_showHidden = ConfMan.hasKey("mspam_hidden") && ConfMan.getBool("mspam_hidden");
	_showHotspots = ConfMan.hasKey("mspam_hotspots") && ConfMan.getBool("mspam_hotspots");

	// Test hooks
	if (ConfMan.hasKey("mspam_dump")) {
		dumpLayouts(ConfMan.get("mspam_dump"));
		return Common::kNoError;
	}
	if (ConfMan.hasKey("mspam_allshots")) {
		saveAllShots(ConfMan.get("mspam_allshots"));
		return Common::kNoError;
	}

	// The game starts in Startup (the intro videos), which goes to the classroom
	Common::String start = ConfMan.hasKey("mspam_layout") ? ConfMan.get("mspam_layout") : "Startup";
	if (_title->findLayout(start) < 0) {
		warning("No layout '%s'", start.c_str());
		start = "Startup";
	}
	bool viewerMode = ConfMan.hasKey("mspam_viewer") && ConfMan.getBool("mspam_viewer");
	if (!viewerMode && !ConfMan.hasKey("mspam_click") && !ConfMan.hasKey("mspam_shot")) {
		runGame(start);
		return Common::kNoError;
	}

	// The static viewer of M2: layouts as loaded, no actions
	if (!showLayout(_title->findLayout(start)))
		return Common::kUnknownError;

	// mspam_click=x,y[;x,y...] logs what each click would trigger
	if (ConfMan.hasKey("mspam_click")) {
		Common::String spec = ConfMan.get("mspam_click");
		const char *p = spec.c_str();
		int x, y, n;
		while (sscanf(p, "%d,%d%n", &x, &y, &n) == 2) {
			hitTest(x, y);
			p += n;
			while (*p == ';' || *p == ' ')
				p++;
		}
	}
	if (ConfMan.hasKey("mspam_shot")) {
		saveShot(ConfMan.get("mspam_shot"));
		return Common::kNoError;
	}
	viewer();
	return Common::kNoError;
}

} // End of namespace MSPAM
