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

#include "dreamforge/actor.h"
#include "dreamforge/console.h"
#include "dreamforge/cutscene.h"
#include "dreamforge/detection.h"
#include "dreamforge/dfa.h"
#include "dreamforge/dreamforge.h"
#include "dreamforge/lipsync.h"
#include "dreamforge/resource.h"
#include "dreamforge/scene.h"
#include "dreamforge/script.h"
#include "dreamforge/sound.h"

namespace DreamForge {

DreamForgeEngine::DreamForgeEngine(OSystem *syst, const ADGameDescription *gameDesc) : Engine(syst),
	_gameDescription(gameDesc), _sound(nullptr), _scene(nullptr), _cursors(nullptr), _scrollX(0), _showLayers(false), _dirty(true), _rnd("dreamforge") {
	memset(_flags, 0, sizeof(_flags));
	memset(_hotspotState, 0, sizeof(_hotspotState));
	memset(_layerState, 0, sizeof(_layerState));
	memset(_objectState, 0, sizeof(_objectState));
	_heldItem = -1;
	_cursorId = -1;
	_cursorFrame = 0;
	_currentActor = 1;
	_counter3D = 0;
	_actors[0] = new Actor(this, 1);
	_actors[1] = new Actor(this, 2);
	_nextStep = 0;
	_inScript = false;
	_shotEvery = _nextShot = _shotCount = 0;
	_currentRegion = -1;
	_quitAt = 0;
	for (uint i = 0; i < kArchiveCount; i++)
		_archives[i] = nullptr;
}

DreamForgeEngine::~DreamForgeEngine() {
	delete _sound;
	delete _actors[0];
	delete _actors[1];
	for (auto &f : _actorFrames)
		delete f._value;
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
	setCursor(0);
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
	_walkMap.load(_scene->_walkMap);
	_currentRegion = -1;
	for (auto *o : _scene->_objects)
		startObject(*o);
	updateObjects(true);
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

void DreamForgeEngine::redraw(bool present) {
	_screen.fillRect(Common::Rect(kScreenWidth, kScreenHeight), 0);
	if (_scene && _scene->_background.getPixels()) {
		Common::Rect src(_scrollX, 0, MIN<int>(_scrollX + kScreenWidth, _scene->_width),
			MIN<int>(kScreenHeight, _scene->_height));
		_screen.copyRectToSurface(_scene->_background, 0, 0, src);
		drawScene();
	}
	if (present && _shotEvery && g_system->getMillis() >= _nextShot) {
		_nextShot = g_system->getMillis() + _shotEvery;
		Common::DumpFile out;
		if (out.open(Common::Path(Common::String::format("%s/t%04u.png", _shotDir.c_str(), _shotCount++), '/'), true))
			Image::writePNG(out, _screen, _scene ? _scene->_palette : nullptr);
	}
	if (present) {
		g_system->copyRectToScreen(_screen.getPixels(), _screen.pitch, 0, 0, kScreenWidth, kScreenHeight);
		g_system->updateScreen();
	}
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

bool DreamForgeEngine::isFlagSet(uint flag) const {
	return flag < kFlagCount && (_flags[flag >> 3] & (1 << (flag & 7)));
}

void DreamForgeEngine::setFlag(uint flag, bool on) {
	if (flag >= kFlagCount)
		return;
	debugC(1, kDebugScript, "flag %u %s", flag, on ? "set" : "cleared");
	if (on)
		_flags[flag >> 3] |= 1 << (flag & 7);
	else
		_flags[flag >> 3] &= ~(1 << (flag & 7));
}

const Record *DreamForgeEngine::hotspotAt(const Common::Point &p) const {
	if (!_scene)
		return nullptr;
	// Mask 4 is things to click on, 2 things to use; 1 marks walk-in regions (exits, shading)
	for (auto &h : _scene->_hotspots)
		if ((h.mask & 6) && h.rect.contains(p))
			return &h;
	return nullptr;
}

const Record *DreamForgeEngine::regionAt(int y, int x) const {
	if (!_scene)
		return nullptr;
	for (auto &h : _scene->_hotspots)
		if ((h.mask & 1) && h.rect.contains(Common::Point(x, y)))
			return &h;
	return nullptr;
}

// 0x18d00 / 0x1f8c0: walking into a region runs its script once
void DreamForgeEngine::checkRegions() {
	if (_inScript || !_scene)
		return;
	const Actor *a = _actors[0];
	const Record *r = a->_hidden ? nullptr : regionAt(a->_y, a->_x);
	int id = r ? r->id : -1;
	if (id == _currentRegion)
		return;
	_currentRegion = id;
	if (r)
		useHotspot(*r, false);
}

const Record *DreamForgeEngine::findHotspot(int id) const {
	if (_scene)
		for (auto &h : _scene->_hotspots)
			if (h.id == id)
				return &h;
	return nullptr;
}

void DreamForgeEngine::useHotspot(const Record &hotspot, bool walk) {
	int visits = 0;
	if (hotspot.id >= 0 && hotspot.id < kStateCount)
		visits = ++_hotspotState[hotspot.id];
	debugC(1, kDebugScript, "hotspot %d \"%s\", visit %d", hotspot.id, hotspot.name.c_str(), visits);
	// 0x258c0: who walks to the hotspot's walk-to point (the game reads puppy 1's point even for puppy 2)
	int who = hotspot.who == 4 ? _currentActor : hotspot.who;
	_inScript = true;
	if (walk && who >= 1 && who <= 3 && hotspot.walkX[0] >= 0 && hotspot.walkY[0] >= 0)
		actorWalk(who, hotspot.walkY[0], hotspot.walkX[0], hotspot.walkPose[0] < 8 ? hotspot.walkPose[0] : -1);
	ScriptRunner runner(this);
	runner.run(hotspot.script(), visits, Common::Point((hotspot.rect.left + hotspot.rect.right) / 2,
		(hotspot.rect.top + hotspot.rect.bottom) / 2));
	_inScript = false;
	_dirty = true;
}

bool DreamForgeEngine::playSceneAnim(int cine, int top, int left, bool flip, int delay, int depth, bool noWait, int hideActor) {
	if (!_scene)
		return false;
	// 0x23af0: a temporary scene object, drawn in depth order with everything else
	SceneObject *o = new SceneObject();
	o->anim = Common::String::format("%04d.DFA", cine);
	o->id = 0xA2C2A + cine;
	o->temporary = true;
	startObject(*o);
	if (!o->dfa) {
		deleteObject(o);
		return false;
	}
	o->rect = Common::Rect(left, top, left + o->dfa->getWidth(), top + o->dfa->getHeight());
	o->depth = depth == -1 ? top + o->dfa->getHeight() - 1 : depth;
	o->flags = 4 | (flip ? 1 : 0);
	o->delay = delay;
	o->nextTick = ticks() + delay;
	// The puppy the animation shows is hidden until it ends
	o->hideActor = hideActor;
	for (int n = 1; n <= 2; n++)
		if (hideActor == n || hideActor == 3)
			getActor(n)->_hidden = true;
	_scene->_objects.push_back(o);
	debugC(1, kDebugScript, "scene anim %d at (%d, %d), %d frames, depth %d%s%s", cine, left, top,
		o->dfa->getFrameCount(), o->depth, flip ? ", flipped" : "", noWait ? ", no wait" : "");
	if (noWait)
		return true;
	int id = o->id;
	auto running = [&]() {
		for (auto *obj : _scene->_objects)
			if (obj->id == id)
				return true;
		return false;
	};
	while (_scene && running() && !shouldQuit())
		runStep();
	return true;
}

void DreamForgeEngine::waitForVoice() {
	while (_sound->isVoicePlaying() && !shouldQuit()) {
		Common::Event event;
		while (g_system->getEventManager()->pollEvent(event)) {
		}
		if (_dirty)
			redraw();
		g_system->updateScreen();
		g_system->delayMillis(10);
	}
}

int DreamForgeEngine::getRefScale() const {
	return _scene && _scene->_refScale > 0 ? _scene->_refScale : 100;
}

const AnimFrames *DreamForgeEngine::getActorFrames(uint pig2Index) {
	if (_actorFrames.contains(pig2Index))
		return _actorFrames[pig2Index];
	AnimFrames *f = new AnimFrames();
	if (!f->load(_archives[2], pig2Index)) {
		delete f;
		f = nullptr;
	}
	_actorFrames[pig2Index] = f;
	return f;
}

bool DreamForgeEngine::findWaypointPath(int y0, int x0, int y1, int x1, Common::Array<Common::Point> &via) const {
	via.clear();
	if (!_scene)
		return false;
	// Edge ends with the same coordinates are the same node
	Common::Array<Common::Point> nodes;
	Common::Array<Common::Array<uint> > links;
	auto nodeFor = [&](int y, int x) -> uint {
		for (uint i = 0; i < nodes.size(); i++)
			if (nodes[i].x == x && nodes[i].y == y)
				return i;
		nodes.push_back(Common::Point(x, y));
		links.push_back(Common::Array<uint>());
		return nodes.size() - 1;
	};
	for (auto &e : _scene->_edges) {
		uint a = nodeFor(e.y1, e.x1), b = nodeFor(e.y2, e.x2);
		links[a].push_back(b);
		links[b].push_back(a);
	}
	// Shortest route from a node the start can see to one that sees the target
	uint n = nodes.size();
	Common::Array<int> dist(n), prev(n);
	Common::Array<bool> done(n);
	for (uint i = 0; i < n; i++) {
		dist[i] = _walkMap.lineClear(this, y0, x0, nodes[i].y, nodes[i].x) ?
			ABS(nodes[i].x - x0) + ABS(nodes[i].y - y0) : INT_MAX;
		prev[i] = -1;
		done[i] = false;
	}
	int best = -1, bestTotal = INT_MAX;
	while (true) {
		int u = -1;
		for (uint i = 0; i < n; i++)
			if (!done[i] && dist[i] != INT_MAX && (u < 0 || dist[i] < dist[u]))
				u = i;
		if (u < 0)
			break;
		done[u] = true;
		if (_walkMap.lineClear(this, nodes[u].y, nodes[u].x, y1, x1)) {
			int total = dist[u] + ABS(nodes[u].x - x1) + ABS(nodes[u].y - y1);
			if (total < bestTotal) {
				bestTotal = total;
				best = u;
			}
		}
		for (uint v : links[u]) {
			int d = dist[u] + ABS(nodes[u].x - nodes[v].x) + ABS(nodes[u].y - nodes[v].y);
			if (d < dist[v]) {
				dist[v] = d;
				prev[v] = u;
			}
		}
	}
	if (best < 0)
		return false;
	for (int i = best; i >= 0; i = prev[i])
		via.insert_at(0, nodes[i]);
	return true;
}

void DreamForgeEngine::placeActorsDefault() {
	if (!_scene)
		return;
	const Scene::Spot &s = _scene->_spots[0];
	if (s.type >= 2 && s.type <= 4 && _walkMap.isWalkable(this, s.y, s.x)) {
		_actors[1]->place(s.y, s.x, s.dir);
		int x = s.x;
		for (int dx = 60; dx < 400 && x == s.x; dx += 20) {
			if (_walkMap.isWalkable(this, s.y, s.x - dx))
				x = s.x - dx;
			else if (_walkMap.isWalkable(this, s.y, s.x + dx))
				x = s.x + dx;
		}
		_actors[0]->place(s.y, x, s.dir);
		_actors[0]->_hidden = _actors[1]->_hidden = false;
	} else {
		_actors[0]->_hidden = _actors[1]->_hidden = true;
	}
}

bool DreamForgeEngine::conditionsHold(const byte *conds, uint count) const {
	for (uint i = 0; i < count; i++) {
		int16 c = (int16)READ_LE_UINT16(conds + i * 2);
		if (c == 0)
			break;
		if (c > 0 ? !isFlagSet(c) : isFlagSet(-c))
			return false;
	}
	return true;
}

// Puppy depth shading (C.PRG 0x25350): darker the further back they stand
void DreamForgeEngine::shadeActors() {
	if (!_scene)
		return;
	for (int n = 1; n <= 2; n++) {
		const Actor *a = getActor(n);
		int first = n == 1 ? 32 : 16;
		int bright = 100;
		int scale = _walkMap.scaleAt(this, a->_y, a->_x);
		if (_scene->_shade[0] && _scene->_shade[1] && scale > 0) {
			int pct = scale * 100 / getRefScale();
			bright = CLIP(100 - ((100 - pct) / _scene->_shade[0]) * _scene->_shade[1], 0, 100);
		}
		byte pal[16 * 3];
		for (int i = 0; i < 16 * 3; i++)
			pal[i] = (_scene->_palette[first * 3 + i] * bright + 50) / 100;
		g_system->getPaletteManager()->setPalette(pal, first, 16);
	}
}

void DreamForgeEngine::drawScene() {
	// Painter's order by depth: layers and objects by their depth (+0x10), puppies by their feet
	struct Item {
		int depth;
		int kind;  // 0 layer, 1 object, 2 puppy
		int index;
	};
	Common::Array<Item> items;
	for (uint i = 0; i < _scene->_layers.size(); i++) {
		const Layer *l = _scene->_layers[i];
		bool on = _showLayers ? _layerVisible[i] : conditionsHold(l->data.data() + 0x272, 9);
		if (on) {
			Item it = { (int32)READ_LE_UINT32(l->data.data() + 0x10), 0, (int)i };
			items.push_back(it);
		}
	}
	for (uint i = 0; i < _scene->_objects.size(); i++) {
		const SceneObject &o = *_scene->_objects[i];
		if (o.dfa && o.frame >= 0 && (o.temporary || conditionsHold(o.data.data() + 0x272, 9)) && !(o.flags2 & 0x80)) {
			Item it = { o.depth, 1, (int)i };
			items.push_back(it);
		}
	}
	for (int n = 0; n < 2; n++) {
		if (!_actors[n]->_hidden) {
			Item it = { _actors[n]->_y, 2, n };
			items.push_back(it);
		}
	}
	Common::sort(items.begin(), items.end(), [](const Item &a, const Item &b) {
		return a.depth < b.depth || (a.depth == b.depth && a.kind < b.kind);
	});
	Common::Rect screen(kScreenWidth, kScreenHeight);
	for (auto &it : items) {
		if (it.kind == 2) {
			const Actor *a = _actors[it.index];
			a->draw(_screen, _scrollX, _walkMap.scaleAt(this, a->_y, a->_x), getRefScale());
			continue;
		}
		const Graphics::Surface *surf;
		Common::Point pos;
		bool keyed;
		if (it.kind == 0) {
			const Layer *l = _scene->_layers[it.index];
			surf = &l->surface;
			pos = Common::Point(l->x, l->y);
			keyed = true;
		} else {
			const SceneObject &o = *_scene->_objects[it.index];
			surf = o.dfa->getSurface();
			pos = Common::Point(o.rect.left, o.rect.top);
			keyed = false;
		}
		Common::Rect dst(pos.x - _scrollX, pos.y, pos.x - _scrollX + surf->w, pos.y + surf->h);
		Common::Rect clipped = dst.findIntersectingRect(screen);
		if (clipped.isEmpty())
			continue;
		Common::Rect part(clipped.left - dst.left, clipped.top - dst.top, clipped.right - dst.left, clipped.bottom - dst.top);
		bool mirror = it.kind == 1 && (_scene->_objects[it.index]->flags & 1);
		if (mirror) {
			for (int y = clipped.top; y < clipped.bottom; y++) {
				const byte *src = (const byte *)surf->getBasePtr(0, y - dst.top);
				byte *out = (byte *)_screen.getBasePtr(0, y);
				for (int x = clipped.left; x < clipped.right; x++)
					out[x] = src[surf->w - 1 - (x - dst.left)];
			}
		} else if (keyed) {
			_screen.copyRectToSurfaceWithKey(*surf, clipped.left, clipped.top, part, 0);
		} else {
			_screen.copyRectToSurface(*surf, clipped.left, clipped.top, part);
		}
	}
	shadeActors();
}

void DreamForgeEngine::startObject(SceneObject &o) {
	o.dfa = new DFADecoder();
	if (!o.dfa->load(SearchMan.createReadStreamForMember(Common::Path("CINE/" + o.anim, '/')))) {
		warning("No animation %s for object %d", o.anim.c_str(), o.id);
		delete o.dfa;
		o.dfa = nullptr;
		return;
	}
	o.act = new ActScript();
	Common::String act = o.anim;
	act.erase(act.size() - 4);
	o.act->load("CINE/" + act + ".ACT");
	o.frame = -1;
}

// 0x22100: the next frame; at the end, a looping object starts again, others finish
void DreamForgeEngine::advanceObject(SceneObject &o) {
	if (!o.dfa)
		return;
	if (o.frame == -1 || o.frame >= o.dfa->getFrameCount() - 1) {
		if (o.frame != -1 && !(o.flags & 0x08)) {
			o.flags |= 0x10;
			return;
		}
		o.dfa->rewind();
		o.frame = -1;
	}
	if (o.dfa->decodeNextFrame()) {
		o.frame++;
		bool stop = false;
		if ((uint)o.frame < o.act->frames.size())
			runActCues(this, o.act->frames[o.frame], stop);
	}
}

// 0x231c0, once per game step
void DreamForgeEngine::updateObjects(bool force) {
	if (!_scene)
		return;
	uint32 now = ticks();
	for (uint i = 0; i < _scene->_objects.size(); i++) {
		SceneObject &o = *_scene->_objects[i];
		bool live = force || (!(o.flags & 0x10) && (o.temporary || conditionsHold(o.data.data() + 0x272, 9)));
		if (!live || now < o.nextTick)
			continue;
		if ((o.flags3 & 1) && !(o.flags & 4))
			continue;
		// TODO: objects that follow a path (+0x32 & 4)
		advanceObject(o);
		o.nextTick = now + o.delay;
		if (o.frame != -1)
			o.flags &= ~4;
		if (o.temporary && (o.flags & 0x10)) {
			if (o.hideActor)
				for (int n = 1; n <= 2; n++)
					if (o.hideActor == n || o.hideActor == 3)
						getActor(n)->_hidden = false;
			deleteObject(&o);
			_scene->_objects.remove_at(i);
			i--;
		}
	}
}

// C.PRG 0x82d7c: per cursor, first frame in the cursor sheet (PIG0 #2), frame count, hotspot y, x
static const int8 kCursorDefs[][4] = {
	{ 0, 2, 12, 12 }, { 2, 1, 12, 12 }, { 2, 1, 12, 12 }, { 2, 1, 12, 12 }, { 2, 1, 12, 12 }, { 2, 1, 12, 12 },
	{ 2, 1, 12, 12 }, { 3, 13, 8, 9 }, { 16, 2, 10, 20 }, { 18, 2, 10, 23 }, { 20, 2, 18, 4 }, { 22, 2, 18, 39 },
	{ 24, 2, 4, 11 }, { 26, 2, 37, 11 }, { 28, 2, 3, 39 }, { 30, 2, 3, 4 }, { 32, 2, 10, 19 }, { 34, 2, 10, 24 },
	{ 36, 1, 8, 10 }, { 37, 1, 8, 10 }, { 38, 1, 4, 9 }, { 39, 1, 4, 9 }, { 40, 1, 9, 10 }, { 41, 1, 9, 10 },
	{ 42, 1, 11, 12 }, { 42, 1, 11, 12 }
};

enum {
	kCursorPaw = 0,
	kCursorWait = 7,
	kCursorScrollLeft = 0x10,
	kCursorScrollRight = 0x11
};

// 0x82d5c: an exit's direction -> arrow cursor
static const int8 kExitArrows[8] = { 12, 14, 9, 11, 13, 10, 8, 15 };

int DreamForgeEngine::cursorFor(const Common::Point &screen) const {
	if (_inScript)
		return kCursorWait;
	if (!_scene)
		return kCursorPaw;
	Common::Point p(screen.x + _scrollX, screen.y);
	const Hotspot *hot = nullptr;
	for (auto &h : _scene->_hotspots)
		if (h.rect.contains(p)) {
			hot = &h;
			break;
		}
	// 0x13b40: the plain cursor turns into a scroll arrow near the edges of a wide room
	int id = kCursorPaw;
	if (_walkMap.isWalkable(this, p.y, p.x) && (int)_scene->_width > kScreenWidth) {
		if (screen.x < 0x3D && _scrollX > 0)
			id = kCursorScrollLeft;
		else if (screen.x > 0x243 && _scrollX < (int)_scene->_width - kScreenWidth)
			id = kCursorScrollRight;
	}
	if (hot) {
		// 0x13c20: the hotspot's mask picks the cursor (TODO: held item cursors)
		uint16 m = hot->mask;
		if (m & 8)
			id = 1;
		else if (m & 0x40)
			id = 6;
		else if (m & 2)
			id = 5;
		else if (m & 0x10)
			id = 3;
		else if (m & 0x20)
			id = 4;
		else if (m & 4)
			id = 2;
		if ((id == kCursorPaw || id == kCursorScrollLeft || id == kCursorScrollRight) && hot->exitDir >= 0 && hot->exitDir < 8)
			id = kExitArrows[hot->exitDir];
	}
	return id;
}

void DreamForgeEngine::setCursor(int id) {
	if (id == _cursorId || !_cursors || !_cursors->frames)
		return;
	_cursorId = id;
	_cursorFrame = 0;
	animateCursor();
}

void DreamForgeEngine::animateCursor() {
	if (!_cursors || _cursorId < 0)
		return;
	const int8 *def = kCursorDefs[_cursorId];
	int frame = MIN<int>(def[0] + _cursorFrame % def[1], _cursors->frames - 1);
	_cursorFrame++;
	Graphics::Surface f = _cursors->surface.getSubArea(_cursors->frameRect(frame));
	CursorMan.replaceCursor(f, def[3], def[2], 0);
}

void DreamForgeEngine::runStep() {
	Common::Event event;
	while (g_system->getEventManager()->pollEvent(event)) {
		if (event.type == Common::EVENT_MOUSEMOVE)
			_mousePos = event.mouse;
	}
	setCursor(cursorFor(_mousePos));
	uint32 now = g_system->getMillis();
	if (now < _nextStep) {
		g_system->delayMillis(MIN<uint32>(_nextStep - now, 10));
		return;
	}
	_nextStep = MAX(_nextStep + kStepMs, now);
	animateCursor();
	for (int n = 0; n < 2; n++)
		_actors[n]->step();
	updateObjects(false);
	redraw();
}

void DreamForgeEngine::actorTurn(Actor &a, int dir) {
	// 0x19ef0: the shorter way round, one turning animation per 45 degrees
	if (dir < 0 || dir > 7)
		return;
	while (a._dir != dir && !shouldQuit()) {
		int cw = (dir - a._dir + 8) & 7;
		int stepDir = cw <= 4 ? 1 : -1;
		// TODO: check which of slots 2 and 4 turns which way
		actorPlay(a, stepDir > 0 ? kSlotTurnA : kSlotTurnB);
		a.setDirection((a._dir + stepDir + 8) & 7);
	}
	a.idle();
}

void DreamForgeEngine::actorPlay(Actor &a, int slot) {
	// 0x16c40: one frame per game step
	int frames = actorAnimFrames(slot);
	a._busy = true;
	for (int f = 0; f < frames && !shouldQuit(); f++) {
		a._slot = slot;
		a._frame = f;
		redraw();
		uint32 due = g_system->getMillis() + kStepMs;
		while (g_system->getMillis() < due && !shouldQuit()) {
			Common::Event event;
			while (g_system->getEventManager()->pollEvent(event)) {
			}
			g_system->delayMillis(5);
		}
	}
	a._busy = false;
	if (slot == kSlotSitDown)
		a._posture = kSitting;
	else if (slot == kSlotStandUp || slot == kSlotGetUp)
		a._posture = kStanding;
	else if (slot == kSlotLieDown || slot == kSlotLieDownAlt)
		a._posture = kLying;
	a.idle();
	redraw();
}

void DreamForgeEngine::actorWalk(int actor, int y, int x, int dir) {
	for (int n = 1; n <= 2; n++) {
		if (actor != n && actor != 3)
			continue;
		Actor *a = getActor(n);
		int ty = y == 0xFFFF ? a->_y : y, tx = x == 0xFFFF ? a->_x : x;
		a->walkTo(ty, tx);
	}
	while (!shouldQuit() && (((actor == 1 || actor == 3) && _actors[0]->isWalking()) || ((actor == 2 || actor == 3) && _actors[1]->isWalking())))
		runStep();
	for (int n = 1; n <= 2; n++)
		if ((actor == n || actor == 3) && dir >= 0 && dir < 8)
			actorTurn(*getActor(n), dir);
}

void DreamForgeEngine::actorSay(int actor, int voiceId, bool quick) {
	Actor *a = getActor(actor);
	if (!a || a->_hidden) {
		_sound->playVoice(voiceId);
		waitForVoice();
		return;
	}
	a->stopWalking();
	Common::Array<byte> visemes;
	LipSync::load(this, voiceId, visemes);
	int slot = talkSlot(a->_posture == kLying, a->_posture == kSitting, quick);
	_sound->playVoice(voiceId);
	a->_busy = true;
	uint32 start = g_system->getMillis();
	for (uint step = 0; !shouldQuit(); step++) {
		byte v = step < visemes.size() ? visemes[step] : 0;
		if (v == 0 && step >= visemes.size() && !_sound->isVoicePlaying())
			break;
		if (step > 0 && !_sound->isVoicePlaying())
			break;
		a->_slot = slot;
		a->_frame = v ? (v - 1) % 6 : 0;
		redraw();
		uint32 due = start + (step + 1) * kLipSyncStepMs;
		while (g_system->getMillis() < due && !shouldQuit()) {
			Common::Event event;
			while (g_system->getEventManager()->pollEvent(event)) {
			}
			g_system->delayMillis(5);
		}
	}
	a->_busy = false;
	a->idle();
	redraw();
}

void DreamForgeEngine::handleEvents() {
	Common::Event event;
	while (g_system->getEventManager()->pollEvent(event)) {
		switch (event.type) {
		case Common::EVENT_MOUSEMOVE:
			_mousePos = event.mouse;
			break;
		case Common::EVENT_LBUTTONUP: {
			Common::Point p(event.mouse.x + _scrollX, event.mouse.y);
			const Record *h = hotspotAt(p);
			if (h)
				useHotspot(*h);
			else if (_walkMap.isWalkable(this, p.y, p.x))
				_actors[0]->walkTo(p.y, p.x);
			break;
		}
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
	while (!shouldQuit() && (!_quitAt || g_system->getMillis() < _quitAt)) {
		handleEvents();
		setCursor(cursorFor(_mousePos));
		uint32 now = g_system->getMillis();
		if (now >= _nextStep) {
			_nextStep = MAX(_nextStep + kStepMs, now);
			for (int n = 0; n < 2; n++)
				_actors[n]->step();
			updateObjects(false);
			checkRegions();
			animateCursor();
			_dirty = true;
		}
		if (_dirty)
			redraw();
		else
			g_system->updateScreen();
		g_system->delayMillis(10);
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
	placeActorsDefault();
	setupCursor();
	if (ConfMan.hasKey("dreamforge_scroll"))
		setScroll(ConfMan.getInt("dreamforge_scroll"));
	_showLayers = ConfMan.getBool("dreamforge_layers");

	// Test hook: dreamforge_shot_every=<ms> saves the screen into dreamforge_shot_dir as it changes
	if (ConfMan.hasKey("dreamforge_shot_every")) {
		_shotEvery = ConfMan.getInt("dreamforge_shot_every");
		_shotDir = ConfMan.get("dreamforge_shot_dir");
	}

	// Test hook: dreamforge_use=<hotspot id,...> clicks hotspots by id, in order
	Common::StringTokenizer uses(ConfMan.get("dreamforge_use"), ",");
	while (!uses.empty() && !shouldQuit()) {
		const Record *h = findHotspot(atoi(uses.nextToken().c_str()));
		if (h)
			useHotspot(*h);
	}

	// Test hook: dreamforge_cursor_probe=<x:y,x:y,...> logs the cursor at screen points
	Common::StringTokenizer probes(ConfMan.get("dreamforge_cursor_probe"), ",");
	while (!probes.empty()) {
		int px = 0, py = 0;
		if (sscanf(probes.nextToken().c_str(), "%d:%d", &px, &py) == 2)
			debug("cursor at (%d, %d): %d", px, py, cursorFor(Common::Point(px, py)));
	}

	if (ConfMan.getBool("dreamforge_quit"))
		return Common::kNoError;

	// Test hook: dreamforge_walk=<y,x> sends puppy 1 there; dreamforge_run_ms=<ms> quits the main loop after that long
	if (ConfMan.hasKey("dreamforge_walk")) {
		int wy = 0, wx = 0;
		if (sscanf(ConfMan.get("dreamforge_walk").c_str(), "%d,%d", &wy, &wx) == 2)
			_actors[0]->walkTo(wy, wx);
	}
	if (ConfMan.hasKey("dreamforge_run_ms")) {
		_quitAt = g_system->getMillis() + ConfMan.getInt("dreamforge_run_ms");
		mainLoop();
		if (!ConfMan.get("dreamforge_shot").empty())
			saveShot(ConfMan.get("dreamforge_shot"));
		return Common::kNoError;
	}

	Common::String shot = ConfMan.get("dreamforge_shot");
	if (!shot.empty()) {
		saveShot(shot);
		return Common::kNoError;
	}

	mainLoop();
	return Common::kNoError;
}

} // End of namespace DreamForge
