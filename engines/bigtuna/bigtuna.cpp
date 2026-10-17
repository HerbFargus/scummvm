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
#include "common/savefile.h"
#include "common/system.h"
#include "common/tokenizer.h"
#include "engines/util.h"
#include "graphics/cursorman.h"
#include "graphics/paletteman.h"
#include "image/png.h"

#include "bigtuna/bigtuna.h"
#include "bigtuna/bitmap.h"
#include "bigtuna/console.h"
#include "bigtuna/detection.h"
#include "bigtuna/imp.h"
#include "bigtuna/movie.h"
#include "bigtuna/score.h"
#include "bigtuna/script.h"
#include "bigtuna/sound.h"
#include "bigtuna/stage.h"

namespace BigTuna {

enum {
	kCursorBlank = 200,
	kCursorMember = -100
};

BigTunaEngine::BigTunaEngine(OSystem *syst, const ADGameDescription *gameDesc) :
	Engine(syst), _gameDescription(gameDesc), _sound(nullptr), _script(nullptr),
	_rnd("bigtuna"), _mouseDown(false), _doubleClick(false), _keyChar(0), _clickOn(0),
	_timerStart(0), _movie(nullptr), _player(nullptr), _stage(nullptr), _cursorId(-1),
	_movieRequested(false), _clicked(false), _moved(false), _lastClickTime(0), _hoverChannel(0),
	_startTime(0), _fast(false), _stopAfter(0), _framesPlayed(0), _fuzzNext(0), _fuzz(false),
	_quitAt(0), _imp(nullptr) {
	memset(_palette, 0, sizeof(_palette));
}

BigTunaEngine::~BigTunaEngine() {
	delete _imp;
	delete _player;
	delete _movie;
	delete _stage;
	delete _script;
	delete _sound;
}

bool BigTunaEngine::hasFeature(EngineFeature f) const {
	return f == kSupportsReturnToLauncher;
}

bool BigTunaEngine::isImp() const {
	return _gameDescription->flags & GF_IMP;
}

void BigTunaEngine::setPaletteData(const byte *pal) {
	memcpy(_palette, pal, sizeof(_palette));
	g_system->getPaletteManager()->setPalette(_palette, 0, 256);
	if (_cursorId != kCursorMember)
		setCursor(_cursorId);
}

void BigTunaEngine::presentStage() {
	const Graphics::Surface &surf = _stage->getSurface();
	g_system->copyRectToScreen(surf.getPixels(), surf.pitch, 0, 0, surf.w, surf.h);
	g_system->updateScreen();
}

bool BigTunaEngine::takeClick(Common::Point &p) {
	if (!_clicked)
		return false;
	_clicked = _moved = false;
	p = _mousePos;
	return true;
}

byte BigTunaEngine::takeKey() {
	byte k = _keyChar;
	_keyChar = 0;
	return k;
}

void BigTunaEngine::frameDone() {
	_framesPlayed++;
	if (_stopAfter && _framesPlayed >= _stopAfter)
		quitGame();
}

bool BigTunaEngine::isV1() const {
	return _gameDescription->flags & GF_IM2_V1;
}

void BigTunaEngine::setPalette(Movie *movie, uint res) {
	if (res == 0)
		memcpy(_palette, movie->getPalette(), sizeof(_palette));
	else if (!movie->loadPalette(res, _palette))
		return;
	g_system->getPaletteManager()->setPalette(_palette, 0, 256);
	if (_cursorId != kCursorMember)
		setCursor(_cursorId);
}

void BigTunaEngine::requestMovie(const Common::String &name, const Value &frame) {
	_movieRequested = true;
	_requestName = name;
	_requestFrame = frame;
	if (_script->isRunning())
		_script->stop();
}

// FUN_00402490 / FUN_00402d90: everything stops, startmovie runs on the first
// frame's sprites, then the score starts over and goes to the requested frame
bool BigTunaEngine::loadMovie(const Common::String &name, const Value &frame) {
	Common::String base = name;
	int dot = base.findLastOf('.');
	if (dot >= 0)
		base = base.substr(0, dot);

	Movie *movie = new Movie(isV1());
	Score *score = nullptr;
	if (!movie->open(base) || !(score = movie->getScore(kResNumScore))) {
		warning("Cannot open movie %s", base.c_str());
		delete movie;
		return false;
	}

	_sound->reset();
	delete _player;
	delete _movie;
	_movie = movie;
	_player = new ScorePlayer(this, _movie, score, false);
	_hoverChannel = 0;
	_clickOn = 0;
	if (_movie->hasPalette())
		setPalette(_movie, 0);
	debugC(1, kDebugLoading, "Loaded movie %s: %d frames", base.c_str(), score->getFrameCount());

	_player->step();
	_script->runMovieHandler("startmovie");
	_player->rewind();
	_sound->reset();

	if (frame.isInt())
		_player->seek(frame.i);
	else if (frame.isString())
		_player->seekLabel(frame.obj->str);
	return true;
}

void BigTunaEngine::waitForSound(int channel) {
	while (_sound->isBusy(channel) && !shouldQuit()) {
		pollEvents();
		g_system->updateScreen();
		g_system->delayMillis(10);
	}
}

void BigTunaEngine::redraw() {
	_stage->render(*_player);
	const Graphics::Surface &surf = _stage->getSurface();
	g_system->copyRectToScreen(surf.getPixels(), surf.pitch, 0, 0, surf.w, surf.h);
	g_system->updateScreen();
}

void BigTunaEngine::updateStage() {
	redraw();
	pollEvents();
}

bool BigTunaEngine::saveShot(const Common::String &path) {
	Common::DumpFile out;
	if (!out.open(Common::Path(path, '/'), true))
		return false;
	return Image::writePNG(out, _stage->getSurface(), _palette);
}

static byte nearestColor(const byte *pal, byte r, byte g, byte b) {
	int best = 0, bestDist = 0x7FFFFFFF;
	for (int i = 0; i < 256; i++) {
		int dr = pal[i * 3] - r, dg = pal[i * 3 + 1] - g, db = pal[i * 3 + 2] - b;
		int d = dr * dr + dg * dg + db * db;
		if (d < bestDist) {
			bestDist = d;
			best = i;
		}
	}
	return best;
}

// 200 hides the cursor (and with it all mouse events); 2 and 4 are the wait
// cursor; anything else is the arrow
void BigTunaEngine::setCursor(int id) {
	_cursorId = id;
	if (id == kCursorBlank) {
		CursorMan.showMouse(false);
		return;
	}
	static const char *const arrow[] = {
		"X           ", "XX          ", "X.X         ", "X..X        ", "X...X       ",
		"X....X      ", "X.....X     ", "X......X    ", "X.......X   ", "X........X  ",
		"X.....XXXXX ", "X..X..X     ", "X.X X..X    ", "XX  X..X    ", "X    X..X   ",
		"     X..X   ", "      XX    "
	};
	const int w = 12, h = 17;
	byte black = nearestColor(_palette, 0, 0, 0);
	byte white = nearestColor(_palette, 255, 255, 255);
	byte key = (black != 1 && white != 1) ? 1 : 2;
	byte pixels[w * h];
	for (int y = 0; y < h; y++) {
		for (int x = 0; x < w; x++) {
			char c = arrow[y][x];
			pixels[y * w + x] = c == 'X' ? black : c == '.' ? white : key;
		}
	}
	CursorMan.replaceCursor(pixels, w, h, 0, 0, key);
	CursorMan.showMouse(true);
}

// FUN_00401ee0: a cast bitmap as the cursor, white transparent, hotspot at the reg point
void BigTunaEngine::setCursorMember(uint res) {
	Bitmap *bitmap = _movie->getBitmap(res);
	if (!bitmap || bitmap->getSurface().format.bytesPerPixel != 1) {
		warning("Cursor resource %d is not a bitmap", res);
		return;
	}
	const Graphics::Surface &src = bitmap->getSurface();
	byte white = nearestColor(_palette, 255, 255, 255);
	const byte *mask = bitmap->getMask();
	Common::Array<byte> pixels(src.w * src.h);
	for (int y = 0; y < src.h; y++) {
		const byte *row = (const byte *)src.getBasePtr(0, y);
		for (int x = 0; x < src.w; x++) {
			byte c = row[x];
			if (mask && !mask[y * src.w + x])
				c = white;
			pixels[y * src.w + x] = c;
		}
	}
	CursorMan.replaceCursor(&pixels[0], src.w, src.h, MAX<int>(bitmap->getRegX(), 0),
		MAX<int>(bitmap->getRegY(), 0), white);
	CursorMan.showMouse(true);
	_cursorId = kCursorMember;
}

const Sprite *BigTunaEngine::spriteAt(int x, int y, bool needScript, uint *channel) const {
	const Common::Array<Sprite> &sprites = _player->getSprites();
	for (int i = (int)sprites.size() - 1; i >= 1; i--) {
		const Sprite &s = sprites[i];
		if (!s.exists || (needScript && !s.script))
			continue;
		if (_player->hitTest(s, x, y)) {
			if (channel)
				*channel = i;
			return &s;
		}
	}
	return nullptr;
}

// Op 32 (FUN_00406fb0): the rects overlap and both sprites cover the overlap's centre
bool BigTunaEngine::spritesIntersect(int a, int b) {
	Sprite &sa = _player->getSprite(MAX(a, 0));
	Common::Rect ra = _player->getSpriteRect(sa);
	Sprite &sb = _player->getSprite(MAX(b, 0));
	Common::Rect rb = _player->getSpriteRect(sb);
	Sprite &s1 = _player->getSprite(MAX(a, 0));
	if (!ra.intersects(rb))
		return false;
	Common::Rect o = ra.findIntersectingRect(rb);
	int cx = (o.left + o.right) / 2, cy = (o.top + o.bottom) / 2;
	if (!s1.loop && !_player->hitTest(s1, cx, cy))
		return false;
	return sb.loop || _player->hitTest(sb, cx, cy);
}

Value BigTunaEngine::getPref(const Common::String &name) {
	Common::String file = _targetName + "-" + name;
	Common::InSaveFile *in = _saveFileMan->openForLoading(file);
	if (!in)
		return Value();
	Common::String text = in->readString(0, 0x3FFF);
	delete in;
	return Value::string(text);
}

void BigTunaEngine::setPref(const Common::String &name, const Common::String &text) {
	Common::String file = _targetName + "-" + name;
	Common::OutSaveFile *out = _saveFileMan->openForSaving(file, false);
	if (!out) {
		warning("Cannot open file for writing: %s", file.c_str());
		return;
	}
	out->writeString(text);
	out->finalize();
	delete out;
}

void BigTunaEngine::pollEvents() {
	Common::Event event;
	while (g_system->getEventManager()->pollEvent(event)) {
		switch (event.type) {
		case Common::EVENT_MOUSEMOVE:
			if (event.mouse != _mousePos) {
				_mousePos = event.mouse;
				_moved = true;
			}
			break;
		case Common::EVENT_LBUTTONDOWN: {
			uint32 now = g_system->getMillis();
			_mousePos = event.mouse;
			_doubleClick = now - _lastClickTime < 500;
			_lastClickTime = now;
			_mouseDown = true;
			_clicked = true;
			break;
		}
		case Common::EVENT_LBUTTONUP:
			_mouseDown = false;
			break;
		case Common::EVENT_KEYDOWN:
			if (event.kbd.keycode == Common::KEYCODE_d && (event.kbd.flags & Common::KBD_CTRL))
				getDebugger()->attach();
			else if (event.kbd.ascii > 0 && event.kbd.ascii < 256)
				_keyChar = event.kbd.ascii;
			break;
		default:
			break;
		}
	}
	checkTestHooks();
}

// The mouse part of FUN_00403b90
void BigTunaEngine::dispatchMouse() {
	bool clicked = _clicked, moved = _moved;
	_clicked = _moved = false;
	if (!(clicked || moved))
		return;

	uint channel = 0;
	if (_cursorId != kCursorBlank && spriteAt(_mousePos.x, _mousePos.y, true, &channel)) {
		_clickOn = channel;
		Sprite &s = _player->getSprite(channel);
		uint script = s.script;
		const char *event = s.mouseInside ? "mousewithin" : "mouseenter";
		s.mouseInside = true;
		_script->runEvent(event, script);
		if (clicked && !_movieRequested) {
			if (!_script->runEvent("mousedown", script))
				_script->runEvent("mouseup", script);
		}
	}
	if (_hoverChannel != channel) {
		Sprite *old = _player->findSprite(_hoverChannel);
		if (old) {
			if (old->script && !_movieRequested)
				_script->runEvent("mouseleave", old->script);
			old = _player->findSprite(_hoverChannel);
			if (old)
				old->mouseInside = false;
		}
		_hoverChannel = channel;
	}
}

// FUN_00403b90: one score step with its events
void BigTunaEngine::runStep(uint frames) {
	_clickOn = 0;
	uint seekCount = _player->getSeekCount();
	if (_player->getFrameScript())
		_script->runEvent("enterframe", _player->getFrameScript());
	if (_movieRequested)
		return;
	redraw();
	checkFrameShot();

	uint32 duration = frames * 1000 / MAX<uint>(_player->getTempo(), 1);
	uint32 start = g_system->getMillis();
	do {
		pollEvents();
		if (_keyChar) {
			_script->runMovieHandler("keydown");
			_keyChar = 0;
		}
		if (!_movieRequested) {
			bool wasClick = _clicked;
			dispatchMouse();
			if (wasClick && _player->getSeekCount() != seekCount)
				return;
		}
		if (_movieRequested || shouldQuit())
			return;
		_sound->update(g_system->getMillis());
		if (!_fast)
			g_system->delayMillis(5);
	} while (!_fast && g_system->getMillis() - start < duration);

	if (_player->getFrameScript())
		_script->runEvent("exitframe", _player->getFrameScript());
}

// FUN_00402ed0
void BigTunaEngine::mainLoop() {
	while (!shouldQuit()) {
		if (_movieRequested) {
			_movieRequested = false;
			if (!loadMovie(_requestName, _requestFrame))
				break;
			continue;
		}
		uint frames = _player->step();
		if (!frames) {
			debug("Score of %s ended: the player quits here", _movie->getName().c_str());
			break;
		}
		if (_movieRequested)
			continue;
		if (_player->hasLoops()) {
			uint seekCount = _player->getSeekCount();
			for (uint i = 0; i < frames && !_movieRequested && _player->getSeekCount() == seekCount; i++) {
				_player->stepLoops();
				runStep(1);
			}
		} else {
			runStep(frames);
		}
		_framesPlayed++;
		if (_stopAfter && _framesPlayed >= _stopAfter)
			break;
	}
}

void BigTunaEngine::checkFrameShot() {
	for (uint frame : _shots) {
		if (frame == _player->getFrame()) {
			Common::String path = Common::String::format("%s/%s-%04d.png", _shotDir.c_str(),
				_movie->getName().c_str(), frame);
			if (!saveShot(path))
				warning("Cannot write %s", path.c_str());
		}
	}
}

// Config keys for unattended tests:
//   bigtuna_actions=ms:c:x:y,ms:k:<char code>,ms:s  clicks, keys and shots at times from start
//   bigtuna_fuzz=1  random clicks on scripted sprites; bigtuna_quit=ms
void BigTunaEngine::readTestHooks() {
	_fast = ConfMan.getBool("bigtuna_fast");
	if (ConfMan.hasKey("bigtuna_frames"))
		_stopAfter = ConfMan.getInt("bigtuna_frames");
	_shotDir = ConfMan.get("bigtuna_shot_dir");
	Common::StringTokenizer shots(ConfMan.get("bigtuna_shots"), ",");
	while (!shots.empty()) {
		Common::String s = shots.nextToken();
		if (!s.empty())
			_shots.push_back(atoi(s.c_str()));
	}
	Common::StringTokenizer actions(ConfMan.get("bigtuna_actions"), ",");
	while (!actions.empty()) {
		Common::StringTokenizer parts(actions.nextToken(), ":");
		TimedAction a;
		a.time = atoi(parts.nextToken().c_str());
		Common::String type = parts.nextToken();
		a.type = type.empty() ? 's' : type[0];
		a.x = atoi(parts.nextToken().c_str());
		a.y = atoi(parts.nextToken().c_str());
		_actions.push_back(a);
	}
	_fuzz = ConfMan.getBool("bigtuna_fuzz");
	if (ConfMan.hasKey("bigtuna_quit"))
		_quitAt = ConfMan.getInt("bigtuna_quit");
	if (ConfMan.hasKey("bigtuna_seed"))
		_rnd.setSeed(ConfMan.getInt("bigtuna_seed"));
	_script->_trace = ConfMan.getBool("bigtuna_trace");
	_startTime = g_system->getMillis();
}

Common::String BigTunaEngine::where() const {
	if (_imp)
		return Common::String::format("%s record %d", _imp->getFilmName().c_str(), _imp->getPosition());
	if (_movie)
		return Common::String::format("%s frame %d", _movie->getName().c_str(), _player->getFrame());
	return "?";
}

Common::Array<Common::Point> BigTunaEngine::fuzzTargets() const {
	if (_imp)
		return _imp->getButtonPoints();
	Common::Array<Common::Point> targets;
	if (!_player)
		return targets;
	const Common::Array<Sprite> &sprites = _player->getSprites();
	for (uint i = 1; i < sprites.size(); i++) {
		if (sprites[i].exists && sprites[i].visible && sprites[i].script && sprites[i].member) {
			Common::Rect r = _player->getSpriteRect(sprites[i]);
			targets.push_back(Common::Point((r.left + r.right) / 2, (r.top + r.bottom) / 2));
		}
	}
	return targets;
}

void BigTunaEngine::checkTestHooks() {
	uint32 t = g_system->getMillis() - _startTime;
	for (uint i = 0; i < _actions.size();) {
		TimedAction a = _actions[i];
		if (a.time > t) {
			i++;
			continue;
		}
		_actions.remove_at(i);
		if (a.type == 'c') {
			_mousePos = Common::Point(a.x, a.y);
			g_system->warpMouse(a.x, a.y);
			_clicked = _moved = true;
			debug("test: click %d,%d at %d ms in %s", a.x, a.y, t, where().c_str());
		} else if (a.type == 'k') {
			_keyChar = a.x;
		} else if (_movie || _imp) {
			Common::String name = where();
			for (uint n = 0; n < name.size(); n++) {
				if (name[n] == ' ')
					name.setChar('-', n);
			}
			saveShot(Common::String::format("%s/t%06d-%s.png", _shotDir.c_str(), a.time, name.c_str()));
		}
	}
	if (_fuzz && (_movie || _imp) && t >= _fuzzNext) {
		_fuzzNext = t + 300 + _rnd.getRandomNumber(700);
		Common::Array<Common::Point> targets = fuzzTargets();
		if (_rnd.getRandomNumber(6) == 0) {
			_keyChar = _imp ? ' ' : 27;
			debug("fuzz: key %d in %s", _keyChar, where().c_str());
			return;
		}
		Common::Point p(_rnd.getRandomNumber(639), _rnd.getRandomNumber(479));
		if (!targets.empty() && _rnd.getRandomNumber(9) < 8)
			p = targets[_rnd.getRandomNumber(targets.size() - 1)];
		_mousePos = p;
		_clicked = _moved = true;
		debug("fuzz: click %d,%d in %s", p.x, p.y, where().c_str());
	}
	if (_quitAt && t >= _quitAt)
		quitGame();
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
		Movie *movie = new Movie(isV1());
		Score *score = nullptr;
		if (!movie->open(name.substr(0, name.size() - 4)) || !(score = movie->getScore(kResNumScore))) {
			delete movie;
			bad++;
			continue;
		}
		delete _player;
		delete _movie;
		_movie = movie;
		_player = new ScorePlayer(this, _movie, score, false);
		if (_movie->hasPalette())
			setPalette(_movie, 0);

		uint bitmaps = 0, scores = 0, scripts = 0, failed = 0;
		for (uint res = 1; res < _movie->getResourceCount(); res++) {
			ResourceType type = _movie->getType(res);
			if (type == kResBitmap) {
				bitmaps++;
				failed += !_movie->getBitmap(res);
			} else if (type == kResScore) {
				scores++;
				failed += !_movie->getScore(res);
			} else if (type == kResScript) {
				scripts++;
				failed += !_movie->getScriptData(res);
			}
		}
		uint steps = 0;
		while (_player->step()) {
			_sound->stopAll();
			_movieRequested = false;
			_stage->render(*_player);
			steps++;
		}
		redraw();
		debug("sweep %s: %d steps, %d bitmaps, %d scores, %d scripts, %d failed", _movie->getName().c_str(),
			steps, bitmaps, scores, scripts, failed);
		bad += failed != 0;
		pollEvents();
	}
	debug("sweep done: %d movies, %d with problems", names.size(), bad);
}

Common::Error BigTunaEngine::run() {
	initGraphics(kStageWidth, kStageHeight);
	setDebugger(new Console(this));
	_sound = new Sound(_mixer);
	_stage = new Stage();
	_script = new Script(this);
	readTestHooks();
	_timerStart = g_system->getMillis();

	if (isImp()) {
		_imp = new ImpPlayer(this);
		if (ConfMan.getBool("bigtuna_allmovies")) {
			_imp->sweep();
			return Common::kNoError;
		}
		Common::String start = ConfMan.get("bigtuna_movie");
		_imp->run(start.empty() ? "intro" : start);
		return Common::kNoError;
	}

	if (ConfMan.getBool("bigtuna_allmovies")) {
		sweepMovies();
		return Common::kNoError;
	}

	Common::String start = ConfMan.get("bigtuna_movie");
	if (start.empty())
		start = "intro";
	if (!loadMovie(start, Value()))
		return Common::kNoGameDataFoundError;
	setCursor(-1);
	mainLoop();
	return Common::kNoError;
}

} // End of namespace BigTuna
