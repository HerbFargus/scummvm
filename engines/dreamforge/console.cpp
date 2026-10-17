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
#include "dreamforge/lipsync.h"
#include "dreamforge/resource.h"
#include "dreamforge/scene.h"
#include "dreamforge/sound.h"

namespace DreamForge {

Console::Console(DreamForgeEngine *vm) : GUI::Debugger(), _vm(vm), _cine(-1), _use(-1) {
	_say[0] = -1;
	registerCmd("scene", WRAP_METHOD(Console, cmdScene));
	registerCmd("scenes", WRAP_METHOD(Console, cmdScenes));
	registerCmd("hotspots", WRAP_METHOD(Console, cmdHotspots));
	registerCmd("layers", WRAP_METHOD(Console, cmdLayers));
	registerCmd("layer", WRAP_METHOD(Console, cmdLayer));
	registerCmd("scroll", WRAP_METHOD(Console, cmdScroll));
	registerCmd("archives", WRAP_METHOD(Console, cmdArchives));
	registerCmd("shot", WRAP_METHOD(Console, cmdShot));
	registerCmd("cine", WRAP_METHOD(Console, cmdCine));
	registerCmd("sfx", WRAP_METHOD(Console, cmdSfx));
	registerCmd("voice", WRAP_METHOD(Console, cmdVoice));
	registerCmd("music", WRAP_METHOD(Console, cmdMusic));
	registerCmd("say", WRAP_METHOD(Console, cmdSay));
	registerCmd("use", WRAP_METHOD(Console, cmdUse));
	registerCmd("flag", WRAP_METHOD(Console, cmdFlag));
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

bool Console::cmdCine(int argc, const char **argv) {
	if (argc < 2) {
		debugPrintf("Usage: %s <number>  (plays CINE\\NNNN.DFA)\n", argv[0]);
		return true;
	}
	_cine = atoi(argv[1]);
	return cmdExit(0, nullptr);
}

bool Console::cmdSfx(int argc, const char **argv) {
	if (argc < 2) {
		debugPrintf("Usage: %s <number> [channel]  (e.g. 23000)\n", argv[0]);
		return true;
	}
	int id = atoi(argv[1]);
	debugPrintf("effect %d is PIG1 #%d\n", id, _vm->getSound()->sfxIndex(id));
	_vm->getSound()->playSfx(id, argc > 2 ? atoi(argv[2]) : 0, false);
	return true;
}

bool Console::cmdVoice(int argc, const char **argv) {
	if (argc < 2) {
		debugPrintf("Usage: %s <number>  (e.g. 11025)\n", argv[0]);
		return true;
	}
	int id = atoi(argv[1]);
	debugPrintf("voice %d is PIG3 #%d\n", id, _vm->getSound()->voiceIndex(id));
	_vm->getSound()->playVoice(id);
	return true;
}

bool Console::cmdMusic(int argc, const char **argv) {
	if (argc < 2) {
		debugPrintf("Usage: %s <track 0-19 | stop>  (now %d)\n", argv[0], _vm->getSound()->getMusic());
		return true;
	}
	if (!strcmp(argv[1], "stop"))
		_vm->getSound()->stopMusic();
	else
		_vm->getSound()->playMusic(atoi(argv[1]), true);
	return true;
}

bool Console::cmdSay(int argc, const char **argv) {
	if (argc < 2) {
		debugPrintf("Usage: %s <voice> [actor 1|2] [slot] [view 0-4]\n", argv[0]);
		debugPrintf("Talking slots: 9 or 30 (normal), 11 or 14, 18 or 26 (the first of each pair is quick)\n");
		return true;
	}
	_say[0] = atoi(argv[1]);
	_say[1] = argc > 2 ? atoi(argv[2]) : 1;
	_say[2] = argc > 3 ? atoi(argv[3]) : talkSlot(false, false, true);
	_say[3] = argc > 4 ? atoi(argv[4]) : 0;
	return cmdExit(0, nullptr);
}

bool Console::cmdUse(int argc, const char **argv) {
	if (argc < 2) {
		debugPrintf("Usage: %s <hotspot id>  (see hotspots)\n", argv[0]);
		return true;
	}
	_use = atoi(argv[1]);
	return cmdExit(0, nullptr);
}

bool Console::cmdFlag(int argc, const char **argv) {
	if (argc < 2) {
		debugPrintf("Usage: %s <n> [0|1]\n", argv[0]);
		return true;
	}
	uint n = atoi(argv[1]);
	if (argc > 2)
		_vm->setFlag(n, atoi(argv[2]) != 0);
	debugPrintf("flag %u is %s\n", n, _vm->isFlagSet(n) ? "set" : "clear");
	return true;
}

void Console::postEnter() {
	if (_cine >= 0) {
		_vm->playCutscene(_cine);
		_cine = -1;
	}
	if (_use >= 0) {
		const Record *h = _vm->findHotspot(_use);
		if (h)
			_vm->useHotspot(*h);
		_use = -1;
	}
	if (_say[0] >= 0) {
		_vm->sayDemo(_say[0], _say[1], _say[2], _say[3]);
		_say[0] = -1;
	}
}

} // End of namespace DreamForge
