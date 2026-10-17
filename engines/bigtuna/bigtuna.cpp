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
#include "common/config-manager.h"
#include "common/debug.h"
#include "common/events.h"
#include "common/file.h"
#include "common/system.h"
#include "common/tokenizer.h"
#include "engines/util.h"
#include "graphics/paletteman.h"
#include "image/png.h"

#include "bigtuna/bigtuna.h"
#include "bigtuna/bitmap.h"
#include "bigtuna/console.h"
#include "bigtuna/detection.h"
#include "bigtuna/movie.h"
#include "bigtuna/score.h"
#include "bigtuna/sound.h"
#include "bigtuna/stage.h"

namespace BigTuna {

BigTunaEngine::BigTunaEngine(OSystem *syst, const ADGameDescription *gameDesc) :
	Engine(syst), _gameDescription(gameDesc), _sound(nullptr), _movie(nullptr),
	_player(nullptr), _stage(nullptr), _jumpPending(false), _stopAfter(0),
	_framesPlayed(0), _fast(false) {
	memset(_palette, 0, sizeof(_palette));
}

BigTunaEngine::~BigTunaEngine() {
	delete _player;
	delete _movie;
	delete _stage;
	delete _sound;
}

bool BigTunaEngine::hasFeature(EngineFeature f) const {
	return f == kSupportsReturnToLauncher;
}

bool BigTunaEngine::isV1() const {
	return _gameDescription->flags & GF_IM2_V1;
}

bool BigTunaEngine::loadMovie(const Common::String &name) {
	Common::String base = name;
	int dot = base.findLastOf('.');
	if (dot >= 0)
		base = base.substr(0, dot);

	Movie *movie = new Movie(isV1());
	if (!movie->open(base)) {
		warning("Cannot open movie %s", base.c_str());
		delete movie;
		return false;
	}
	Score *score = movie->getScore(kResNumScore);
	if (!score) {
		warning("Movie %s has no score", base.c_str());
		delete movie;
		return false;
	}

	_sound->stopAll();
	delete _player;
	delete _movie;
	_movie = movie;
	_player = new ScorePlayer(this, _movie, score, false);
	if (_movie->hasPalette())
		setPalette(_movie, 0);
	debugC(1, kDebugLoading, "Loaded movie %s: %d frames", base.c_str(), score->getFrameCount());
	return true;
}

void BigTunaEngine::setPalette(Movie *movie, uint res) {
	if (res == 0)
		memcpy(_palette, movie->getPalette(), sizeof(_palette));
	else if (!movie->loadPalette(res, _palette))
		return;
	g_system->getPaletteManager()->setPalette(_palette, 0, 256);
}

void BigTunaEngine::requestJump(const Common::String &movie, const Common::String &label) {
	_jumpPending = true;
	_jumpMovie = movie;
	_jumpLabel = label;
}

void BigTunaEngine::handleJump() {
	_jumpPending = false;
	if (!_jumpMovie.empty() && !loadMovie(_jumpMovie))
		return;
	if (_jumpLabel.empty())
		_player->gotoFrame(1);
	else
		_player->gotoLabel(_jumpLabel);
	redraw();
	checkShot();
}

void BigTunaEngine::redraw() {
	_stage->render(*_player);
	const Graphics::Surface &surf = _stage->getSurface();
	g_system->copyRectToScreen(surf.getPixels(), surf.pitch, 0, 0, surf.w, surf.h);
	g_system->updateScreen();
}

bool BigTunaEngine::saveShot(const Common::String &path) {
	Common::DumpFile out;
	if (!out.open(Common::Path(path, '/'), true))
		return false;
	return Image::writePNG(out, _stage->getSurface(), _palette);
}

void BigTunaEngine::checkShot() {
	if (_shots.empty())
		return;
	for (uint frame : _shots) {
		if (frame == _player->getFrame()) {
			Common::String path = Common::String::format("%s/%s-%04d.png", _shotDir.c_str(),
				_movie->getName().c_str(), frame);
			if (!saveShot(path))
				warning("Cannot write %s", path.c_str());
		}
	}
}

// Steps every frame of every movie's main score and decodes every bitmap and score
void BigTunaEngine::sweepMovies() {
	Common::ArchiveMemberList files;
	SearchMan.listMatchingMembers(files, "*.im2");
	Common::StringArray names;
	for (auto &f : files)
		names.push_back(f->getFileName());
	Common::sort(names.begin(), names.end());

	uint bad = 0;
	for (auto &name : names) {
		if (shouldQuit())
			break;
		if (!loadMovie(name)) {
			bad++;
			continue;
		}
		uint bitmaps = 0, scores = 0, failed = 0;
		for (uint res = 1; res < _movie->getResourceCount(); res++) {
			if (_movie->getType(res) == kResBitmap) {
				bitmaps++;
				if (!_movie->getBitmap(res))
					failed++;
			} else if (_movie->getType(res) == kResScore) {
				scores++;
				if (!_movie->getScore(res))
					failed++;
			}
		}
		uint frames = 0;
		while (_player->getFrame() < _player->getFrameCount()) {
			_player->advance();
			_sound->stopAll();
			_jumpPending = false;
			_stage->render(*_player);
			frames++;
		}
		redraw();
		debug("sweep %s: %d frames, %d bitmaps, %d scores, %d failed", _movie->getName().c_str(),
			frames, bitmaps, scores, failed);
		if (failed)
			bad++;
		Common::Event event;
		while (g_system->getEventManager()->pollEvent(event)) {
		}
	}
	debug("sweep done: %d movies, %d with problems", names.size(), bad);
}

Common::Error BigTunaEngine::run() {
	initGraphics(kStageWidth, kStageHeight);
	setDebugger(new Console(this));
	_sound = new Sound(_mixer);
	_stage = new Stage();

	if (ConfMan.hasKey("bigtuna_frames"))
		_stopAfter = ConfMan.getInt("bigtuna_frames");
	_fast = ConfMan.getBool("bigtuna_fast");
	_shotDir = ConfMan.get("bigtuna_shot_dir");
	Common::StringTokenizer shotList(ConfMan.get("bigtuna_shots"), ",");
	while (!shotList.empty()) {
		Common::String s = shotList.nextToken();
		if (!s.empty())
			_shots.push_back(atoi(s.c_str()));
	}

	if (ConfMan.getBool("bigtuna_allmovies")) {
		sweepMovies();
		return Common::kNoError;
	}

	Common::String start = ConfMan.get("bigtuna_movie");
	if (start.empty())
		start = "intro";
	if (!loadMovie(start))
		return Common::kNoGameDataFoundError;

	uint32 nextFrame = 0;
	bool ended = false;
	while (!shouldQuit()) {
		Common::Event event;
		while (g_system->getEventManager()->pollEvent(event)) {
			if (event.type == Common::EVENT_KEYDOWN && event.kbd.keycode == Common::KEYCODE_d &&
				(event.kbd.flags & Common::KBD_CTRL))
				getDebugger()->attach();
		}

		uint32 now = g_system->getMillis();
		_sound->update(now);
		if (_fast || now >= nextFrame) {
			nextFrame = now + 1000 / MAX<uint>(_player->getTempo(), 1);
			if (_jumpPending) {
				handleJump();
			} else if (!ended) {
				uint before = _player->getFrame();
				ended = !_player->advance();
				if (!ended && _player->getFrame() != before) {
					_framesPlayed++;
					redraw();
					checkShot();
				}
			}
			if (_stopAfter && (_framesPlayed >= _stopAfter || (ended && _fast)))
				break;
		}
		if (!_fast)
			g_system->delayMillis(5);
	}
	return Common::kNoError;
}

} // End of namespace BigTuna
