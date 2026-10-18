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
#include "audio/decoders/raw.h"
#include "audio/decoders/wave.h"
#include "common/algorithm.h"
#include "common/config-manager.h"
#include "common/debug.h"
#include "common/endian.h"
#include "common/file.h"
#include "common/formats/winexe_ne.h"
#include "common/memstream.h"
#include "common/system.h"
#include "common/textconsole.h"

#include "mspam/busgame.h"
#include "mspam/detection.h"
#include "mspam/media.h"

namespace MSPAM {

// Addresses below are those of BUSGAME1.EX_ (segment 1000) and BUSGAME2.DLL
enum {
	kTickMs = 40,            // SetTimer(hwnd, 1, 40, NULL)
	kKeyLeft = 1,
	kKeyRight = 2,
	kKeyUp = 4,
	kKeyDown = 8,
	kKeySpace = 0x10,
	kPowerSnowman = 1,       // fire and lava don't hurt
	kPowerFire = 2,
	kPowerJetPack = 4,       // Up flies
	kPowerBoots = 8,         // higher jumps
	kPowerTracBoots = 0x10   // grip on slippery levels
};

// Object words (LEVELINFO, 18 words per object)
enum {
	kOType, kOScreen, kOLeft, kOTop, kORight, kOBottom, kOFlags = 8, kOPresent, kOActive,
	kOX1, kOY1, kOX2, kOY2, kOSpeed, kOCount, kOReset
};

enum {
	kFlagSolid = 1,          // can be stood on
	kFlagPushable = 2,
	kFlagLandable = 8        // can be landed on from a jump
};

static const char *const kObjectTypes[] = {
	"", "BLOCK", "STAR", "COIN", "PLATFORM1", "PLATFORM2", "PLATFORM3", "JETPAK", "TRACBOOTS", "FIRE",
	"BOOTS", "BALLOON", "AIRTANK", "SNOWMAN", "WEIGHT", "LAVA", "CLOUD1", "CLOUD2", "CLOUD3", "CLOUD4",
	"BUBBLE", "MARTIANLEFT", "UP", "DOWN", "LEFTARROW", "RIGHT"
};

static const char *const kPlanets[] = {
	"MOON", "MERCURY", "VENUS", "MARS", "JUPITER", "SATURN", "URANUS", "NEPTUNE", "PLUTO"
};

// Gravity per planet (DS:020e)
static const float kGravity[] = { 0.16f, 0.38f, 0.9f, 0.33f, 0.95f, 0.86f, 0.4f, 0.74f, 0.26f };

static const char *const kSounds[] = {
	"JUMPSOUND", "JETSOUND", "GRABSOUND", "STARSOUND", "VICTORY", "OUCH", "BALLOON", "LIZTALK"
};
enum { kSoundJump, kSoundJet, kSoundGrab, kSoundStar, kSoundVictory, kSoundOuch, kSoundBalloon, kSoundLiz };

// x87 conversions truncate (_ftol with the rounding control set to chop)
static inline int32 ftol(float f) {
	return (int32)f;
}

BusGame::BusGame(Audio::Mixer *mixer, const Common::String &planet, bool lizIntro, bool lowMemory) :
	_mixer(mixer), _planet(planet), _lizIntro(lizIntro), _lowMemory(lowMemory), _data(nullptr), _dataSize(0),
	_bgColor(0), _head(nullptr), _nextUpdate(nullptr), _music(nullptr), _musicRestart(0), _victoryEnd(0),
	_now(0), _nextTick(0), _seed(1), _player(nullptr), _jet(nullptr), _riding(nullptr), _pushing(nullptr),
	_overlay(nullptr), _liz(nullptr), _whatsit(nullptr), _keys(0), _prevKeys(0), _vx(0), _vy(0), _target(0),
	_grav(0), _planetGrav(0), _air(0), _floorLimit(419), _lizTX(0), _lizTY(0), _lizVy(0), _airShown(0),
	_score32(0), _planetIndex(0), _screen(1), _pendingScreen(-1), _screenChange(false), _screenSolid(true),
	_lizState(0), _lizCarry(false), _running(false), _stateChanged(false), _pushFlag(false), _fellOff(false),
	_powerups(0), _result(-1), _ticks(0), _lastOuch(0), _done(false) {
	_planet.toUppercase();
	memset(_palette, 0, sizeof(_palette));
	memset(_heights, 0, sizeof(_heights));
	memset(&_level, 0, sizeof(_level));
	memset(&_backup, 0, sizeof(_backup));
	for (int i = 0; i < 6; i++)
		_score[i] = nullptr;
	for (int i = 0; i < 3; i++)
		_airDigits[i] = nullptr;
}

BusGame::~BusGame() {
	for (int i = 0; i < 4; i++)
		_mixer->stopHandle(_channels[i]);
	for (Node *n = _head; n;) {
		Node *next = n->next;
		if (!n->isLayer)
			delete (Sprite *)n;
		n = next;
	}
	for (Sprite *s : _garbage)
		delete s;
	for (Layer *l : _layers)
		delete l;
	for (Sequence *s : _seqs)
		delete s;
	for (SpriteType *t : _types)
		delete t;
	for (auto &i : _images)
		delete i._value;
	free(_data);
}

int BusGame::getResult() const {
	return _result == -1 ? 0 : _result;
}

int BusGame::msRand() {
	// The Microsoft C msRand() of BUSGAME1 (1000:71f8)
	_seed = _seed * 0x343fd + 0x269ec3;
	return (_seed >> 16) & 0x7fff;
}

// ---- Loading ----

// Records {u32 length, TYPE\0, NAME\0, payload}; the first one is the ACTION script
const byte *BusGame::findRecord(const char *type, const char *name, uint32 *size) const {
	uint32 p = 0;
	while (p + 8 < _dataSize) {
		uint32 len = READ_LE_UINT32(_data + p);
		const char *t = (const char *)_data + p + 4;
		uint32 tl = strnlen(t, _dataSize - p - 4);
		const char *n = t + tl + 1;
		if (!strcmp(t, "END") || len < 6 || p + len > _dataSize)
			break;
		uint32 nl = strnlen(n, _dataSize - (n - (const char *)_data));
		if (!strcmp(t, type) && (!name || !scumm_stricmp(n, name))) {
			const byte *payload = (const byte *)n + nl + 1;
			if (size)
				*size = _data + p + len - payload;
			return payload;
		}
		p += len;
	}
	return nullptr;
}

// SPRITEIMAGE: {u16 w, u16 h, u32 row offsets[h]}, rows of ops 20-3f skip n-1f,
// 40-7f run of n-3f of the next byte, 80-ff literal of n-7f bytes (BUSGAME1.DLL)
const BusGame::Image *BusGame::getImage(const Common::String &name) {
	if (_images.contains(name))
		return _images[name];
	uint32 size;
	const byte *p = findRecord("SPRITEIMAGE", name.c_str(), &size);
	Image *img = nullptr;
	if (p && size >= 4) {
		img = new Image();
		img->w = READ_LE_UINT16(p);
		img->h = READ_LE_UINT16(p + 2);
		img->pixels.resize(img->w * img->h);
		img->opaque.resize(img->w * img->h);
		for (int y = 0; y < img->h && 4 + 4 * y + 4 <= (int)size; y++) {
			uint32 off = READ_LE_UINT32(p + 4 + 4 * y);
			uint32 end = y + 1 < img->h ? READ_LE_UINT32(p + 8 + 4 * y) : size;
			int x = 0;
			while (off < end && off < size && x < img->w) {
				byte op = p[off++];
				int n;
				if (op < 0x40) {
					x += op - 0x1f;
				} else if (op < 0x80) {
					n = op - 0x3f;
					byte c = off < size ? p[off++] : 0;
					for (int i = 0; i < n && x < img->w; i++, x++) {
						img->pixels[y * img->w + x] = c;
						img->opaque[y * img->w + x] = 1;
					}
				} else {
					n = op - 0x7f;
					for (int i = 0; i < n && x < img->w && off < size; i++, x++) {
						img->pixels[y * img->w + x] = p[off++];
						img->opaque[y * img->w + x] = 1;
					}
				}
			}
		}
	} else {
		warning("BUSGAME: no image '%s'", name.c_str());
	}
	_images[name] = img;
	return img;
}

static Common::String fixedString(const byte *p, int max) {
	Common::String s;
	for (int i = 0; i < max && p[i]; i++)
		s += (char)p[i];
	return s;
}

// LOADACTION (BUSGAME2 1000:2498): a dump of the editor's linked structs, read in
// order; a non-zero (stale) next pointer means another record follows
bool BusGame::parseAction() {
	const byte *p = findRecord("ACTION", nullptr);
	if (!p)
		return false;
	const byte *end = _data + _dataSize;
	if (p + 0x8b > end)
		return false;
	const byte *h = p;
	p += 0x8b;
	_bgColor = READ_LE_UINT16(h + 0x69) == 1 ? READ_LE_UINT16(h + 0x71) : 0;
	uint32 seqs = READ_LE_UINT32(h + 0x73), layers = READ_LE_UINT32(h + 0x7b);
	uint32 procs = READ_LE_UINT32(h + 0x7f), types = READ_LE_UINT32(h + 0x83), cycles = READ_LE_UINT32(h + 0x87);
	if (procs || cycles)
		warning("BUSGAME: processes and palette cycles are not implemented");

	if (seqs) {
		for (;;) {
			if (p + 0x6f > end)
				return false;
			Sequence *seq = new Sequence();
			seq->name = fixedString(p, 0x65);
			seq->centered = READ_LE_UINT16(p + 0x65) == 0;
			uint32 frames = READ_LE_UINT32(p + 0x67), next = READ_LE_UINT32(p + 0x6b);
			p += 0x6f;
			_seqs.push_back(seq);
			if (frames) {
				for (;;) {
					if (p + 0x73 > end)
						return false;
					Frame f;
					f.img = getImage(fixedString(p, 0x65));
					f.dx = (int16)READ_LE_UINT16(p + 0x69);
					f.dy = (int16)READ_LE_UINT16(p + 0x6b);
					f.dur = (int16)READ_LE_UINT16(p + 0x6d);
					uint32 fnext = READ_LE_UINT32(p + 0x6f);
					p += 0x73;
					if (f.img)
						seq->frames.push_back(f);
					if (!fnext)
						break;
				}
			}
			if (!next)
				break;
		}
	}
	if (layers) {
		for (;;) {
			if (p + 0xdc > end)
				return false;
			Layer *l = new Layer();
			l->isLayer = true;
			l->prev = l->next = nullptr;
			l->linked = false;
			l->name = fixedString(p, 0x65);
			l->img = getImage(fixedString(p + 0x65, 0x69));
			l->x = (int16)READ_LE_UINT16(p + 0xce);
			l->y = (int16)READ_LE_UINT16(p + 0xd0);
			l->pri = (int16)READ_LE_UINT16(p + 0xd2);
			l->visible = true;
			uint32 next = READ_LE_UINT32(p + 0xd8);
			p += 0xdc;
			_layers.push_back(l);
			if (!next)
				break;
		}
	}
	if (types) {
		for (;;) {
			if (p + 0x102 > end)
				return false;
			SpriteType *t = new SpriteType();
			t->name = fixedString(p, 0x65);
			Common::String seq = fixedString(p + 0x65, 0x69);
			t->seq = nullptr;
			for (const Sequence *s : _seqs)
				if (s->name == seq)
					t->seq = s;
			t->pri = (int16)READ_LE_UINT16(p + 0xce);
			t->vx = READ_LE_FLOAT32(p + 0xd0);
			t->vy = READ_LE_FLOAT32(p + 0xd4);
			t->ax = READ_LE_FLOAT32(p + 0xd8);
			t->ay = READ_LE_FLOAT32(p + 0xdc);
			// +e0 the collision masks are 0 in every planet's data
			t->offMode = (int16)READ_LE_UINT16(p + 0xec);
			for (int i = 0; i < 4; i++)
				t->offRect[i] = (int16)READ_LE_UINT16(p + 0xee + 2 * i);
			t->trigFrame = (int16)READ_LE_UINT16(p + 0xf6);
			t->trigTimer = (int16)READ_LE_UINT16(p + 0xf8);
			t->offAct = (int16)READ_LE_UINT16(p + 0xfa);
			t->animAct = (int16)READ_LE_UINT16(p + 0xfc);
			uint32 next = READ_LE_UINT32(p + 0xfe);
			p += 0x102;
			if (t->seq && !t->seq->frames.empty())
				_types.push_back(t);
			else
				delete t;
			if (!next)
				break;
		}
	}
	return true;
}

// LOADLEVELINFO / FUN_1000_149a: six 80x52 grids of 8-pixel cells, the screen
// links, the objects and the start
bool BusGame::loadLevel() {
	uint32 size;
	const byte *p = findRecord("LEVELINFO", "LEVELINFO", &size);
	if (!p || size < 0x6b92) {
		// FUN_1000_149a's default: six screens with a floor, linked three by two
		memset(&_level, 0, sizeof(_level));
		for (int i = 0; i < 6; i++) {
			_level.links[i][0] = i < 3 ? i + 4 : 0;
			_level.links[i][1] = i < 3 ? 0 : i - 2;
			_level.links[i][2] = i;
			_level.links[i][3] = i + 2;
			memset(_level.grid[i][51], 1, kGridW);
		}
		_level.links[3][2] = _level.links[0][2] = 0;
		_level.links[5][3] = _level.links[2][3] = 0;
		_level.start[0] = 1;
		_level.start[1] = _level.start[2] = 50;
		_level.start[3] = 1;
	} else {
		memcpy(_level.grid, p, sizeof(_level.grid));
		for (int i = 0; i < 6; i++)
			for (int j = 0; j < 4; j++)
				_level.links[i][j] = (int16)READ_LE_UINT16(p + 0x6180 + 8 * i + 2 * j);
		_level.count = CLIP<int16>((int16)READ_LE_UINT16(p + 0x61b0), 0, kMaxObjects);
		for (int i = 0; i < kMaxObjects; i++) {
			for (int j = 0; j < kObjWords; j++)
				_level.obj[i].w[j] = (int16)READ_LE_UINT16(p + 0x61b2 + 36 * i + 2 * j);
			_level.obj[i].spr = nullptr;
			_level.obj[i].w[kOActive] = 0;
			if (isPlatformType(_level.obj[i]))
				_level.obj[i].w[kOCount] = _level.obj[i].w[kOReset] = 0;
		}
		for (int j = 0; j < 4; j++)
			_level.start[j] = (int16)READ_LE_UINT16(p + 0x6b8a + 2 * j);
	}
	_running = true;
	// Test hook: Liz drops the player off at the object of this type (3 = the coin)
	if (ConfMan.hasKey("mspam_busgame_warp")) {
		int type = ConfMan.getInt("mspam_busgame_warp");
		for (int i = 0; i < _level.count; i++) {
			const Object &o = _level.obj[i];
			if (o.w[kOType] == type) {
				_level.start[0] = o.w[kOScreen];
				_level.start[1] = (o.w[kOLeft] + o.w[kORight]) / 2 - 20;
				_level.start[2] = o.w[kOBottom] - 2;
				break;
			}
		}
	}
	return true;
}

Audio::SeekableAudioStream *BusGame::makeWave(const byte *data, uint32 size) {
	if (!data || !size)
		return nullptr;
	Common::SeekableReadStream *s = new Common::MemoryReadStream(data, size);
	return Audio::makeWAVStream(s, DisposeAfterUse::YES);
}

bool BusGame::loadSounds() {
	Common::NEResources exe;
	if (!exe.loadFromEXE(Common::Path("BUSGAME1.EX_"))) {
		warning("BUSGAME: can't read BUSGAME1.EX_, no sound effects");
		_effects.resize(ARRAYSIZE(kSounds));
		return false;
	}
	for (uint i = 0; i < ARRAYSIZE(kSounds); i++) {
		Common::Array<byte> buf;
		Common::SeekableReadStream *s = exe.getResource(Common::WinResourceID("WAVE"), Common::WinResourceID(kSounds[i]));
		if (s) {
			buf.resize(s->size());
			s->read(buf.data(), buf.size());
			delete s;
		} else {
			warning("BUSGAME: no WAVE resource %s", kSounds[i]);
		}
		_effects.push_back(buf);
	}
	// LOADSONG: the SONG record's WAVE<n> parts in the order of its string
	uint32 size;
	const byte *song = _lowMemory ? nullptr : findRecord("SONG", nullptr, &size);
	if (song && size > 0x8c0) {
		int parts = (int16)READ_LE_UINT16(song + 0x6ad);
		for (int i = 0; i < parts; i++) {
			uint32 wsize = 0;
			const byte *w = findRecord("WAVE", Common::String::format("WAVE%d", i).c_str(), &wsize);
			if (w && wsize > 4) {
				_songParts.push_back(w + 4);
				_songSizes.push_back(MIN<uint32>(READ_LE_UINT32(w), wsize - 4));
			} else {
				_songParts.push_back(nullptr);
				_songSizes.push_back(0);
			}
		}
		_songOrder = fixedString(song + 0x8bf, size - 0x8bf);
	}
	return true;
}

const BusGame::SpriteType *BusGame::findType(const Common::String &name) const {
	for (const SpriteType *t : _types)
		if (t->name == name)
			return t;
	return nullptr;
}

// WM_CREATE of the game window (1000:1b66)
bool BusGame::load() {
	_planetIndex = 0;
	for (uint i = 0; i < ARRAYSIZE(kPlanets); i++)
		if (_planet == kPlanets[i])
			_planetIndex = i;
	_seed = g_system->getMillis() ^ 0x5a17;

	Common::File f;
	Common::String name = _planet + (_lowMemory ? ".4MB" : ".BIN");
	if (!f.open(Common::Path(name)) && !f.open(Common::Path(_planet + ".4MB"))) {
		warning("BUSGAME: can't open %s", name.c_str());
		_result = 102;
		return false;
	}
	_dataSize = f.size();
	_data = (byte *)malloc(_dataSize);
	if (!_data || f.read(_data, _dataSize) != _dataSize || !parseAction()) {
		warning("BUSGAME: bad file %s", name.c_str());
		_result = 103;
		return false;
	}

	// SETIDENTITYACTIONPALETTE: MYPALETTE with the 20 static colours
	uint32 psize;
	const byte *pal = findRecord("SPRITEPALETTE", "MYPALETTE", &psize);
	if (pal && psize >= 4 + 1024)
		for (int i = 0; i < 256; i++)
			memcpy(_palette + 3 * i, pal + 4 + 4 * i, 3);
	MediaManager::setStaticColors(_palette);

	loadLevel();
	_screen = _level.start[0];
	showLayers();
	// STARTACTION puts the layers into the list
	for (Layer *l : _layers)
		insertNode(l);

	const SpriteType *number = findType("NUMBER");
	for (int i = 0; i < 6; i++) {
		_score[i] = createSprite("NUMBER");
		if (!_score[i])
			return false;
		_score[i]->x = 0x4f + 0x15 * i;
		_score[i]->y = 438;
		_score[i]->pri = 0xf;
		_score[i]->frameCount = 1;
		_score[i]->digit = _score[i]->cur = &number->seq->frames.back();
		_score[i]->move = kCbNumber;
		launchSprite(_score[i]);
	}
	for (int i = 0; i < 3; i++) {
		_airDigits[i] = createSprite("NUMBER");
		if (!_airDigits[i])
			return false;
		_airDigits[i]->x = 0x214 + 0x15 * i;
		_airDigits[i]->y = 438;
		_airDigits[i]->pri = 0xf;
		_airDigits[i]->frameCount = 1;
		_airDigits[i]->digit = _airDigits[i]->cur = &number->seq->frames.back();
		_airDigits[i]->move = kCbNumber;
		launchSprite(_airDigits[i]);
	}
	_whatsit = createSprite("WHATSIT");
	if (!_whatsit)
		return false;
	_whatsit->x = 433;
	_whatsit->y = 426;
	launchSprite(_whatsit);
	showScore();
	_backup = _level;
	restartLevel();
	showAir();
	loadSounds();
	startSong();
	_nextTick = 0;
	return !_done;
}

// ---- Sprite library (BUSGAME2.DLL) ----

// CREATESPRITE (1000:179a): a sprite of a type, not yet in the list
BusGame::Sprite *BusGame::createSprite(const Common::String &name) {
	const SpriteType *t = findType(name);
	if (!t) {
		warning("BUSGAME: no sprite type '%s'", name.c_str());
		quit();
		return nullptr;
	}
	Sprite *s = new Sprite();
	s->isLayer = false;
	s->prev = s->next = nullptr;
	s->linked = false;
	s->type = t;
	s->seq = t->seq;
	s->x = s->y = 0;
	s->vx = t->vx;
	s->vy = t->vy;
	s->ax = t->ax;
	s->ay = t->ay;
	s->pri = t->pri;
	s->animating = s->visible = false;
	s->frameNo = s->timer = 0;
	s->trigFrame = t->trigFrame;
	s->trigTimer = t->trigTimer;
	s->offMode = t->offMode;
	for (int i = 0; i < 4; i++)
		s->offRect[i] = t->offRect[i];
	s->offAct = t->offAct;
	s->animAct = t->animAct;
	s->frameCount = s->seq->frames.size();
	s->cur = &s->seq->frames[0];
	s->ix = s->iy = 0;
	s->w = s->cur->img->w;
	s->h = s->cur->img->h;
	s->move = s->off = s->anim = kCbNone;
	s->state = s->dir = 0;
	s->counter = 0;
	s->restOn = nullptr;
	s->digit = nullptr;
	return s;
}

// After all nodes of a lower or equal priority (FUN_1000_0152)
void BusGame::insertNode(Node *n) {
	Node *prev = nullptr, *at = _head;
	while (at && at->pri <= n->pri) {
		prev = at;
		at = at->next;
	}
	n->prev = prev;
	n->next = at;
	if (prev)
		prev->next = n;
	else
		_head = n;
	if (at)
		at->prev = n;
	n->linked = true;
}

void BusGame::unlinkNode(Node *n) {
	if (!n->linked)
		return;
	if (_nextUpdate == n)
		_nextUpdate = n->next;
	if (n->prev)
		n->prev->next = n->next;
	else
		_head = n->next;
	if (n->next)
		n->next->prev = n->prev;
	n->prev = n->next = nullptr;
	n->linked = false;
}

void BusGame::setFrame(Sprite *s) {
	if (s->frameNo < 1 || s->frameNo > s->frameCount)
		s->frameNo = 1;
	if (!s->digit)
		s->cur = &s->seq->frames[s->frameNo - 1];
}

// FUN_1000_04c2: the drawing rectangle from the position and the frame's offset
void BusGame::updateRect(Sprite *s) {
	s->w = s->cur->img->w;
	s->h = s->cur->img->h;
	s->ix = ftol(s->x) + s->cur->dx;
	s->iy = ftol(s->y) + s->cur->dy;
	if (s->seq->centered) {
		s->ix -= s->w >> 1;
		s->iy -= s->h >> 1;
	}
}

void BusGame::launchSprite(Sprite *s) {
	s->animating = s->visible = true;
	insertNode(s);
	setFrame(s);
	updateRect(s);
}

// DELETESPRITE: the memory stays until the end of the tick, so that code reading a
// sprite it has just replaced sees its old fields as the original did
void BusGame::deleteSprite(Sprite *&s) {
	if (!s)
		return;
	if (s->linked) {
		unlinkNode(s);
		_garbage.push_back(s);
	} else if (Common::find(_garbage.begin(), _garbage.end(), s) == _garbage.end()) {
		_garbage.push_back(s);
	}
	s = nullptr;
}

// Offscreen modes: 1 entirely outside the rectangle, 2 not entirely inside it
bool BusGame::isOffscreen(const Sprite *s) const {
	const int16 *r = s->offRect;
	if (s->offMode == 1)
		return s->ix + s->w <= r[0] || r[2] <= s->ix || s->iy + s->h <= r[1] || r[3] <= s->iy;
	if (s->offMode == 2)
		return !(r[0] <= s->ix && s->ix + s->w <= r[2] && r[1] <= s->iy && s->iy + s->h <= r[3]);
	return false;
}

// FUN_1000_3634: one tick of a sprite
void BusGame::updateSprite(Sprite *self) {
	Sprite *s = self;
	if (s->ay != 0 || s->ax != 0) {
		s->vx += s->ax;
		s->vy += s->ay;
	}
	s->x += s->vx;
	s->y += s->vy;
	if (s->animating && ++s->timer >= s->cur->dur) {
		if (++s->frameNo > s->frameCount)
			s->frameNo = 1;
		s->timer = 0;
		setFrame(s);
	}
	call(s->move, self);
	if (!self)
		return;
	updateRect(s);
	if (s->off != kCbNone) {
		if (isOffscreen(s))
			call(s->off, self);
	} else if (s->offAct && isOffscreen(s) && s->offAct == 1) {
		deleteSprite(self);
	}
	if (!self)
		return;
	if (s->anim != kCbNone) {
		if (s->frameNo == s->trigFrame && s->timer == s->trigTimer)
			call(s->anim, self);
	} else if (s->animAct && s->frameNo == s->trigFrame && s->timer == s->trigTimer && s->animAct == 1) {
		deleteSprite(self);
	}
}

void BusGame::call(Callback cb, Sprite *&s) {
	switch (cb) {
	case kCbNone:
		break;
	case kCbPlayer:
		cbPlayer(s);
		break;
	case kCbPlayerAnimEnd:
		playerState(true);
		break;
	case kCbDelete:
		deleteSprite(s);
		break;
	case kCbBounce:
		cbBounce(s);
		break;
	case kCbArrow:
		cbArrow(s);
		break;
	case kCbPlatform:
		cbPlatform(s);
		break;
	case kCbBlock:
		cbBlock(s);
		break;
	case kCbLava:
		cbLava(s);
		break;
	case kCbCloud:
		cbCloud(s);
		break;
	case kCbNumber:
		// FUN_1000_5c86 points the shared NUMBER sequence at the digit
		s->cur = s->digit;
		break;
	case kCbLiz:
		cbLiz(s);
		break;
	case kCbBalloon:
		cbBalloon(s);
		break;
	case kCbMartian:
		cbMartian(s);
		break;
	}
}

// MOVEACTION: every sprite in list order, then the game's after-update hook
// (FUN_1000_1b4c, a pending screen change); the screen is drawn whole
void BusGame::moveAction() {
	for (Node *n = _head; n;) {
		_nextUpdate = n->next;
		if (!n->isLayer)
			updateSprite((Sprite *)n);
		n = _nextUpdate;
	}
	_nextUpdate = nullptr;
	if (_screenChange) {
		changeScreen();
		_screenChange = false;
	}
	for (Sprite *s : _garbage)
		delete s;
	_garbage.clear();
}

void BusGame::drawImage(Graphics::Surface &dst, const Image *img, int x0, int y0) {
	if (!img)
		return;
	for (int y = 0; y < img->h; y++) {
		int dy = y0 + y;
		if (dy < 0 || dy >= dst.h)
			continue;
		byte *out = (byte *)dst.getBasePtr(0, dy);
		const byte *src = img->pixels.data() + y * img->w, *mask = img->opaque.data() + y * img->w;
		for (int x = 0; x < img->w; x++) {
			int dx = x0 + x;
			if (dx >= 0 && dx < dst.w && mask[x])
				out[dx] = src[x];
		}
	}
}

void BusGame::render(Graphics::Surface &dst) {
	dst.fillRect(Common::Rect(dst.w, dst.h), _bgColor);
	for (Node *n = _head; n; n = n->next) {
		if (n->isLayer) {
			Layer *l = (Layer *)n;
			if (l->visible)
				drawImage(dst, l->img, l->x, l->y);
		} else {
			Sprite *s = (Sprite *)n;
			if (s->visible)
				drawImage(dst, s->cur->img, s->ix, s->iy);
		}
	}
}

// ---- Input and the main loop (the window procedure, 1000:1b66) ----

void BusGame::keyDown(Common::KeyCode key) {
	switch (key) {
	case Common::KEYCODE_SPACE:
		_keys |= kKeySpace;
		break;
	case Common::KEYCODE_LEFT:
		_keys = (_keys | kKeyLeft) & ~kKeyRight;
		break;
	case Common::KEYCODE_UP:
		if (_powerups & kPowerJetPack)
			_keys = (_keys | kKeyUp) & ~kKeyDown;
		break;
	case Common::KEYCODE_RIGHT:
		_keys = (_keys | kKeyRight) & ~kKeyLeft;
		break;
	default:
		break;
	}
}

void BusGame::keyUp(Common::KeyCode key) {
	switch (key) {
	case Common::KEYCODE_SPACE:
		_keys &= ~kKeySpace;
		break;
	case Common::KEYCODE_LEFT:
		_keys &= ~kKeyLeft;
		break;
	case Common::KEYCODE_UP:
		_keys &= ~kKeyUp;
		break;
	case Common::KEYCODE_RIGHT:
		_keys &= ~kKeyRight;
		break;
	case Common::KEYCODE_DOWN:
		_keys &= ~kKeyDown;
		break;
	default:
		break;
	}
}

void BusGame::charTyped(uint16 ascii) {
	if (ascii == 27)
		quit();
	else if (ascii == ' ' && !_running && _airShown == 0 && _result > 0)
		quit();
}

// The bus at the bottom right leaves; Liz talks when clicked after landing
void BusGame::mouseDown(int x, int y) {
	if (x > 0x1b0 && x < 0x1e5 && y > 0x1a9 && y < 0x1d6) {
		quit();
		return;
	}
	if (_lizState == 2 && _liz && _liz->x <= x && x <= _liz->w + _liz->x && _liz->y <= y && y <= _liz->h + _liz->y)
		lizTalk();
}

void BusGame::quit() {
	_done = true;
}

void BusGame::update(uint32 now) {
	_now = now;
	if (!_nextTick)
		_nextTick = now;
	updateMusic(now);
	// MM_WOM_DONE of the victory tune ends a won game
	if (_victoryEnd && now >= _victoryEnd && _lizState == 6) {
		quit();
		return;
	}
	int ticks = 0;
	while (!_done && now >= _nextTick && ticks++ < 5) {
		tick();
		_nextTick += kTickMs;
	}
	if (now >= _nextTick)
		_nextTick = now + kTickMs;
}

// WM_TIMER
void BusGame::tick() {
	if (!_running) {
		// The won game counts the air into the score
		if (_lizState == 6 && _airShown != 0) {
			if (_airShown >= 101) {
				_airShown -= 100;
				_score32 += 1000;
			} else if (_airShown >= 11) {
				_airShown -= 10;
				_score32 += 100;
			} else {
				_airShown -= 1;
				_score32 += 10;
			}
			showAir();
			showScore();
		}
	} else {
		_ticks++;
		_air -= 0.2f;
		if (ftol(_air) != _airShown) {
			_airShown = ftol(_air);
			showAir();
		}
	}
	moveAction();
}

// ---- The game (BUSGAME1.EX_) ----

BusGame::Object *BusGame::findObject(const Sprite *s) {
	for (int i = 0; i < _level.count; i++)
		if (_level.obj[i].spr == s && s)
			return &_level.obj[i];
	return nullptr;
}

bool BusGame::isPlatformType(const Object &o) {
	int t = o.w[kOType];
	return (t >= 4 && t <= 6) || t == 0xb;
}

// FUN_1000_054a: the object's rectangle from its sprite's first frame
void BusGame::setObjectRect(Object &o, int x, int y) {
	o.w[kOLeft] = x;
	o.w[kOTop] = y;
	if (o.spr) {
		o.w[kORight] = x + o.spr->seq->frames[0].img->w;
		o.w[kOBottom] = y + o.spr->seq->frames[0].img->h;
	}
}

void BusGame::resetPlatformCounters() {
	for (int i = 0; i < _level.count; i++)
		if (isPlatformType(_level.obj[i]) && _level.obj[i].w[kOType] != 0xb)
			_level.obj[i].w[kOReset] = 0;
}

// FUN_1000_0644: platforms go from (x1, y1) to (x2, y2) at their speed, or bob
void BusGame::setupMotion(Object &o) {
	Sprite *s = o.spr;
	float dy = o.w[kOY2] - o.w[kOY1], dx = o.w[kOX2] - o.w[kOX1];
	float m = dx >= dy ? dx : dy;
	float fx = 0, fy = 0;
	if (m != 0) {
		fx = dx / m;
		fy = dy / m;
	}
	if (dy == 0 && dx == 0) {
		int a = msRand() % 3;
		int b = msRand() % 3;
		s->vy = (a - b) * 0.5f;
		s->ay = 0.5f;
	} else {
		s->vy = o.w[kOSpeed] * fy;
		s->vx = o.w[kOSpeed] * fx;
		s->ay = 0;
	}
	if (o.w[kOSpeed] != 0) {
		if (o.w[kOReset] == 0) {
			o.w[kOReset] = o.w[kOCount] = ftol(m / o.w[kOSpeed]);
		} else {
			if (o.w[kOReset] < 0) {
				s->vx = -s->vx;
				s->vy = -s->vy;
			}
			return;
		}
	}
	s->x = o.w[kOX1];
	s->y = o.w[kOY1];
}

// FUN_1000_080a
void BusGame::createObject(Object &o) {
	int type = o.w[kOType];
	Common::String name;
	int martian = 0;
	if (type == 0x15) {
		if ((float)o.w[kOLeft] < _player->x) {
			name = "MARTIANRIGHT";
			martian = 2;
		} else {
			name = "MARTIANLEFT";
			martian = 1;
		}
	} else if (type > 0 && type < (int)ARRAYSIZE(kObjectTypes)) {
		name = kObjectTypes[type];
	}
	o.spr = createSprite(name);
	if (!o.spr)
		return;
	Sprite *s = o.spr;
	s->counter = 0;
	setObjectRect(o, o.w[kOLeft], o.w[kOTop]);
	s->x = o.w[kOLeft];
	s->y = o.w[kOTop];
	s->pri = 4;
	o.w[kOActive] = 1;
	switch (type) {
	case 1:
		s->move = kCbBlock;
		o.w[kOFlags] |= kFlagSolid | kFlagPushable | kFlagLandable;
		s->vy = 0.5f;
		s->pri = 2;
		break;
	case 2:
		s->offRect[3] = o.w[kOBottom] - s->seq->frames[0].img->h;
		s->off = kCbBounce;
		s->vx = 1.0f;
		break;
	case 3:
		if (_running) {
			s->vy = -5.0f;
			s->ay = 1.0f;
			s->offRect[3] = o.w[kOBottom] - s->seq->frames[0].img->h;
		}
		s->off = kCbBounce;
		break;
	case 4:
	case 5:
	case 6:
		s->move = kCbPlatform;
		s->pri = 2;
		o.w[kOFlags] |= kFlagSolid | kFlagLandable;
		if (o.w[kOReset] == 0) {
			s->x = o.w[kOX1];
			s->y = o.w[kOY1];
		}
		setupMotion(o);
		break;
	case 0xb:
		s->move = kCbBalloon;
		o.w[kOFlags] |= kFlagSolid | kFlagLandable;
		s->pri = 2;
		break;
	case 0xf:
		s->offMode = 0;
		s->offRect[1] = o.w[kOTop];
		s->offRect[2] = 640;
		s->offRect[3] = s->seq->frames[0].img->h + o.w[kOBottom];
		s->ay = 0;
		s->y = o.w[kOY2];
		s->vy = 0;
		if (o.w[kOReset] == 0)
			o.w[kOReset] = 50;
		o.w[kOCount] = o.w[kOReset];
		s->pri = 4;
		s->move = kCbLava;
		break;
	case 0x10:
	case 0x11:
	case 0x12:
	case 0x13:
		s->move = kCbCloud;
		s->pri = 9;
		s->vx = (s->x - 320.0f) * 0.1f;
		s->ay = 0;
		break;
	case 0x14:
		s->pri = 2;
		break;
	case 0x15:
		s->move = kCbMartian;
		s->counter = martian;
		s->pri = 3;
		break;
	case 0x16:
	case 0x17:
	case 0x18:
	case 0x19: {
		s->move = kCbArrow;
		int a = msRand() % 3;
		int b = msRand() % 3;
		s->vy = (float)(a - b) * -0.5f;
		s->ay = 0.5f;
		s->pri = 2;
		break;
	}
	default:
		break;
	}
	launchSprite(s);
	if (type == 0x14) {
		// A random first frame (FUN_1000_1746)
		s->frameNo = msRand() % s->frameCount + 1;
		setFrame(s);
	}
}

void BusGame::spawnObjects() {
	for (int i = 0; i < _level.count; i++)
		if (_level.obj[i].w[kOScreen] == _screen && _level.obj[i].w[kOPresent])
			createObject(_level.obj[i]);
}

// FUN_1000_0c50
void BusGame::deleteObjectSprite(Object &o) {
	if (!o.w[kOActive])
		return;
	if (o.spr == _riding) {
		_riding = nullptr;
		_grav = _planetGrav;
	}
	deleteSprite(o.spr);
	o.w[kOActive] = 0;
}

// FUN_1000_0c98: the balloon being ridden stays
void BusGame::deleteObjectSprites() {
	for (int i = _level.count - 1; i >= 0; i--) {
		Object &o = _level.obj[i];
		if (o.w[kOType] != 0xb || o.spr != _riding)
			deleteObjectSprite(o);
	}
}

// FUN_1000_0d30: the screen's background layer and its OVERLAYn sprite
void BusGame::showLayers() {
	for (int i = 1; i < 7; i++) {
		Layer *layer = nullptr;
		Common::String name = Common::String::format("BGLAYER%d", i);
		for (Layer *l : _layers)
			if (l->name == name)
				layer = l;
		if (!layer)
			return;
		if (i == _screen) {
			layer->visible = true;
			layer->pri = 2;
			_overlay = createSprite(Common::String::format("OVERLAY%d", i));
			if (!_overlay)
				return;
			launchSprite(_overlay);
		} else {
			layer->visible = false;
			layer->pri = 1;
		}
	}
}

// FUN_1000_0dfc: the objects remember where platforms got to, blocks go back home
void BusGame::changeScreen() {
	for (int i = 0; i < _level.count; i++) {
		Object &o = _level.obj[i];
		if (!o.w[kOActive])
			continue;
		if (isPlatformType(o)) {
			if (o.w[kOType] == 0xb && o.spr->vy != 0)
				o.w[kOPresent] = 0;
			setObjectRect(o, ftol(o.spr->x), ftol(o.spr->y));
		} else if (o.w[kOType] == 1) {
			o.w[kOLeft] = _backup.obj[i].w[kOLeft];
			o.w[kOTop] = _backup.obj[i].w[kOTop];
			setObjectRect(o, o.w[kOLeft], o.w[kOTop]);
		}
	}
	deleteObjectSprites();
	deleteSprite(_overlay);
	_screen = _pendingScreen;
	spawnObjects();
	showLayers();
	_floorLimit = 419.0f;
	if (_screen >= 1 && _screen <= 6 && _level.links[_screen - 1][1] == 0)
		_floorLimit = (float)(_player->seq->frames[0].img->h + 30) + 419.0f;

	// The floor under each column: the first solid cell, interpolated between columns
	_screenSolid = false;
	float cur = 840.0f;
	for (int c = 0; c < kGridW; c++) {
		int h = 840;
		for (int r = 0; r < kGridH; r++) {
			if (_screen >= 1 && _screen <= 6 && _level.grid[_screen - 1][r][c]) {
				_screenSolid = true;
				h = r * 8;
				break;
			}
		}
		float step;
		if ((uint)h > 420 || cur > 420.0f) {
			cur = (float)h;
			step = 0;
		} else {
			step = ((float)h - cur) * 0.125f;
		}
		for (int k = 0; k < 8; k++) {
			_heights[c * 8 + k] = ftol(cur);
			cur += step;
		}
		cur = (float)h;
	}
	playerState(true);
	_pendingScreen = -1;
}

void BusGame::requestScreen(int screen) {
	_pendingScreen = screen;
	_screenChange = true;
}

void BusGame::resetPhysics() {
	_vy = _vx = 0;
	_grav = _planetGrav;
}

// FUN_1000_131a
void BusGame::restartLevel() {
	_planetGrav = kGravity[_planetIndex];
	deleteSprite(_player);
	if (_liz) {
		deleteSprite(_liz);
		_lizState = 0;
	}
	deleteObjectSprites();
	_level = _backup;
	resetPhysics();
	// A placeholder standing right off-screen, which the new player sprite copies
	Sprite placeholder = Sprite();
	placeholder.x = placeholder.y = -200.0f;
	placeholder.state = 2;
	placeholder.dir = 2;
	_player = &placeholder;
	_riding = nullptr;
	playerState(false);
	requestScreen(_level.start[0]);
	_airShown = 999;
	_air = 999.0f;
	_powerups = 0;
	_score32 = 0;
	showScore();
	lizArrive();
	resetPlatformCounters();
}

void BusGame::setDigit(Sprite *s, int n) {
	const Sequence *seq = s->seq;
	s->digit = &seq->frames[CLIP<int>(n, 0, seq->frames.size() - 1)];
}

void BusGame::showScore() {
	int32 v = _score32;
	bool blank = false;
	for (int i = 5; i >= 0; i--) {
		setDigit(_score[i], blank ? 10 : v % 10);
		v /= 10;
		if (v == 0)
			blank = true;
	}
}

void BusGame::showAir() {
	if (_airShown < 1) {
		_airShown = 0;
		lizArrive();
	}
	int32 v = _airShown;
	bool blank = false;
	for (int i = 2; i >= 0; i--) {
		setDigit(_airDigits[i], blank ? 10 : v % 10);
		v /= 10;
		if (v == 0)
			blank = true;
	}
}

void BusGame::stopPlatforms() {
	for (int i = 0; i < _level.count; i++) {
		Object &o = _level.obj[i];
		if (o.w[kOActive] && isPlatformType(o))
			o.spr->vx = o.spr->vy = o.spr->ay = 0;
	}
}

// FUN_1000_2f18: Liz flies in with the player at the start, or to fetch them
// when the air has run out
void BusGame::lizArrive() {
	if (_lizState != 2 && _lizState != 0)
		return;
	_running = false;
	_lizCarry = false;
	_vy = _vx = 0;
	Sprite *l = createSprite("LIZLAND");
	if (!l)
		return;
	float lx, ly;
	if (_lizState == 0) {
		ly = 420.0f;
		if (420.0f <= _player->y)
			lx = _player->x;
		else
			lx = (float)(640 - l->seq->frames[0].img->w);
	} else {
		lx = _liz->x;
		ly = _liz->y;
		deleteSprite(_liz);
	}
	l->y = ly;
	l->x = lx;
	_liz = l;
	if (_air < 1.0f) {
		_lizTX = _player->x;
		_lizTY = _player->y;
		stopPlatforms();
		_riding = nullptr;
		_lizState = 5;
	} else {
		_lizTX = _level.start[1];
		_lizTY = _level.start[2];
		_lizCarry = true;
		_lizState = 3;
		_fellOff = false;
	}
	_grav = 0;
	playerState(true);
	_grav = 0;
	_lizTY += _player->seq->frames[0].dy;
	float dx = _lizTX - l->x, dy = _lizTY - l->y;
	float f = (float)sqrt(dx * dx + dy * dy) * 0.1f;
	l->vx = dx / f;
	_lizVy = dy / f;
	l->vy = _fellOff ? 0 : _lizVy;
	if (_fellOff) {
		l->vx *= 2.0f;
		l->vy *= 2.0f;
	}
	l->move = kCbLiz;
	l->pri = _player->pri + 1;
	_liz = l;
	launchSprite(l);
}

// FUN_1000_319c
void BusGame::jetFollow() {
	float off = _player->dir == 1 ? 24.0f : 1.0f;
	_jet->x = _player->cur->dx + _player->x + off;
	_jet->y = _player->cur->dy + _player->y + 80.0f;
}

void BusGame::deleteJet() {
	if (_jet) {
		deleteSprite(_jet);
		stopChannel(2);
	}
}

// FUN_1000_3246: the player's next state and its sprite
void BusGame::playerState(bool replace) {
	_stateChanged = true;
	Sprite *old = _player;
	int oldState = old->state, newState = oldState;
	if (_airShown < 1)
		_keys = 0;
	if (_keys & kKeyLeft) {
		if (old->y < 420.0f)
			_target = -10.0f;
	} else if (_keys & kKeyRight) {
		if (old->y < 420.0f)
			_target = 10.0f;
	} else {
		_target = 0;
	}
	if (_level.start[3] != 2 || _grav != 0 || (_powerups & kPowerTracBoots))
		_vx = 0.5f * _target;
	bool jumped = false;
	if ((_keys & kKeySpace) && !(_prevKeys & kKeySpace) && _grav == 0) {
		_air -= 20.0f;
		playSound(kSoundJump);
		_grav = _planetGrav;
		jumped = true;
		if (_riding) {
			Object *o = findObject(_riding);
			if (o && !isPlatformType(*o))
				_riding = _riding->restOn;
			else if (o && o->w[kOType] == 0xb)
				_riding->vy = -5.0f;
			if (_riding)
				_vx += _riding->vx;
		}
		_riding = nullptr;
	}
	int oldDir = old->dir, newDir = oldDir;
	if (_target < 0)
		newDir = 1;
	else if (_target > 0)
		newDir = 2;

	if (_pushFlag)
		newState = 7;
	else if ((_keys & kKeyUp) && (_powerups & kPowerJetPack))
		newState = 9;
	else if (jumped)
		newState = 3;
	else if (oldState == 3)
		newState = (_keys & kKeySpace) ? 4 : 8;
	else if (_grav != 0)
		newState = 8;
	else if (oldState == 4) {
		newState = 5;
		newDir = oldDir;
	} else if (_target == 0)
		newState = 2;
	else if (newDir != oldDir)
		newState = 6;
	else
		newState = 1;
	_prevKeys = _keys;

	float nx = old->x, ny = old->y;
	if (oldState == 9 && newState != 9) {
		deleteJet();
		_grav = _planetGrav;
	}
	Common::String name;
	Callback move = kCbNone, anim = kCbNone;
	bool newJet = false;
	switch (newState) {
	case 1:
		name = "MANWALK";
		move = kCbPlayer;
		break;
	case 3:
		_vy = (_powerups & kPowerBoots) ? -13.0f : -10.0f;
		name = "STARTJUMP";
		anim = kCbPlayerAnimEnd;
		break;
	case 4:
		name = "MANJUMP";
		move = kCbPlayer;
		break;
	case 5:
		name = "STOPJUMP";
		anim = kCbPlayerAnimEnd;
		break;
	case 6:
		name = "MANTURN";
		anim = kCbPlayerAnimEnd;
		break;
	case 7:
		name = "PUSH";
		move = kCbPlayer;
		break;
	case 8:
		name = "FREEFALL";
		move = kCbPlayer;
		break;
	case 9:
		if (!_jet) {
			newJet = true;
			playSound(kSoundJet);
		}
		_vy = -4.0f;
		if (_riding) {
			Object *o = findObject(_riding);
			if (o && o->w[kOType] == 0xb)
				_riding->vy = -5.0f;
		}
		_riding = nullptr;
		// fall through
	default:
		name = "MANSTAND";
		move = kCbPlayer;
		break;
	}
	name += newDir == 1 ? "LEFT" : "RIGHT";

	Sprite *p = createSprite(name);
	if (!p)
		return;
	_player = p;
	if (_pushFlag) {
		float w = p->seq->frames[0].img->w;
		if (newDir == 2)
			nx = (1.0f - w) + nx;
		nx = nx - p->seq->frames[0].dx;
		_pushFlag = false;
		if (newDir == 2) {
			if (_pushing->w + _pushing->x >= 640.0f) {
				nx = nx - (_pushing->w + _pushing->x - 640.0f);
				_pushing->x = (float)(640 - _pushing->w);
			}
		} else if (_pushing->x < 0) {
			nx = nx - _pushing->x;
			_pushing->x = 0;
		}
	}
	p->x = nx;
	p->pri = 6;
	p->y = ny;
	float snap = ny + 1.0f;
	p->trigFrame = anim != kCbNone ? p->frameCount : 1;
	p->anim = anim;
	p->move = move;
	p->state = newState;
	p->dir = newDir;
	if (replace && _grav == 0)
		p->y = (float)(ftol(snap) - 1);
	launchSprite(p);
	if (newJet) {
		_jet = createSprite("JET");
		if (!_jet)
			return;
		_jet->x = old->x;
		_jet->y = old->y;
		_jet->pri = 8;
		jetFollow();
		launchSprite(_jet);
	}
	if (replace)
		deleteSprite(old);
}

void BusGame::ouch() {
	if (_ticks - _lastOuch > 50) {
		_lastOuch = _ticks;
		playSound(kSoundOuch);
	}
}

// FUN_1000_3c6e: the player touches an object; always returns 0
int BusGame::touch(Object &o) {
	bool take = true;
	switch (o.w[kOType]) {
	case 1:
	case 0x15:
	case 0x16:
	case 0x17:
	case 0x18:
	case 0x19:
		take = false;
		break;
	case 2:
		_score32 += 500;
		playSound(kSoundStar);
		showScore();
		break;
	case 3:
		_result = 1;
		_score32 += 1000;
		showScore();
		_running = false;
		_lizState = 6;
		_vx = _vy = 0;
		playerState(true);
		playSound(kSoundVictory);
		break;
	case 7:
	case 8:
	case 9:
	case 0xa:
	case 0xc:
	case 0xd:
	case 0xe:
		if (o.w[kOType] == 7)
			_powerups |= kPowerJetPack;
		else if (o.w[kOType] == 8)
			_powerups |= kPowerTracBoots;
		else if (o.w[kOType] == 9)
			_powerups |= kPowerFire;
		else if (o.w[kOType] == 0xa)
			_powerups |= kPowerBoots;
		else if (o.w[kOType] == 0xd)
			_powerups |= kPowerSnowman;
		else if (o.w[kOType] == 0xc)
			_air = MIN(_air + 200.0f, 999.0f);
		else {
			_planetGrav += 0.32f;
			if (_grav != 0)
				_grav = _planetGrav;
		}
		_score32 += 100;
		showScore();
		break;
	case 0xf: {
		take = false;
		float c = (float)(o.spr->w / 2) + o.spr->x;
		if (c <= _player->x || _player->w + _player->x <= c)
			break;
	}
		// fall through
	case 0x14:
		take = false;
		if (!(_powerups & kPowerSnowman)) {
			_air -= 0.5f;
			ouch();
		}
		break;
	case 0x10:
	case 0x11:
	case 0x12:
	case 0x13:
		take = false;
		_air -= 0.5f;
		ouch();
		break;
	default:
		break;
	}
	if (take) {
		if (o.w[kOType] != 2 && o.w[kOType] != 3)
			playSound(kSoundGrab);
		o.w[kOPresent] = 0;
		deleteObjectSprite(o);
	}
	return 0;
}

// FUN_1000_3e66: moves the pushed block; false when it starts to fall
bool BusGame::pushBlock(float dx) {
	Sprite *b = _pushing;
	bool ok = true, blocked = false;
	int probe = 0;
	if (dx > 0)
		probe = ftol(b->w + b->x);
	else if (dx < 0)
		probe = ftol(b->x) - 1;
	float c = (float)(b->w / 2) + b->x;
	for (int i = 0; i < _level.count; i++) {
		Object &o = _level.obj[i];
		Sprite *s = o.spr;
		if (s == b || !o.w[kOActive] || o.w[kOType] != 1)
			continue;
		if (s->h + s->y != b->y || c < s->x || s->w + s->x < c) {
			if (s->y == b->y) {
				if (dx >= 0) {
					float f = s->x + 1.0f;
					if ((float)probe < f && f - probe <= dx) {
						b->x = (f - b->w) - 1.0f;
						blocked = true;
						break;
					}
				} else {
					float f = (s->w + s->x) - 1.0f;
					if (f <= (float)probe && dx < f - probe) {
						b->x = f + 2.0f;
						blocked = true;
						break;
					}
				}
			}
		} else {
			blocked = true;
		}
	}
	if (!blocked) {
		b->x += dx;
		if (landOnPlatform(b) == 0 && gridFloor(b) == 0) {
			b->vy = _planetGrav;
			b->restOn = nullptr;
			ok = false;
		}
	}
	if (b->x >= 0) {
		if (b->w + b->x >= 640.0f)
			b->x = (float)(640 - b->w);
	} else {
		b->x = 0;
	}
	Object *o = findObject(b);
	if (o)
		setObjectRect(*o, ftol(b->x), ftol(b->y));
	return ok;
}

float BusGame::centerX(const Sprite *s) const {
	return (float)s->cur->dx + (float)(s->w / 2) + s->x;
}

// FUN_1000_56da: the top of a block or platform the falling block lands on
float BusGame::landOnPlatform(Sprite *s) {
	float top = s->y + s->vy;
	int h = s->h;
	float c = centerX(s);
	for (int i = 0; i < _level.count; i++) {
		Object &o = _level.obj[i];
		if (o.spr == s || !o.w[kOActive] || !(o.w[kOFlags] & kFlagSolid))
			continue;
		Sprite *p = o.spr;
		if (p->x <= c && c <= p->w + p->x && top < p->y && p->y <= h + top) {
			float y = p->y;
			if (!(o.w[kOFlags] & kFlagPushable)) {
				s->restOn = p;
			} else if (p->vy == 0) {
				o.w[kOFlags] ^= kFlagLandable;
				return y;
			}
		}
	}
	return 0;
}

// FUN_1000_5844: the grid floor under a falling block
float BusGame::gridFloor(Sprite *s) {
	int r0 = ftol(s->y) / 8;
	int r1 = ftol(s->h + s->y + s->vy) / 8;
	int c = ftol(centerX(s)) / 8;
	for (int r = r0 + 1; r <= r1; r++) {
		int idx = ((_screen - 1) * kGridH + r) * kGridW + c;
		if (idx >= 0 && idx < (int)sizeof(_level.grid) && ((const byte *)_level.grid)[idx])
			return (float)(r * 8);
	}
	return 0;
}

// FUN_1000_418a: the player's move callback
void BusGame::cbPlayer(Sprite *&selfRef) {
	if (!_running || _airShown < 0)
		return;
	_stateChanged = false;
	Sprite *orig = selfRef, *self = selfRef;
	Sprite snapshot;
	int dir = orig->dir;
	if (_prevKeys != _keys) {
		bool accepted = false;
		if (_grav != 0) {
			uint32 changed = _prevKeys ^ _keys, now = changed & _keys;
			if ((changed & kKeyRight) && dir == 2) {
				_vx = (now & kKeyRight) ? 5.0f : 0;
				changed ^= kKeyRight;
			}
			if ((changed & kKeyLeft) && dir == 1) {
				_vx = (now & kKeyLeft) ? -5.0f : 0;
				changed ^= kKeyLeft;
			}
			if ((changed & kKeySpace) && orig->state != 4 && orig->state != 3)
				changed ^= kKeySpace;
			if (changed == 0)
				accepted = true;
		}
		snapshot = *orig;
		self = &snapshot;
		if (!accepted) {
			playerState(true);
			return;
		}
		_prevKeys = _keys;
	}
	if (_grav == 0) {
		float d = _target - _vx;
		if (d < 0)
			d = -0.5f;
		else if (d > 0)
			d = 0.5f;
		if (d != 0)
			_vx += d;
	}

	if (orig->state == 7) {
		float old = _pushing->x;
		if (pushBlock(0.5f * _vx)) {
			_player->x += _pushing->x - old;
		} else {
			_pushing = nullptr;
			playerState(true);
		}
	} else {
		float sp = fabs(_vx);
		if (fabs(_vy) > sp)
			sp = fabs(_vy);
		Sprite *p = _player;
		float left = p->cur->dx + p->x;
		float right = (p->w + left) - 1.0f;
		float feet = p->y;
		float head = p->cur->dy + feet;
		if (sp == 0) {
			for (int i = 0; i < _level.count; i++) {
				Object &o = _level.obj[i];
				if (!o.w[kOActive] || (o.w[kOFlags] & kFlagSolid))
					continue;
				Sprite *s = o.spr;
				if (s->cur->dy + s->h + s->y - head >= 5.0f && s->cur->dy + s->y - feet <= -5.0f &&
				    s->cur->dx + s->x - right <= -5.0f && s->cur->dy + s->w + s->x - left >= 5.0f)
					touch(o);
			}
		} else {
			float stepx = _vx / sp, stepy = _vy / sp;
			float cx = p->cur->dx + (float)(p->w / 2) + p->x;
			float edgeX = _vx < 0 ? left : right;
			int t = ftol(sp);
			int n = ABS(t);
			if (t & 7)
				n++;
			bool hit = false, grounded = false;
			for (int i = 0; i < n; i++) {
				p = _player;
				if (!_riding) {
					if (_grav == 0 && !_jet) {
						int c = CLIP(ftol(cx), 0, 639);
						if (_heights[c] < 420) {
							p->y = (float)_heights[c] - 1.0f;
							grounded = true;
						}
						if (!grounded) {
							for (int k = 0; k < 12; k += 4) {
								int c2 = c - k;
								if (!(c2 >= 0 && _heights[c2] < 420)) {
									c2 = c + k;
									if (!(c2 < 640 && _heights[c2] < 420))
										continue;
								}
								p->y = (float)_heights[c2] - 1.0f;
								grounded = true;
								break;
							}
						}
					} else if (_vy >= 0) {
						int c = CLIP(ftol(cx), 0, 639);
						int hc = _heights[c];
						int cl = MAX(0, ftol(left));
						int hl = _heights[MIN(cl, 639)];
						int cr = MIN(639, ftol(right));
						int hr = _heights[MAX(cr, 0)];
						if (ABS(ftol(feet) - hc) < 2 && hc < 420) {
							stepx = stepy = 0;
							_grav = 0;
							feet = (float)hc;
						} else {
							if ((float)hr > 420.0f && stepx < 0 && (float)hl < feet) {
								for (int k = cl; k < 640 && _heights[k] < hc; k++)
									_player->x += 1.0f;
								stepx = 0;
								_grav = 0;
							} else if ((float)hl > 420.0f && stepx > 0 && (float)hr < feet) {
								for (int k = cr; k >= 0 && _heights[k] < hc; k--)
									_player->x -= 1.0f;
								stepx = 0;
								_grav = 0;
							}
						}
						if (_grav == 0) {
							feet = (float)(hc - 1);
							_player->y = feet;
							head = feet - _player->h;
							_vx = _vy = 0;
							grounded = hit = true;
							playerState(true);
						}
					}
					if (_grav == 0 && !grounded) {
						_vy = _grav = _planetGrav;
						playerState(true);
					}
				}
				p = _player;
				for (int j = 0; j < _level.count; j++) {
					Object &o = _level.obj[j];
					if (!o.w[kOActive])
						continue;
					Sprite *s = o.spr;
					int fl = o.w[kOFlags];
					if ((fl & (kFlagSolid | kFlagLandable)) == (kFlagSolid | kFlagLandable) || ((fl & kFlagSolid) && _grav == 0)) {
						if (_vy > 0 && fabs(feet - (s->cur->dy + s->y)) < 5.0 && s->x <= cx && cx <= s->w + s->x) {
							_riding = s;
							if (o.w[kOType] == 0xb) {
								s->vy = 3.0f;
								s->counter = 100;
							}
							stepy = 0;
							_player->y = s->y;
							_vy = _grav = 0;
							_player->x = (cx - _player->cur->dx) - (float)(_player->w / 2);
							playerState(true);
							hit = true;
						}
					} else {
						if (s->cur->dy + s->h + s->y - head >= 5.0f && s->cur->dy + s->y - feet <= -5.0f &&
						    s->cur->dx + s->x - right <= -5.0f && s->cur->dy + s->w + s->x - left >= 5.0f) {
							hit = touch(o) != 0;
							if (!hit)
								continue;
						}
					}
					if (hit)
						break;
					if (_vx != 0) {
						bool inX = (_vx > 0 && s->x <= edgeX && edgeX < s->w + s->x) ||
							(_vx < 0 && s->x <= edgeX && edgeX <= s->w + s->x);
						if (inX && s->cur->dy + s->y <= feet && head < s->y) {
							if ((fl & kFlagPushable) && s != _riding) {
								hit = true;
								if (_grav != 0 || s->vy != 0 || _target == 0 || (_target < 0 && _vx > 0) || (_target > 0 && _vx < 0)) {
									_vx = 0;
								} else {
									_pushFlag = true;
									_player->x = s->x;
									if (_vx > 0)
										_player->x = _player->x - 1.0f;
									else
										_player->x = s->w + _player->x;
									_pushing = s;
									playerState(true);
								}
							} else if (!(fl & kFlagSolid)) {
								hit = touch(o) != 0;
							}
						}
					}
					if (hit)
						break;
				}
				if (hit)
					break;
				cx += stepx;
				edgeX += stepx;
				feet += stepy;
				head += stepy;
			}
			if (!hit) {
				_player->y += _vy;
				_player->x += _vx;
			}
			if (_riding && (cx < _riding->x || _riding->w + _riding->x <= cx)) {
				_grav = _planetGrav;
				_vy = _riding->vy;
				Object *o = findObject(_riding);
				if (o && o->w[kOType] == 0xb)
					_riding->vy = -5.0f;
				_riding = nullptr;
			}
		}
	}

	int l = ftol(self->cur->dx + self->x);
	int r = l + self->cur->img->w;
	if (orig->state == 9)
		_air -= 2.5f;
	else if (_vy < 20.0f)
		_vy += _grav;
	if (!_stateChanged) {
		Sprite *p = _player;
		const int16 *links = _level.links[CLIP(_screen, 1, 6) - 1];
		int next = 0;
		bool edge = false;
		if (l <= 0) {
			if (!links[2]) {
				p->x = (float)-p->cur->dx;
			} else {
				p->x = (float)(0x26c - p->cur->dx - p->cur->img->w);
				next = links[2];
				edge = true;
			}
		} else if (r >= 640) {
			if (!links[3]) {
				p->x = (float)(640 - p->cur->dx - p->cur->img->w);
			} else {
				p->x = (float)(0x1e - p->cur->dx);
				next = links[3];
				edge = true;
			}
		}
		bool done = false;
		if (edge) {
			_screen = next;
			links = _level.links[CLIP(_screen, 1, 6) - 1];
			if (p->y < 0 && links[0]) {
				_screen = links[0];
				requestScreen(_screen);
				p->y = 419.0f;
				done = true;
			} else {
				requestScreen(_screen);
			}
		}
		if (!done) {
			if (p->y < 0) {
				if (links[0]) {
					_screen = links[0];
					requestScreen(_screen);
					p->y = 419.0f;
				}
			} else if (p->y > _floorLimit) {
				if (!links[1]) {
					_air = -1.0f;
					p->y = _floorLimit - 1.0f;
					_vx = _vy = _grav = 0;
					if (420.0f < _floorLimit)
						_fellOff = true;
				} else {
					_screen = links[1];
					requestScreen(_screen);
					p->y = 1.0f;
					if (_riding)
						_riding->y = p->y;
				}
			} else if (p->y > 420.0f && !links[1] && _screenSolid) {
				_vx = 0;
			}
		}
	}
	if (_jet)
		jetFollow();
}

// FUN_1000_5386: stars and coins hop back up from where they started
void BusGame::cbBounce(Sprite *&s) {
	s->y = s->offRect[3];
	s->vy = -5.0f;
	s->vx = -s->vx;
}

void BusGame::cbArrow(Sprite *&s) {
	if (s->vy > 1.0f || s->vy < -1.0f)
		s->ay = -s->ay;
}

// FUN_1000_54a8: platforms carry the player and the blocks resting on them
void BusGame::cbPlatform(Sprite *&s) {
	Object *o = findObject(s);
	if (s == _riding) {
		_player->y = s->y;
		_player->x = s->vx + _player->x;
	}
	if (!o)
		return;
	if (o->w[kOSpeed] == 0) {
		if (s->vy > 1.5f || s->vy < -1.5f)
			s->ay = -s->ay;
	} else if (o->w[kOCount] == 0) {
		s->vx = -s->vx;
		s->vy = -s->vy;
		int16 v = o->w[kOReset];
		o->w[kOReset] = -v;
		o->w[kOCount] = -v;
	} else if (o->w[kOCount] < 0) {
		o->w[kOCount]++;
	} else {
		o->w[kOCount]--;
	}
	for (int i = 0; i < _level.count; i++) {
		Object &b = _level.obj[i];
		if (b.w[kOActive] && b.w[kOType] == 1 && b.spr->restOn == s) {
			b.spr->y = (s->y - b.spr->cur->dy) - b.spr->h;
			setObjectRect(b, ftol(b.spr->x), ftol(b.spr->y));
			if (b.spr == _riding)
				_player->y = b.spr->y;
		}
	}
}

// FUN_1000_5924: blocks fall until they land (the sprite moves by vy too)
void BusGame::cbBlock(Sprite *&s) {
	int h = s->h;
	float y = s->y, vy = s->vy;
	if (s->vy == 0)
		return;
	float land = landOnPlatform(s);
	bool landed = land != 0;
	float bottom = (float)h + y + vy;
	if (landed)
		bottom = land;
	float g = gridFloor(s);
	if (g != 0 && g <= bottom) {
		landed = true;
		bottom = g;
	}
	s->y = bottom - (float)s->h;
	if (landed) {
		s->vy = 0;
	} else {
		s->vy = _planetGrav + s->vy;
		if (s->vy > 20.0f)
			s->vy = 20.0f;
	}
	Object *o = findObject(s);
	if (o)
		setObjectRect(*o, ftol(s->x), ftol(s->y));
}

// FUN_1000_5a82: lava rises from y2 to y1 and sinks back, then waits
void BusGame::cbLava(Sprite *&s) {
	if (!_running) {
		s->vy = 0;
		return;
	}
	Object *o = findObject(s);
	if (!o)
		return;
	if (o->w[kOCount] != 0) {
		if (--o->w[kOCount] != 0)
			return;
		s->ay = 1.0f;
		int c = o->w[kOY2], e = -1;
		do {
			c += e;
			e--;
		} while (o->w[kOY1] <= c);
		s->vy = (float)e;
	}
	if (s->vy < 0) {
		if (s->vy == -1.0f)
			s->ay = 0;
		if (s->y + s->vy < (float)o->w[kOY1])
			s->vy = (float)o->w[kOY1] - s->y;
	}
	if (s->y <= (float)o->w[kOY1]) {
		s->y = o->w[kOY1];
		s->vy = 0;
		s->ay = 1.0f;
		return;
	}
	if ((float)o->w[kOY2] < s->y) {
		s->y = o->w[kOY2];
		s->vy = 0;
		s->ay = 0;
		o->w[kOCount] = o->w[kOReset];
	}
}

void BusGame::cbCloud(Sprite *&s) {
	if (s->x > 640.0f)
		s->x = (float)-s->w;
	else if (s->x < (float)-s->w)
		s->x = 640.0f;
}

// FUN_1000_5cde: Liz flies to her target; on arrival the next leg of the rescue
void BusGame::cbLiz(Sprite *&self) {
	Sprite *s = self;
	float lx = s->x, ly = s->y;
	if (!_lizCarry) {
		if (_lizState == 2 || _lizState == 6) {
			if (s->vy > 1.5f || s->vy < -1.5f)
				s->ay = -s->ay;
			return;
		}
	} else {
		_player->x = lx;
		_player->y = ly - _player->cur->dy;
	}
	if (fabs(s->vx) <= fabs(_lizTX - s->x) && fabs(s->vy) <= fabs(_lizTY - s->y))
		return;
	if (_fellOff && _lizState == 5 && s->vy == 0) {
		s->vy = 2.0f * _lizVy;
		s->vx = 0;
		return;
	}
	deleteSprite(self);
	_liz = nullptr;
	Common::String name;
	if (_lizState == 1) {
		_lizState = 3;
		name = "LIZLAND";
		if (_level.start[0] != _screen) {
			lx = 640.0f;
			ly = 0;
		}
		restartLevel();
		_lizCarry = true;
		_lizTX = _level.start[1];
		_lizTY = (float)(_level.start[2] - _player->h);
	} else if (_lizState == 3) {
		_lizCarry = false;
		_lizTX = _lizTY = 10.0f;
		_lizState = 4;
		resetPhysics();
		playerState(true);
		name = "LIZLAND";
	} else if (_lizState == 4) {
		_lizState = 2;
		name = "LIZLAND";
		_lizTX = lx;
		_lizTY = ly;
	} else if (_lizState == 5) {
		_lizTX = _lizTY = 0;
		if (_fellOff) {
			_lizTX = lx;
			if (_level.start[0] == _screen)
				_lizTY = 210.0f;
		}
		_lizState = 1;
		name = "LIZFLY";
		_fellOff = false;
		_lizTY -= _player->h;
		_lizCarry = true;
	} else {
		return;
	}
	Sprite *l = createSprite(name);
	if (!l)
		return;
	_liz = l;
	if (_jet)
		deleteJet();
	l->x = lx;
	l->y = ly;
	if (_lizState == 2) {
		l->vx = l->vy = 0;
		l->ay = 0.5f;
	} else {
		float dx = _lizTX - l->x, dy = _lizTY - l->y;
		float f = (float)sqrt(dx * dx + dy * dy) * 0.05f;
		l->vx = dx / f;
		l->vy = dy / f;
		if (_fellOff) {
			l->vx *= 2.0f;
			l->vy *= 2.0f;
		}
	}
	l->move = kCbLiz;
	l->pri = _lizState == 2 ? 4 : 7;
	launchSprite(l);
	if (_air > 0 && !_lizCarry) {
		_running = true;
		if (_lizIntro) {
			lizTalk();
			_lizIntro = false;
		}
	}
}

// FUN_1000_6194: a balloon sinks while stood on, flickers and pops
void BusGame::cbBalloon(Sprite *&self) {
	Sprite *s = self;
	if (s->y < -100.0f) {
		Object *o = findObject(s);
		deleteSprite(self);
		if (o) {
			o->spr = nullptr;
			o->w[kOActive] = 0;
		}
		return;
	}
	if (s->vy != 0) {
		if (s->counter == 0) {
			playSound(kSoundBalloon);
			Object *o = findObject(s);
			deleteSprite(self);
			if (o) {
				o->spr = nullptr;
				o->w[kOActive] = 0;
			}
			if (s == _riding) {
				_riding = nullptr;
				_grav = _planetGrav;
				playerState(true);
			}
		} else if (--s->counter < 50) {
			s->x = -s->x;
		}
	}
	if (s == _riding) {
		_player->y = s->y;
		_player->x = s->vx + _player->x;
	}
}

// FUN_1000_62aa: martians turn to look at the player
void BusGame::cbMartian(Sprite *&self) {
	Sprite *s = self;
	int turn = 0;
	if (s->counter == 1) {
		if (s->x < _player->x)
			turn = 2;
	} else if (_player->x < s->x) {
		turn = 1;
	}
	if (!turn)
		return;
	Sprite *n = createSprite(turn == 2 ? "MARTIANRIGHT" : "MARTIANLEFT");
	if (!n)
		return;
	n->x = s->x;
	n->y = s->y;
	n->counter = turn;
	n->pri = 3;
	n->move = kCbMartian;
	Object *o = findObject(s);
	Sprite *old = s;
	deleteSprite(old);
	if (o)
		o->spr = n;
	launchSprite(n);
}

// ---- Sound ----

void BusGame::playSound(int n) {
	if (n < 0 || n >= (int)_effects.size() || _effects[n].empty())
		return;
	int channel;
	bool loop = false;
	if (n == kSoundJet) {
		channel = 2;
		loop = true;
	} else if (n == kSoundVictory) {
		channel = 3;
		stopChannel(1);
		stopChannel(2);
		_music = nullptr;
		_musicRestart = 0;
		_songOrder.clear();
	} else {
		channel = 0;
	}
	stopChannel(channel);
	Audio::SeekableAudioStream *s = makeWave(_effects[n].data(), _effects[n].size());
	if (!s)
		return;
	if (n == kSoundVictory)
		_victoryEnd = _now + s->getLength().msecs();
	Audio::AudioStream *as = loop ? (Audio::AudioStream *)Audio::makeLoopingAudioStream(s, 0) : s;
	_mixer->playStream(Audio::Mixer::kSFXSoundType, &_channels[channel], as);
}

void BusGame::stopChannel(int channel) {
	_mixer->stopHandle(_channels[channel]);
}

void BusGame::lizTalk() {
	const Common::Array<byte> &w = _effects.size() > kSoundLiz ? _effects[kSoundLiz] : Common::Array<byte>();
	if (w.empty())
		return;
	stopChannel(3);
	Audio::SeekableAudioStream *s = makeWave(w.data(), w.size());
	if (s)
		_mixer->playStream(Audio::Mixer::kSpeechSoundType, &_channels[3], s);
}

// The song plays its parts in the order of its string, then pauses a second
// (the one-shot timer 0x2a) and starts again
void BusGame::startSong() {
	if (_songOrder.empty())
		return;
	_music = Audio::makeQueuingAudioStream(11025, false);
	bool any = false;
	for (uint i = 0; i < _songOrder.size(); i++) {
		int part = _songOrder[i] - '0';
		if (part < 0 || part >= (int)_songParts.size() || !_songParts[part])
			continue;
		Audio::SeekableAudioStream *s = makeWave(_songParts[part], _songSizes[part]);
		if (s && (s->getRate() != 11025 || s->isStereo())) {
			warning("BUSGAME: song part %d isn't 11 kHz mono", part);
			delete s;
			s = nullptr;
		}
		if (s) {
			_music->queueAudioStream(s, DisposeAfterUse::YES);
			any = true;
		}
	}
	_music->finish();
	if (!any) {
		delete _music;
		_music = nullptr;
		return;
	}
	_mixer->playStream(Audio::Mixer::kMusicSoundType, &_channels[1], _music);
}

void BusGame::updateMusic(uint32 now) {
	if (_songOrder.empty())
		return;
	if (_music && !_mixer->isSoundHandleActive(_channels[1])) {
		_music = nullptr;
		_musicRestart = now + 1000;
	}
	if (!_music && _musicRestart && now >= _musicRestart) {
		_musicRestart = 0;
		startSong();
	}
}

} // End of namespace MSPAM
