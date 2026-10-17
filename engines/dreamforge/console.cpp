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

#include "dreamforge/console.h"
#include "dreamforge/dreamforge.h"
#include "dreamforge/resource.h"
#include "dreamforge/scene.h"

namespace DreamForge {

Console::Console(DreamForgeEngine *vm) : GUI::Debugger(), _vm(vm) {
	registerCmd("scene", WRAP_METHOD(Console, cmdScene));
	registerCmd("scenes", WRAP_METHOD(Console, cmdScenes));
	registerCmd("hotspots", WRAP_METHOD(Console, cmdHotspots));
	registerCmd("layers", WRAP_METHOD(Console, cmdLayers));
	registerCmd("layer", WRAP_METHOD(Console, cmdLayer));
	registerCmd("scroll", WRAP_METHOD(Console, cmdScroll));
	registerCmd("archives", WRAP_METHOD(Console, cmdArchives));
	registerCmd("shot", WRAP_METHOD(Console, cmdShot));
}

bool Console::cmdScene(int argc, const char **argv) {
	Scene *s = _vm->getScene();
	if (argc < 2) {
		if (s)
			debugPrintf("%s (%s): %ux%u, music %d, %u hotspots, %u objects, %u layers\n",
				Scene::fileName(s->_act, s->_area, s->_room).c_str(), s->_name.c_str(), s->_width, s->_height,
				s->_music, s->_hotspots.size(), s->_objects.size(), s->_layers.size());
		debugPrintf("Usage: %s <1X01X06> | %s <act> <area> <room>\n", argv[0], argv[0]);
		return true;
	}
	int act, area, room;
	bool ok = argc >= 4 ? (act = atoi(argv[1]), area = atoi(argv[2]), room = atoi(argv[3]), true)
	                    : DreamForgeEngine::parseSceneName(argv[1], act, area, room);
	if (!ok || !_vm->loadScene(act, area, room)) {
		debugPrintf("Can't load that scene\n");
		return true;
	}
	return cmdExit(0, nullptr);
}

bool Console::cmdScenes(int argc, const char **argv) {
	Common::Array<Common::String> scenes = _vm->listScenes();
	for (auto &n : scenes)
		debugPrintf("%s\n", n.c_str());
	return true;
}

bool Console::cmdHotspots(int argc, const char **argv) {
	Scene *s = _vm->getScene();
	if (!s)
		return true;
	for (uint i = 0; i < s->_hotspots.size(); i++)
		debugPrintf("%2u  %s\n", i, s->_hotspots[i].name.c_str());
	return true;
}

bool Console::cmdLayers(int argc, const char **argv) {
	Scene *s = _vm->getScene();
	if (!s)
		return true;
	for (uint i = 0; i < s->_layers.size(); i++) {
		const Layer *l = s->_layers[i];
		debugPrintf("%3u %c x %4d y %4d  %3dx%-3d  %s\n", i, _vm->_layerVisible[i] ? '*' : ' ',
			l->x, l->y, l->surface.w, l->surface.h, l->name.c_str());
	}
	debugPrintf("Layers are %s (key L toggles)\n", _vm->_showLayers ? "shown" : "hidden");
	return true;
}

bool Console::cmdLayer(int argc, const char **argv) {
	Scene *s = _vm->getScene();
	if (!s || argc < 2) {
		debugPrintf("Usage: %s <n|all|none>\n", argv[0]);
		return true;
	}
	Common::String a = argv[1];
	for (uint i = 0; i < _vm->_layerVisible.size(); i++) {
		if (a == "all")
			_vm->_layerVisible[i] = true;
		else if (a == "none")
			_vm->_layerVisible[i] = false;
		else if ((uint)atoi(argv[1]) == i)
			_vm->_layerVisible[i] = !_vm->_layerVisible[i];
	}
	_vm->_showLayers = true;
	return cmdExit(0, nullptr);
}

bool Console::cmdScroll(int argc, const char **argv) {
	if (argc >= 2)
		_vm->setScroll(atoi(argv[1]));
	debugPrintf("scroll %d\n", _vm->getScroll());
	return true;
}

bool Console::cmdArchives(int argc, const char **argv) {
	for (uint i = 0; i < kArchiveCount; i++) {
		ResourceArchive *a = _vm->getArchive(i);
		if (a)
			debugPrintf("%s: %u entries\n", a->getPath().toString().c_str(), a->size());
	}
	return true;
}

bool Console::cmdShot(int argc, const char **argv) {
	if (argc < 2) {
		debugPrintf("Usage: %s <file.png>\n", argv[0]);
		return true;
	}
	debugPrintf(_vm->saveShot(argv[1]) ? "saved\n" : "failed\n");
	return true;
}

} // End of namespace DreamForge
