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

#include "bigtuna/bigtuna.h"
#include "bigtuna/console.h"
#include "bigtuna/movie.h"
#include "bigtuna/score.h"

namespace BigTuna {

static const char *const typeNames[] = {
	"empty", "script", "symbols", "castnames", "score", "palette", "bitmap", "sound", "type8", "labels"
};

Console::Console(BigTunaEngine *vm) : GUI::Debugger(), _vm(vm) {
	registerCmd("movie", WRAP_METHOD(Console, cmdMovie));
	registerCmd("res", WRAP_METHOD(Console, cmdResources));
	registerCmd("labels", WRAP_METHOD(Console, cmdLabels));
	registerCmd("frame", WRAP_METHOD(Console, cmdFrame));
	registerCmd("sprites", WRAP_METHOD(Console, cmdSprites));
}

bool Console::cmdMovie(int argc, const char **argv) {
	if (argc < 2) {
		debugPrintf("Current movie: %s\n", _vm->getMovie()->getName().c_str());
		debugPrintf("Usage: %s <movie> [label]\n", argv[0]);
		return true;
	}
	_vm->requestMovie(argv[1], argc > 2 ? Value::string(argv[2]) : Value());
	return false;
}

bool Console::cmdResources(int argc, const char **argv) {
	Movie *movie = _vm->getMovie();
	for (uint i = 1; i < movie->getResourceCount(); i++) {
		ResourceType type = movie->getType(i);
		if (type == kResEmpty)
			continue;
		if (argc > 1 && strcmp(argv[1], type < ARRAYSIZE(typeNames) ? typeNames[type] : "?"))
			continue;
		debugPrintf("%4d %-9s %d\n", i, type < ARRAYSIZE(typeNames) ? typeNames[type] : "?", movie->getSize(i));
	}
	return true;
}

bool Console::cmdLabels(int argc, const char **argv) {
	for (auto &l : _vm->getMovie()->getLabels())
		debugPrintf("%6d %s\n", l.value, l.name.c_str());
	return true;
}

bool Console::cmdFrame(int argc, const char **argv) {
	ScorePlayer *player = _vm->getPlayer();
	if (argc < 2) {
		debugPrintf("Frame %d of %d, tempo %d, frame script %d\n", player->getFrame(),
			player->getFrameCount(), player->getTempo(), player->getFrameScript());
		return true;
	}
	if (Common::isDigit(argv[1][0]))
		player->seek(atoi(argv[1]));
	else
		player->seekLabel(argv[1]);
	return false;
}

bool Console::cmdSprites(int argc, const char **argv) {
	const Common::Array<Sprite> &sprites = _vm->getPlayer()->getSprites();
	for (uint i = 1; i < sprites.size(); i++) {
		const Sprite &s = sprites[i];
		if (!s.exists)
			continue;
		debugPrintf("%3d member %4d at (%d,%d) script %d%s%s%s\n", i, s.member, s.x, s.y, s.script,
			s.visible ? "" : " hidden", s.puppet ? " puppet" : "", s.loop ? " [film loop]" : "");
	}
	return true;
}

} // End of namespace BigTuna
