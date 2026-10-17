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
#include "common/tokenizer.h"
#include "common/textconsole.h"
#include "engines/util.h"
#include "graphics/cursorman.h"
#include "graphics/paletteman.h"
#include "image/png.h"

#include "dreamforge/console.h"
#include "dreamforge/cutscene.h"
#include "dreamforge/detection.h"
#include "dreamforge/dreamforge.h"
#include "dreamforge/lipsync.h"
#include "dreamforge/resource.h"
#include "dreamforge/scene.h"
#include "dreamforge/sound.h"

namespace DreamForge {

DreamForgeEngine::DreamForgeEngine(OSystem *syst, const ADGameDescription *gameDesc) : Engine(syst),
	_gameDescription(gameDesc), _sound(nullptr), _scene(nullptr), _cursors(nullptr), _scrollX(0), _showLayers(false), _dirty(true) {
	for (uint i = 0; i < kArchiveCount; i++)
		_archives[i] = nullptr;
}

DreamForgeEngine::~DreamForgeEngine() {
	delete _sound;
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
	if (_sound)
		_sound->playMusic(_scene->_music, true);  // C.PRG 0x1db00: -666 keeps the current track
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

bool DreamForgeEngine::playCutscene(int number) {
	Cutscene cutscene(this);
	bool ok = cutscene.play(number);
	if (_scene)
		g_system->getPaletteManager()->setPalette(_scene->_palette, 0, 256);
	_dirty = true;
	return ok;
}

// Test hook: dreamforge_cine=<n,n,...> plays cutscenes and quits; dreamforge_fast=true doesn't wait
// between frames; dreamforge_cine_frames=<f,f,...> with dreamforge_shot_dir=<dir> saves frames
void DreamForgeEngine::runCutsceneHook() {
	Cutscene cutscene(this);
	cutscene._fast = ConfMan.getBool("dreamforge_fast");
	cutscene._shotDir = ConfMan.get("dreamforge_shot_dir");
	Common::StringTokenizer frames(ConfMan.get("dreamforge_cine_frames"), ",");
	while (!frames.empty())
		cutscene._shotFrames.push_back(atoi(frames.nextToken().c_str()));
	Common::StringTokenizer cines(ConfMan.get("dreamforge_cine"), ",");
	while (!cines.empty() && !shouldQuit())
		cutscene.play(atoi(cines.nextToken().c_str()));
}

bool DreamForgeEngine::sayDemo(int voiceId, int actor, int slot, int view, const Common::String &shotDir) {
	AnimFrames anim;
	uint index = actorAnimIndex(actor, slot, view);
	if (!anim.load(_archives[2], index)) {
		warning("say: no animation PIG2 #%u", index);
		return false;
	}
	Common::Array<byte> visemes;
	if (!LipSync::load(this, voiceId, visemes))
		warning("say: voice %d has no lip sync", voiceId);
	debugC(1, kDebugSound, "say %d: actor %d slot %d view %d (PIG2 #%u, %u frames)", voiceId, actor, slot, view,
		index, anim.frames.size());

	g_system->getPaletteManager()->setPalette(anim.palette, 0, 256);
	const Graphics::Surface *first = anim.frames[0];
	int x = (kScreenWidth - first->w) / 2, y = (kScreenHeight - first->h) / 2;
	_sound->playVoice(voiceId);
	uint32 start = g_system->getMillis();
	Common::String trace;
	for (uint step = 0; step <= visemes.size() && !shouldQuit(); step++) {
		// 0 (or the end of the string) ends the line, and so does the end of the voice
		byte v = step < visemes.size() ? visemes[step] : 0;
		if (v == 0 || (step > 0 && shotDir.empty() && !_sound->isVoicePlaying()))
			break;
		uint frame = (v - 1) % anim.frames.size();
		trace += Common::String::format("%u", frame);
		_screen.fillRect(Common::Rect(kScreenWidth, kScreenHeight), 0);
		_screen.copyRectToSurface(*anim.frames[frame], x, y, Common::Rect(first->w, first->h));
		g_system->copyRectToScreen(_screen.getPixels(), _screen.pitch, 0, 0, kScreenWidth, kScreenHeight);
		g_system->updateScreen();
		if (!shotDir.empty()) {
			Common::DumpFile out;
			if (out.open(Common::Path(Common::String::format("%s/say%d_%03u_f%u.png", shotDir.c_str(), voiceId, step, frame), '/'), true))
				Image::writePNG(out, *anim.frames[frame], anim.palette);
		}
		uint32 due = start + (step + 1) * kLipSyncStepMs;
		while (g_system->getMillis() < due && !shouldQuit()) {
			Common::Event event;
			while (g_system->getEventManager()->pollEvent(event)) {
			}
			g_system->delayMillis(5);
		}
	}
	debugC(1, kDebugSound, "say %d frames: %s", voiceId, trace.c_str());
	if (_scene)
		g_system->getPaletteManager()->setPalette(_scene->_palette, 0, 256);
	_dirty = true;
	return true;
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
	_sound = new Sound(this, _mixer);
	_sound->init();

	// Test hook: dreamforge_say=<voice>[,actor,slot,view] runs the lip sync demo and quits
	if (ConfMan.hasKey("dreamforge_say")) {
		int v = 0, a = 1, s = talkSlot(false, false, true), w = 0;
		sscanf(ConfMan.get("dreamforge_say").c_str(), "%d,%d,%d,%d", &v, &a, &s, &w);
		sayDemo(v, a, s, w, ConfMan.get("dreamforge_shot_dir"));
		return Common::kNoError;
	}
	if (ConfMan.hasKey("dreamforge_cine")) {
		runCutsceneHook();
		return Common::kNoError;
	}

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
