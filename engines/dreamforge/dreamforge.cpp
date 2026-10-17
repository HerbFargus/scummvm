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

#include "common/archive.h"
#include "common/compression/installshieldv3_archive.h"
#include "common/config-manager.h"
#include "common/debug.h"
#include "common/events.h"
#include "common/file.h"
#include "common/fs.h"
#include "common/system.h"
#include "common/textconsole.h"
#include "engines/util.h"
#include "graphics/cursorman.h"
#include "graphics/paletteman.h"
#include "image/png.h"

#include "dreamforge/console.h"
#include "dreamforge/detection.h"
#include "dreamforge/dreamforge.h"
#include "dreamforge/resource.h"
#include "dreamforge/scene.h"

namespace DreamForge {

DreamForgeEngine::DreamForgeEngine(OSystem *syst, const ADGameDescription *gameDesc) : Engine(syst),
	_gameDescription(gameDesc), _scene(nullptr), _cursors(nullptr), _scrollX(0), _showLayers(false), _dirty(true) {
	for (uint i = 0; i < kArchiveCount; i++)
		_archives[i] = nullptr;
}

DreamForgeEngine::~DreamForgeEngine() {
	for (uint i = 0; i < kArchiveCount; i++)
		delete _archives[i];
	delete _scene;
	delete _cursors;
	_screen.free();
}

bool DreamForgeEngine::hasFeature(EngineFeature f) const {
	return f == kSupportsReturnToLauncher;
}

// The game runs from 101D\ on the CD. PIG0, PIG2, PIG5 and PIG6.USR only exist
// inside the InstallShield 3 archive DATA.Z (the installer copies them to the
// hard disk), so mount that too unless they're already loose (an installed copy).
bool DreamForgeEngine::mountData() {
	// 101D\ (with SCENES\ and CINE\ below it) is already searched: the base
	// engine adds the detection's directory globs
	if (!SearchMan.hasFile("PIG0.USR")) {
		Common::InstallShieldV3 *is3 = new Common::InstallShieldV3();
		if (is3->open("DATA.Z")) {
			SearchMan.add("DATA.Z", is3, 0, true);
			debugC(1, kDebugLoading, "Mounted DATA.Z");
		} else {
			delete is3;
		}
	}

	for (uint i = 0; i < kArchiveCount; i++) {
		_archives[i] = new ResourceArchive();
		Common::Path path(Common::String::format("PIG%u.USR", i));
		if (!_archives[i]->open(path)) {
			GUIErrorMessage(Common::String::format("Can't find %s (on the CD in 101D\\ or inside DATA.Z)",
				path.toString().c_str()));
			return false;
		}
	}
	return true;
}

// PIG0 #2 is the 43-frame cursor sheet; frame 0 is the paw
void DreamForgeEngine::setupCursor() {
	Resource *res = _archives[0]->getResource(2);
	_cursors = new Sprite();
	if (!res || !_cursors->load(*res)) {
		warning("No cursor sprite");
		delete res;
		return;
	}
	delete res;
	Graphics::Surface frame = _cursors->surface.getSubArea(_cursors->frameRect(0));
	CursorMan.replaceCursor(frame, frame.w / 2, frame.h / 2, 0);
	CursorMan.showMouse(true);
}

bool DreamForgeEngine::parseSceneName(const Common::String &name, int &act, int &area, int &room) {
	Common::String n = name;
	n.toUppercase();
	return sscanf(n.c_str(), "%dX%dX%d", &act, &area, &room) == 3;
}

Common::Array<Common::String> DreamForgeEngine::listScenes() const {
	Common::ArchiveMemberList list;
	SearchMan.listMatchingMembers(list, "SCENES/*.PIG");
	Common::Array<Common::String> names;
	for (auto &m : list) {
		Common::String n = m->getFileName();
		n.toUppercase();
		names.push_back(n.substr(0, n.size() - 4));
	}
	Common::sort(names.begin(), names.end());
	return names;
}

bool DreamForgeEngine::loadScene(int act, int area, int room) {
	Scene *scene = new Scene();
	if (!scene->load(act, area, room)) {
		delete scene;
		return false;
	}
	delete _scene;
	_scene = scene;
	_layerVisible.clear();
	_layerVisible.resize(_scene->_layers.size());
	for (uint i = 0; i < _layerVisible.size(); i++)
		_layerVisible[i] = true;
	g_system->getPaletteManager()->setPalette(_scene->_palette, 0, 256);
	setScroll(0);
	_dirty = true;
	return true;
}

void DreamForgeEngine::setScroll(int x) {
	int maxScroll = _scene ? (int)_scene->_width - kScreenWidth : 0;
	_scrollX = CLIP(x, 0, MAX(maxScroll, 0));
	_dirty = true;
}

void DreamForgeEngine::redraw() {
	_screen.fillRect(Common::Rect(kScreenWidth, kScreenHeight), 0);
	if (_scene && _scene->_background.getPixels()) {
		Common::Rect src(_scrollX, 0, MIN<int>(_scrollX + kScreenWidth, _scene->_width),
			MIN<int>(kScreenHeight, _scene->_height));
		_screen.copyRectToSurface(_scene->_background, 0, 0, src);

		// Debug view only: the game draws each layer when its state flags say so
		if (_showLayers) {
			for (uint i = 0; i < _scene->_layers.size(); i++) {
				if (!_layerVisible[i])
					continue;
				const Layer *l = _scene->_layers[i];
				Common::Rect dst(l->x - _scrollX, l->y, l->x - _scrollX + l->surface.w, l->y + l->surface.h);
				Common::Rect clipped = dst.findIntersectingRect(Common::Rect(kScreenWidth, kScreenHeight));
				if (clipped.isEmpty())
					continue;
				Common::Rect part(clipped.left - dst.left, clipped.top - dst.top,
					clipped.right - dst.left, clipped.bottom - dst.top);
				_screen.copyRectToSurfaceWithKey(l->surface, clipped.left, clipped.top, part, 0);
			}
		}
	}
	g_system->copyRectToScreen(_screen.getPixels(), _screen.pitch, 0, 0, kScreenWidth, kScreenHeight);
	g_system->updateScreen();
	_dirty = false;
}

bool DreamForgeEngine::saveShot(const Common::String &path) {
	if (_dirty)
		redraw();
	Common::DumpFile out;
	if (!out.open(Common::Path(path, '/'), true))
		return false;
	return Image::writePNG(out, _screen, _scene ? _scene->_palette : nullptr);
}

// Test hook: dreamforge_allscenes=<dir> writes <dir>/<scene>.png (and _layers.png) for every scene
void DreamForgeEngine::dumpAllScenes(const Common::String &dir) {
	Common::Array<Common::String> scenes = listScenes();
	for (auto &name : scenes) {
		int act, area, room;
		if (!parseSceneName(name, act, area, room) || !loadScene(act, area, room))
			continue;
		_showLayers = false;
		saveShot(Common::String::format("%s/%s.png", dir.c_str(), name.c_str()));
		if (!_scene->_layers.empty()) {
			_showLayers = true;
			_dirty = true;
			saveShot(Common::String::format("%s/%s_layers.png", dir.c_str(), name.c_str()));
		}
	}
	debug("dumped %u scenes to %s", scenes.size(), dir.c_str());
}

void DreamForgeEngine::handleEvents() {
	Common::Event event;
	while (g_system->getEventManager()->pollEvent(event)) {
		switch (event.type) {
		case Common::EVENT_MOUSEMOVE:
			_mousePos = event.mouse;
			break;
		case Common::EVENT_KEYDOWN:
			if (event.kbd.keycode == Common::KEYCODE_LEFT)
				setScroll(_scrollX - 32);
			else if (event.kbd.keycode == Common::KEYCODE_RIGHT)
				setScroll(_scrollX + 32);
			else if (event.kbd.keycode == Common::KEYCODE_l) {
				_showLayers = !_showLayers;
				_dirty = true;
			}
			break;
		default:
			break;
		}
	}

	// Rooms wider than the screen scroll when the cursor reaches an edge
	if (_scene && _scene->_width > kScreenWidth) {
		if (_mousePos.x < 8)
			setScroll(_scrollX - 8);
		else if (_mousePos.x >= kScreenWidth - 8)
			setScroll(_scrollX + 8);
	}
}

void DreamForgeEngine::mainLoop() {
	while (!shouldQuit()) {
		handleEvents();
		if (_dirty)
			redraw();
		else
			g_system->updateScreen();
		g_system->delayMillis(16);
	}
}

Common::Error DreamForgeEngine::run() {
	initGraphics(kScreenWidth, kScreenHeight);
	_screen.create(kScreenWidth, kScreenHeight, Graphics::PixelFormat::createFormatCLUT8());
	setDebugger(new Console(this));

	if (!mountData())
		return Common::kNoGameDataFoundError;

	Common::String dumpDir = ConfMan.get("dreamforge_allscenes");
	if (!dumpDir.empty()) {
		dumpAllScenes(dumpDir);
		return Common::kNoError;
	}

	// Until the game flow exists, start in the first room (or dreamforge_scene=1X01X06)
	Common::String start = ConfMan.hasKey("dreamforge_scene") ? ConfMan.get("dreamforge_scene") : "1X01X01";
	int act, area, room;
	if (!parseSceneName(start, act, area, room) || !loadScene(act, area, room))
		return Common::kNoGameDataFoundError;
	setupCursor();
	if (ConfMan.hasKey("dreamforge_scroll"))
		setScroll(ConfMan.getInt("dreamforge_scroll"));
	_showLayers = ConfMan.getBool("dreamforge_layers");

	Common::String shot = ConfMan.get("dreamforge_shot");
	if (!shot.empty()) {
		saveShot(shot);
		return Common::kNoError;
	}

	mainLoop();
	return Common::kNoError;
}

} // End of namespace DreamForge
