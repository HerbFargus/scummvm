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

#include "audio/audiostream.h"
#include "audio/decoders/wave.h"
#include "common/compression/installshieldv3_archive.h"
#include "common/config-manager.h"
#include "common/debug.h"
#include "common/events.h"
#include "common/file.h"
#include "common/fs.h"
#include "common/md5.h"
#include "common/memstream.h"
#include "common/savefile.h"
#include "common/system.h"
#include "common/textconsole.h"
#include "engines/util.h"
#include "graphics/paletteman.h"
#include "image/png.h"
#include "video/smk_decoder.h"

#include "nightmarened/attic.h"
#include "nightmarened/menus.h"
#include "nightmarened/options.h"
#include "nightmarened/demo.h"
#include "nightmarened/ned.h"
#include "nightmarened/detection.h"
#include "nightmarened/display.h"
#include "nightmarened/game.h"
#include "nightmarened/hints.h"
#include "nightmarened/naf.h"
#include "nightmarened/nightmarened.h"
#include "nightmarened/rdx.h"
#include "nightmarened/scene1.h"
#include "nightmarened/scene7.h"

namespace NightmareNed {

const int kNafNumbers[12] = { 0, 1000, 86000, 90000, 92000, 93000, 94000, 95000, 96000, 97000, 98000, 99000 };

NightmareNedEngine::NightmareNedEngine(OSystem *syst, const ADGameDescription *gameDesc) : Engine(syst),
	_gameDescription(gameDesc), _data32(nullptr), _game(nullptr), _options(nullptr), _idle(false), _display(nullptr), _naf(nullptr), _nafIndex(-1), _bgIndex(0),
	_scrollX(0), _scrollY(0), _spriteIndex(0), _frameIndex(0), _showSprite(false) {
	memset(_palette, 0, sizeof(_palette));
}

NightmareNedEngine::~NightmareNedEngine() {
	for (SpriteFrame *f : _frames)
		delete f;
	delete _naf;
	delete _options;
	delete _game;
	delete _data32;
	_view.free();
	_screen.free();
}

bool NightmareNedEngine::hasFeature(EngineFeature f) const {
	return f == kSupportsReturnToLauncher || f == kSupportsLoadingDuringRuntime || f == kSupportsSavingDuringRuntime;
}

// The ScummVM menu opens from the main loop's event poll; the loops inside a tick don't allow it
bool NightmareNedEngine::canLoadGameStateCurrently(Common::U32String *msg) {
	return _idle && _game && _options;
}

// Saving needs a scene that would let the options screen open
bool NightmareNedEngine::canSaveGameStateCurrently(Common::U32String *msg) {
	return _idle && _game && _options && _game->_optionsAllowed && !_options->isOpen();
}

// GAMEnn (0x460580): the 12-byte name, then 0x459d90's game state; ScummVM's header follows
Common::Error NightmareNedEngine::saveGameStream(Common::WriteStream *stream, bool isAutosave) {
	if (!_options)
		return Common::kWritingFailed;
	char name[Options::kNameSize];
	memset(name, 0, sizeof(name));
	Common::strlcpy(name, _saveName.c_str(), sizeof(name));
	stream->write(name, sizeof(name));
	return _options->writeGame(*stream) ? Common::kNoError : Common::kWritingFailed;
}

// The game's names are capitals and spaces, at most 11
Common::Error NightmareNedEngine::saveGameState(int slot, const Common::String &desc, bool isAutosave) {
	_saveName.clear();
	for (uint i = 0; i < desc.size() && _saveName.size() < Options::kNameSize - 1; i++)
		_saveName += (char)toupper((byte)desc[i]);
	return Engine::saveGameState(slot, desc, isAutosave);
}

bool NightmareNedEngine::saveSlot(int slot, const Common::String &name) {
	return saveGameState(slot, name, false).getCode() == Common::kNoError;
}

// As the options screen's load (0x45f3c0): restore, close the screen, then 0x45a210
Common::Error NightmareNedEngine::loadGameStream(Common::SeekableReadStream *stream) {
	if (!_options || !_game)
		return Common::kReadingFailed;
	stream->skip(Options::kNameSize);
	int scene = 0, sub = -1;
	if (!_options->readGame(*stream, scene, sub))
		return Common::kReadingFailed;
	_options->closeForLoad();
	debugC(1, kDebugLoading, "Loaded a saved game: scene %d:%d", scene, sub);
	_game->jumpToScene(scene, sub);
	return Common::kNoError;
}

void NightmareNedEngine::pumpEvents() {
	Common::Event event;
	while (g_system->getEventManager()->pollEvent(event)) {
		if (event.type == Common::EVENT_KEYDOWN && event.kbd.keycode < (int)_keysDown.size())
			_keysDown[event.kbd.keycode] = true;
		else if (event.type == Common::EVENT_KEYUP && event.kbd.keycode < (int)_keysDown.size())
			_keysDown[event.kbd.keycode] = false;
	}
}

bool NightmareNedEngine::isKeyDown(Common::KeyCode key) const {
	return key < (int)_keysDown.size() && _keysDown[key];
}

// The CD has DATA\DATA\*.NAF and DATA\VIDEO\*.SMK. NITENED.EXE, NED01000.NAF,
// NED99000.NAF, the .SST placement tables and the area music only exist inside DATA\DATA32.Z.
bool NightmareNedEngine::mountData() {
	const Common::FSNode gameDir(ConfMan.getPath("path"));
	// The detection globs already add DATA itself, without its subfolders
	SearchMan.addDirectory("nightmarened-cd", gameDir.getChild("data"), 0, 2, true);

	_data32 = new Common::InstallShieldV3();
	if (!_data32->open(Common::Path("data32.z"))) {
		delete _data32;
		_data32 = nullptr;
		GUIErrorMessage("Can't open DATA\\DATA32.Z");
		return false;
	}
	debugC(1, kDebugLoading, "Mounted DATA32.Z");
	if (!_exe.load(openFile("NITENED.EXE"))) {
		GUIErrorMessage("Can't read NITENED.EXE from DATA32.Z");
		return false;
	}
	return true;
}

Common::SeekableReadStream *NightmareNedEngine::openFile(const Common::String &name) {
	Common::Path path(name);
	if (SearchMan.hasFile(path))
		return SearchMan.createReadStreamForMember(path);
	static const char *const dirs[] = { "DATA", "VIDEO", "SOUND", "" };
	for (const char *dir : dirs) {
		Common::Path member = *dir ? Common::Path(dir).appendComponent(name) : path;
		if (_data32 && _data32->hasFile(member))
			return _data32->createReadStreamForMember(member);
	}
	return nullptr;
}

NafArchive *NightmareNedEngine::openNaf(int number) {
	Common::String name = Common::String::format("NED%05d.NAF", number);
	NafArchive *naf = new NafArchive();
	if (!naf->open(openFile(name), name)) {
		warning("Can't open %s", name.c_str());
		delete naf;
		return nullptr;
	}
	return naf;
}

bool NightmareNedEngine::playVideo(const Common::String &name) {
	Common::SeekableReadStream *stream = openFile(name);
	if (!stream) {
		warning("Can't find %s", name.c_str());
		return true;
	}
	Video::SmackerDecoder decoder;
	if (!decoder.loadStream(stream)) {
		warning("Can't play %s", name.c_str());
		return true;
	}
	debugC(1, kDebugVideo, "Playing %s (%d frames)", name.c_str(), decoder.getFrameCount());
	decoder.start();

	// Test hook: nightmarened_videoshot=<NAME.SMK>:<frame>:<png> saves that frame and quits
	Common::String shotPath;
	int shotFrame = -1;
	if (ConfMan.hasKey("nightmarened_videoshot")) {
		Common::String spec = ConfMan.get("nightmarened_videoshot");
		size_t a = spec.findFirstOf(':'), b = a == Common::String::npos ? a : spec.find(':', a + 1);
		if (b != Common::String::npos && spec.substr(0, a).equalsIgnoreCase(name)) {
			shotFrame = atoi(spec.substr(a + 1, b - a - 1).c_str());
			shotPath = spec.substr(b + 1);
		}
	}

	bool keepGoing = true, skip = false;
	while (!decoder.endOfVideo() && !shouldQuit() && !skip) {
		if (decoder.needsUpdate()) {
			const Graphics::Surface *frame = decoder.decodeNextFrame();
			if (decoder.hasDirtyPalette())
				g_system->getPaletteManager()->setPalette(decoder.getPalette(), 0, 256);
			if (frame && decoder.getCurFrame() == shotFrame) {
				Common::DumpFile out;
				if (out.open(Common::Path(shotPath, '/'), true))
					Image::writePNG(out, *frame, decoder.getPalette());
				quitGame();
			}
			if (frame) {
				int x = (kScreenWidth - frame->w) / 2, y = (kScreenHeight - frame->h) / 2;
				g_system->copyRectToScreen(frame->getPixels(), frame->pitch, MAX(x, 0), MAX(y, 0),
					MIN<int>(frame->w, kScreenWidth), MIN<int>(frame->h, kScreenHeight));
				g_system->updateScreen();
			}
		}
		Common::Event event;
		while (g_system->getEventManager()->pollEvent(event)) {
			if (event.type == Common::EVENT_KEYDOWN) {
				if (event.kbd.keycode == Common::KEYCODE_ESCAPE)
					keepGoing = false;
				skip = true;
			} else if (event.type == Common::EVENT_LBUTTONDOWN) {
				skip = true;
			}
		}
		g_system->delayMillis(5);
	}
	g_system->fillScreen(0);
	g_system->updateScreen();
	return keepGoing && !shouldQuit();
}

// NITENED.EXE 0x445990 (skipped when ned.ini [Intro Video] Ignore=1): DI, then
// 0x445d00 plays CAPERS, then WINDP and INTRO. Escape stops the whole sequence.
void NightmareNedEngine::playIntro() {
	static const char *const videos[] = { "DI.SMK", "CAPERS.SMK", "WINDP.SMK", "INTRO.SMK" };
	for (const char *v : videos) {
		if (!playVideo(v))
			break;
	}
}

// Area music: SOUND\<area>002208.wav inside DATA32.Z (22 kHz, 8-bit)
void NightmareNedEngine::playMusic(int nafNumber) {
	_mixer->stopHandle(_musicHandle);
	if (nafNumber < 90000)
		return;
	Common::String name = Common::String::format("%02d002208.wav", nafNumber / 1000);
	Common::SeekableReadStream *stream = openFile(name);
	if (!stream)
		return;
	Audio::RewindableAudioStream *wav = Audio::makeWAVStream(stream, DisposeAfterUse::YES);
	if (wav) {
		debugC(1, kDebugSound, "Music %s", name.c_str());
		_mixer->playStream(Audio::Mixer::kMusicSoundType, &_musicHandle, Audio::makeLoopingAudioStream(wav, 0));
	}
}

void NightmareNedEngine::showNaf(int nafIndex) {
	nafIndex = (nafIndex + 12) % 12;
	NafArchive *naf = openNaf(kNafNumbers[nafIndex]);
	if (!naf)
		return;
	delete _naf;
	_naf = naf;
	_nafIndex = nafIndex;
	for (SpriteFrame *f : _frames)
		delete f;
	_frames.clear();
	_bgKeys.clear();
	for (const ResKey &k : _naf->listResources(kResBackground))
		_bgKeys.push_back(k.scene << 16 | k.id);
	_spriteIndex = _frameIndex = 0;
	playMusic(kNafNumbers[nafIndex]);
	showBackground(0);
}

// Narrow backgrounds are strips of a scrolling level (NED95000: 24 x 80x960). Until
// the level code exists, lay every same-sized background of the scene side by side.
bool NightmareNedEngine::showBackground(int index) {
	if (!_naf || _bgKeys.empty())
		return false;
	_bgIndex = (index + _bgKeys.size()) % _bgKeys.size();
	int key = _bgKeys[_bgIndex];

	Background bg;
	Common::ScopedPtr<Common::SeekableReadStream> stream(_naf->getResource(kResBackground, key >> 16, key & 0xFFFF));
	if (!stream || !bg.load(*stream)) {
		warning("Background %d:%d doesn't decode", key >> 16, key & 0xFFFF);
		return false;
	}
	if (bg.hasPalette)
		memcpy(_palette, bg.palette, sizeof(_palette));

	Common::Array<int> strips;
	if (bg.surface.w < 320) {
		for (int k : _bgKeys) {
			if ((k >> 16) != (key >> 16))
				continue;
			Background other;
			Common::ScopedPtr<Common::SeekableReadStream> s(_naf->getResource(kResBackground, k >> 16, k & 0xFFFF));
			if (s && other.load(*s) && other.surface.w == bg.surface.w && other.surface.h == bg.surface.h)
				strips.push_back(k);
		}
	}

	_view.free();
	if (strips.size() > 1) {
		_view.create(bg.surface.w * strips.size(), bg.surface.h, Graphics::PixelFormat::createFormatCLUT8());
		for (uint i = 0; i < strips.size(); i++) {
			Background strip;
			Common::ScopedPtr<Common::SeekableReadStream> s(_naf->getResource(kResBackground, strips[i] >> 16, strips[i] & 0xFFFF));
			strip.load(*s);
			_view.copyRectToSurface(strip.surface, i * bg.surface.w, 0, Common::Rect(strip.surface.w, strip.surface.h));
		}
	} else {
		_view.copyFrom(bg.surface);
	}
	_scrollX = _scrollY = 0;
	debugC(1, kDebugGraphics, "%s background %d:%d %dx%d -> view %dx%d", _naf->getName().c_str(),
		key >> 16, key & 0xFFFF, bg.surface.w, bg.surface.h, _view.w, _view.h);
	redraw();
	return true;
}

void NightmareNedEngine::redraw() {
	_screen.fillRect(Common::Rect(kScreenWidth, kScreenHeight), 0);
	if (_view.getPixels()) {
		_scrollX = CLIP(_scrollX, 0, MAX(0, (int)_view.w - kScreenWidth));
		_scrollY = CLIP(_scrollY, 0, MAX(0, (int)_view.h - kScreenHeight));
		Common::Rect src(_scrollX, _scrollY, MIN<int>(_view.w, _scrollX + kScreenWidth),
			MIN<int>(_view.h, _scrollY + kScreenHeight));
		_screen.copyRectToSurface(_view, 0, 0, src);
	}

	if (_showSprite && _naf) {
		Common::Array<ResKey> sprites = _naf->listResources(kResSprite);
		if (!sprites.empty()) {
			_spriteIndex = (_spriteIndex + sprites.size()) % sprites.size();
			if (_frames.empty()) {
				Common::ScopedPtr<Common::SeekableReadStream> s(_naf->getResource(kResSprite,
					sprites[_spriteIndex].scene, sprites[_spriteIndex].id));
				if (s && !loadSpriteBank(*s, _frames))
					warning("Sprite %d:%d doesn't decode", sprites[_spriteIndex].scene, sprites[_spriteIndex].id);
			}
			if (!_frames.empty()) {
				_frameIndex = (_frameIndex + _frames.size()) % _frames.size();
				const SpriteFrame *f = _frames[_frameIndex];
				int ox = (kScreenWidth - f->surface.w) / 2, oy = (kScreenHeight - f->surface.h) / 2;
				for (int y = 0; y < f->surface.h; y++) {
					for (int x = 0; x < f->surface.w; x++) {
						if (f->isOpaque(x, y) && ox + x >= 0 && ox + x < kScreenWidth && oy + y >= 0 && oy + y < kScreenHeight)
							*(byte *)_screen.getBasePtr(ox + x, oy + y) = *(const byte *)f->surface.getBasePtr(x, y);
					}
				}
			}
		}
	}

	g_system->getPaletteManager()->setPalette(_palette, 0, 256);
	g_system->copyRectToScreen(_screen.getPixels(), _screen.pitch, 0, 0, kScreenWidth, kScreenHeight);
	g_system->updateScreen();
}

bool NightmareNedEngine::saveShot(const Common::String &path) {
	Common::DumpFile out;
	if (!out.open(Common::Path(path, '/'), true))
		return false;
	return Image::writePNG(out, _screen, _palette);
}

static Common::String digest(const Common::Array<byte> &data) {
	Common::MemoryReadStream stream(data.data(), data.size());
	return Common::computeStreamMD5AsString(stream).substr(0, 8);
}

// Test hook: one line per decoded background / sprite frame / sound, to compare with tools/nafdump.py
void NightmareNedEngine::dumpChecksums(const Common::String &path) {
	Common::DumpFile out;
	if (!out.open(Common::Path(path, '/'), true))
		return;
	int failures = 0;
	for (int n : kNafNumbers) {
		Common::ScopedPtr<NafArchive> naf(openNaf(n));
		if (!naf)
			continue;
		for (const ResKey &k : naf->listResources(kResBackground)) {
			Common::ScopedPtr<Common::SeekableReadStream> s(naf->getResource(kResBackground, k.scene, k.id));
			Background bg;
			if (!s || !bg.load(*s)) {
				out.writeString(Common::String::format("%05d bg %d:%d FAIL\n", n, k.scene, k.id));
				failures++;
				continue;
			}
			Common::Array<byte> px(bg.surface.w * bg.surface.h);
			for (int y = 0; y < bg.surface.h; y++)
				memcpy(&px[y * bg.surface.w], bg.surface.getBasePtr(0, y), bg.surface.w);
			out.writeString(Common::String::format("%05d bg %d:%d %dx%d %s\n", n, k.scene, k.id,
				bg.surface.w, bg.surface.h, digest(px).c_str()));
		}
		for (const ResKey &k : naf->listResources(kResSprite)) {
			Common::ScopedPtr<Common::SeekableReadStream> s(naf->getResource(kResSprite, k.scene, k.id));
			Common::Array<SpriteFrame *> frames;
			if (s && !loadSpriteBank(*s, frames)) {
				out.writeString(Common::String::format("%05d spr %d:%d FAIL\n", n, k.scene, k.id));
				failures++;
			}
			for (uint i = 0; i < frames.size(); i++) {
				const SpriteFrame *f = frames[i];
				Common::Array<byte> px;
				for (int y = 0; y < f->surface.h; y++) {
					for (int x = 0; x < f->surface.w; x++) {
						bool opaque = f->isOpaque(x, y);
						px.push_back(opaque ? *(const byte *)f->surface.getBasePtr(x, y) : 0);
						px.push_back(opaque);
					}
				}
				out.writeString(Common::String::format("%05d spr %d:%d:%d %dx%d %s\n", n, k.scene, k.id, i,
					f->surface.w, f->surface.h, digest(px).c_str()));
				delete f;
			}
		}
		for (const ResKey &k : naf->listResources(kResSound)) {
			Common::ScopedPtr<Common::SeekableReadStream> s(naf->getResource(kResSound, k.scene, k.id));
			out.writeString(Common::String::format("%05d snd %d:%d %d\n", n, k.scene, k.id, s ? (int)s->size() : -1));
		}
	}
	debug("Checksums written to %s, %d failures", path.c_str(), failures);
}

void NightmareNedEngine::viewer() {
	// The attic (scene 4 loads NED98000; its first room is 98000 2:1, 1200x480)
	int nafNumber = ConfMan.hasKey("nightmarened_naf") ? ConfMan.getInt("nightmarened_naf") : 98000;
	int start = 0;
	for (int i = 0; i < 12; i++) {
		if (kNafNumbers[i] == nafNumber)
			start = i;
	}
	showNaf(start);
	if (ConfMan.hasKey("nightmarened_bg")) {
		int scene = 0, id = 0;
		if (sscanf(ConfMan.get("nightmarened_bg").c_str(), "%d:%d", &scene, &id) == 2) {
			for (uint i = 0; i < _bgKeys.size(); i++) {
				if (_bgKeys[i] == (scene << 16 | id))
					showBackground(i);
			}
		}
	} else if (nafNumber == 98000) {
		for (uint i = 0; i < _bgKeys.size(); i++) {
			if (_bgKeys[i] == (2 << 16 | 1))
				showBackground(i);
		}
	}
	if (ConfMan.hasKey("nightmarened_sprite")) {
		_showSprite = true;
		_spriteIndex = ConfMan.getInt("nightmarened_sprite");
		redraw();
	}
	if (ConfMan.hasKey("nightmarened_shot")) {
		saveShot(ConfMan.get("nightmarened_shot"));
		return;
	}

	// Arrows scroll (Shift = faster), PgUp/PgDn change background, N/P change NAF,
	// Tab toggles the sprite overlay, [ ] change sprite bank, , . change frame
	while (!shouldQuit()) {
		Common::Event event;
		bool dirty = false;
		while (g_system->getEventManager()->pollEvent(event)) {
			if (event.type != Common::EVENT_KEYDOWN)
				continue;
			int step = (event.kbd.flags & Common::KBD_SHIFT) ? 64 : 16;
			switch (event.kbd.keycode) {
			case Common::KEYCODE_LEFT: _scrollX -= step; dirty = true; break;
			case Common::KEYCODE_RIGHT: _scrollX += step; dirty = true; break;
			case Common::KEYCODE_UP: _scrollY -= step; dirty = true; break;
			case Common::KEYCODE_DOWN: _scrollY += step; dirty = true; break;
			case Common::KEYCODE_PAGEUP: showBackground(_bgIndex - 1); break;
			case Common::KEYCODE_PAGEDOWN: showBackground(_bgIndex + 1); break;
			case Common::KEYCODE_n: showNaf(_nafIndex + 1); break;
			case Common::KEYCODE_p: showNaf(_nafIndex - 1); break;
			case Common::KEYCODE_TAB: _showSprite = !_showSprite; dirty = true; break;
			case Common::KEYCODE_LEFTBRACKET:
			case Common::KEYCODE_RIGHTBRACKET:
				for (SpriteFrame *f : _frames)
					delete f;
				_frames.clear();
				_spriteIndex += event.kbd.keycode == Common::KEYCODE_RIGHTBRACKET ? 1 : -1;
				_frameIndex = 0;
				_showSprite = dirty = true;
				break;
			case Common::KEYCODE_COMMA: _frameIndex--; dirty = true; break;
			case Common::KEYCODE_PERIOD: _frameIndex++; dirty = true; break;
			default: break;
			}
		}
		if (dirty)
			redraw();
		g_system->delayMillis(10);
	}
}

uint32 NightmareNedEngine::readInput() {
	// Directions, then one button replaces them (Shift > Space > Ctrl), then raw keys
	static const struct {
		Common::KeyCode key;
		uint32 bit;
	} directions[] = {
		{ Common::KEYCODE_LEFT, kInputLeft }, { Common::KEYCODE_RIGHT, kInputRight },
		{ Common::KEYCODE_UP, kInputUp }, { Common::KEYCODE_DOWN, kInputDown }
	}, buttons[] = {
		{ Common::KEYCODE_LSHIFT, kInputButton1 }, { Common::KEYCODE_RSHIFT, kInputButton1 },
		{ Common::KEYCODE_SPACE, kInputButton2 },
		{ Common::KEYCODE_LCTRL, kInputButton3 }, { Common::KEYCODE_RCTRL, kInputButton3 }
	}, raw[] = {
		{ Common::KEYCODE_z, kInputZ }, { Common::KEYCODE_c, kInputC }, { Common::KEYCODE_s, kInputS },
		{ Common::KEYCODE_a, kInputA }, { Common::KEYCODE_w, kInputW }, { Common::KEYCODE_o, kInputOptions },
		{ Common::KEYCODE_ESCAPE, kInputOptions }, { Common::KEYCODE_q, kInputQuit }
	};
	uint32 input = 0;
	for (const auto &d : directions) {
		if (_keysDown[d.key])
			input |= d.bit;
	}
	for (const auto &b : buttons) {
		if (_keysDown[b.key]) {
			input = b.bit;
			break;
		}
	}
	for (const auto &r : raw) {
		if (_keysDown[r.key])
			input = r.bit;
	}
	return input;
}

// The demo that stood in for the scene code (nightmarened_demo=true): the attic built
// from its data with every sprite looping and a simple camera band.
void NightmareNedEngine::runDemo() {
	_game = new Game(this);
	_display = &_game->display();
	_keysDown.resize(Common::KEYCODE_LAST);
	_display->_showCollision = ConfMan.hasKey("nightmarened_collision") && ConfMan.getBool("nightmarened_collision");
	_game->setNed(new Ned(_game));
	if (!_game->ned()->init())
		warning("Ned did not load");

	PlacementDemo demo(this, _game);
	if (!demo.loadAttic())
		return;
	if (ConfMan.hasKey("nightmarened_scroll")) {
		Common::Point scroll;
		sscanf(ConfMan.get("nightmarened_scroll").c_str(), "%hd,%hd", &scroll.x, &scroll.y);
		demo.setScroll(scroll);
	}
	mainLoop(&demo);
}

// WinMain 0x445f70: the scenes are registered (0x4654b0 first), the options set up
// (0x45da60), the scenes started (0x4598b0), Ned loaded (0x44f710, 0x44fbc0), and after the
// intro scene 0 is entered from -1. A ScummVM save_slot loads that game instead.
// Test hooks: nightmarened_scene=<n> starts in scene n; nightmarened_from=<scene> is the
// origin it sees (alone, it starts the attic: 0 plays the TV clip 8702).
void NightmareNedEngine::runGame() {
	_game = new Game(this);
	_display = &_game->display();
	_keysDown.resize(Common::KEYCODE_LAST);
	_display->_showCollision = ConfMan.hasKey("nightmarened_collision") && ConfMan.getBool("nightmarened_collision");
	_game->addScene(new MenuScene(_game));
	_game->addScene(new Scene1(_game));
	_game->addScene(new AtticScene(_game));
	_game->addScene(new Scene7(_game));
	_options = new Options(_game);
	if (_options->init())
		warning("The options screen did not load");
	_game->initScenes();
	_game->setNed(new Ned(_game));
	if (!_game->ned()->init())
		warning("Ned did not load");

	int slot = ConfMan.hasKey("save_slot") ? ConfMan.getInt("save_slot") : -1;
	if (slot < 0 || loadGameState(slot).getCode() != Common::kNoError) {
		int from = ConfMan.hasKey("nightmarened_from") ? ConfMan.getInt("nightmarened_from") : -1;
		int scene = ConfMan.hasKey("nightmarened_from") ? 4 : 0, sub = -1;
		if (ConfMan.hasKey("nightmarened_scene"))
			sscanf(ConfMan.get("nightmarened_scene").c_str(), "%d:%d", &scene, &sub);
		_game->switchScene(scene, sub, from, -1);
	}
	mainLoop(nullptr);
}

// The Windows virtual key a WM_KEYDOWN would carry (only what 0x45e740 reads matters)
static uint32 virtualKey(const Common::KeyState &kbd) {
	if (kbd.keycode >= Common::KEYCODE_a && kbd.keycode <= Common::KEYCODE_z)
		return 'A' + (kbd.keycode - Common::KEYCODE_a);
	if (kbd.keycode >= Common::KEYCODE_0 && kbd.keycode <= Common::KEYCODE_9)
		return '0' + (kbd.keycode - Common::KEYCODE_0);
	switch (kbd.keycode) {
	case Common::KEYCODE_BACKSPACE:
		return 0x08;
	case Common::KEYCODE_TAB:
		return 0x09;
	case Common::KEYCODE_RETURN:
		return 0x0d;
	case Common::KEYCODE_ESCAPE:
		return 0x1b;
	case Common::KEYCODE_SPACE:
		return 0x20;
	case Common::KEYCODE_LEFT:
		return 0x25;
	case Common::KEYCODE_UP:
		return 0x26;
	case Common::KEYCODE_RIGHT:
		return 0x27;
	case Common::KEYCODE_DOWN:
		return 0x28;
	case Common::KEYCODE_DELETE:
		return 0x2e;
	default:
		return 0;
	}
}

static void writeShot(Display *display, const Common::String &path) {
	Common::DumpFile out;
	byte pal[256 * 3];
	g_system->getPaletteManager()->grabPalette(pal, 0, 256);
	if (out.open(Common::Path(path, '/'), true))
		Image::writePNG(out, display->getPrimary()->getBuffer(), pal);
}

// One WinMain loop iteration at a time: events (WM_KEYDOWN goes to 0x45e740 while the
// options screen is open), then the options loop 0x45e060, or the input bitmask (0x446700)
// and options (0x10000, 0x45db30), quit to the quilt (0x20000) or the scene tick (0x45a310).
// Test hooks: nightmarened_ticks=<n> runs n drawn frames, then nightmarened_shot=<png>;
// nightmarened_shots=<frame>:<png>,... saves several and quits after the last;
// nightmarened_inputs=<bits>:<frames>,... replaces the keyboard for that many drawn frames
// each, and t<LETTERS> types those keys (_ is a space) for one frame.
// While the options screen is open every 41 ms counts as a frame.
// F9 toggles the collision overlay (ned.ini ShowCollisionRects).
void NightmareNedEngine::mainLoop(PlacementDemo *demo) {
	int ticksLeft = ConfMan.hasKey("nightmarened_ticks") ? ConfMan.getInt("nightmarened_ticks") : -1;
	struct ScriptedInput {
		uint32 bits;
		int ticks;
		Common::String text;
	};
	Common::Array<ScriptedInput> script;
	if (ConfMan.hasKey("nightmarened_inputs")) {
		Common::String spec = ConfMan.get("nightmarened_inputs");
		const char *p = spec.c_str();
		while (*p) {
			char *end;
			ScriptedInput in;
			in.bits = 0;
			if (*p == 't') {
				for (p++; *p && *p != ',' && *p != ':' && *p != ' '; p++)
					in.text += *p == '_' ? ' ' : *p;
			} else {
				in.bits = strtoul(p, &end, 0);
				if (end == p)
					break;
				p = end;
			}
			in.ticks = 1;
			if (*p == ':') {
				in.ticks = strtol(p + 1, &end, 0);
				p = end;
			}
			script.push_back(in);
			while (*p == ',' || *p == ' ')
				p++;
		}
	}
	struct Shot {
		int frame;
		Common::String path;
	};
	Common::Array<Shot> shots;
	if (ConfMan.hasKey("nightmarened_shots")) {
		Common::String spec = ConfMan.get("nightmarened_shots");
		const char *p = spec.c_str();
		while (*p) {
			char *end;
			Shot shot;
			shot.frame = strtol(p, &end, 0);
			if (end == p || *end != ':')
				break;
			p = end + 1;
			while (*p && *p != ',')
				shot.path += *p++;
			shots.push_back(shot);
			while (*p == ',')
				p++;
		}
	}
	int frames = 0;
	uint shotIndex = 0;
	uint scriptIndex = 0;
	int scriptTicks = 0;
	bool typed = false;
	while (!shouldQuit()) {
		uint32 start = g_system->getMillis();
		bool optionsOpen = _options && _options->isOpen();
		Common::Event event;
		_idle = true;
		while (g_system->getEventManager()->pollEvent(event)) {
			if (event.type == Common::EVENT_KEYDOWN && event.kbd.keycode < Common::KEYCODE_LAST) {
				if (event.kbd.keycode == Common::KEYCODE_F9)
					_display->_showCollision = !_display->_showCollision;
				_keysDown[event.kbd.keycode] = true;
				if (_options && _options->isOpen())
					_options->keyDown(virtualKey(event.kbd));
			} else if (event.type == Common::EVENT_KEYUP && event.kbd.keycode < Common::KEYCODE_LAST) {
				_keysDown[event.kbd.keycode] = false;
			}
		}
		_idle = false;
		if (scriptIndex < script.size() && !typed && _options && _options->isOpen()) {
			for (uint i = 0; i < script[scriptIndex].text.size(); i++)
				_options->keyDown((byte)toupper((byte)script[scriptIndex].text[i]));
			typed = true;
		}
		uint32 input = readInput();
		if (scriptIndex < script.size())
			input = script[scriptIndex].bits;
		uint32 draws = _display->_drawCount;
		if (demo) {
			demo->tick(input, _game->now());
		} else if (_options && _options->isOpen()) {
			// 0x45e650 reads the arrows and Space itself
			uint32 keys = 0;
			if (scriptIndex < script.size()) {
				keys = script[scriptIndex].bits & (kInputLeft | kInputRight | kInputUp | kInputDown | kInputButton2);
			} else {
				keys = (_keysDown[Common::KEYCODE_LEFT] ? kInputLeft : 0) | (_keysDown[Common::KEYCODE_RIGHT] ? kInputRight : 0) |
					(_keysDown[Common::KEYCODE_UP] ? kInputUp : 0) | (_keysDown[Common::KEYCODE_DOWN] ? kInputDown : 0) |
					(_keysDown[Common::KEYCODE_SPACE] ? kInputButton2 : 0);
			}
			_options->tick(keys, _game->now());
		} else if (_game->_optionsAllowed && (input & kInputOptions) && _options) {
			_options->open();
		} else if (input & kInputQuit) {
			int scene = _game->getCurrentSceneId();
			if (scene != 0 && scene != -1) {
				if (_game->hints().isActive())
					_game->hints().cancel();
				_game->switchScene(0, -1);
			}
		} else {
			_game->tick(input, _game->now());
		}
		bool drew = _display->_drawCount != draws;
		if (optionsOpen || (_options && _options->isOpen())) {
			uint32 elapsed = g_system->getMillis() - start;
			if (elapsed < kMsPerStep)
				g_system->delayMillis(kMsPerStep - elapsed);
			drew = true;
		}
		if (drew && scriptIndex < script.size() && ++scriptTicks >= script[scriptIndex].ticks) {
			scriptIndex++;
			scriptTicks = 0;
			typed = false;
		}
		if (drew)
			frames++;
		while (drew && shotIndex < shots.size() && frames >= shots[shotIndex].frame) {
			writeShot(_display, shots[shotIndex].path);
			debugC(1, kDebugGraphics, "Frame %d saved to %s", frames, shots[shotIndex].path.c_str());
			if (++shotIndex == shots.size() && ticksLeft < 0)
				return;
		}
		if (drew && ticksLeft > 0 && --ticksLeft == 0) {
			if (ConfMan.hasKey("nightmarened_shot"))
				writeShot(_display, ConfMan.get("nightmarened_shot"));
			break;
		}
		if (demo)
			g_system->delayMillis(5);
	}
}

Common::Error NightmareNedEngine::run() {
	initGraphics(kScreenWidth, kScreenHeight);
	_screen.create(kScreenWidth, kScreenHeight, Graphics::PixelFormat::createFormatCLUT8());
	if (!mountData())
		return Common::kNoGameDataFoundError;

	if (ConfMan.hasKey("nightmarened_checksums")) {
		dumpChecksums(ConfMan.get("nightmarened_checksums"));
		return Common::kNoError;
	}
	bool loading = ConfMan.hasKey("save_slot") && ConfMan.getInt("save_slot") >= 0;
	if (!loading && !(ConfMan.hasKey("nightmarened_skipintro") && ConfMan.getBool("nightmarened_skipintro")))
		playIntro();
	if (shouldQuit())
		return Common::kNoError;
	if (ConfMan.hasKey("nightmarened_viewer") && ConfMan.getBool("nightmarened_viewer"))
		viewer();
	else if (ConfMan.hasKey("nightmarened_demo") && ConfMan.getBool("nightmarened_demo"))
		runDemo();
	else
		runGame();
	return Common::kNoError;
}

} // End of namespace NightmareNed
