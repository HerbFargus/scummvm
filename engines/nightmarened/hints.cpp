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

#include "common/config-manager.h"
#include "common/debug.h"
#include "common/events.h"
#include "common/system.h"
#include "common/file.h"
#include "common/textconsole.h"
#include "graphics/paletteman.h"
#include "image/png.h"

#include "nightmarened/detection.h"
#include "nightmarened/display.h"
#include "nightmarened/game.h"
#include "nightmarened/hints.h"
#include "nightmarened/media.h"
#include "nightmarened/ned.h"
#include "nightmarened/nightmarened.h"
#include "nightmarened/rdx.h"

namespace NightmareNed {

enum {
	kHintArea = 9,       // PTR_DAT_004afcd4
	kNafHints = 4,       // ned92000.naf
	kNafOptions = 2,     // ned86000.naf
	kHintScene = 1,
	kSwirl = 0x32,
	kHintZ = 2,
	kNedList = 0x14,     // Ned's level-3 action sprites
	kCreditsArea = 1,
	kCreditsSlot = 0x95,
	kCreditsBgList = 0x4b0980,
	kCreditsSprList = 0x4b095c,
	kCreditsMusic = 0x82,
	kEndClipShort = 0x30,
	kEndClipLong = 0x2f
};

// The lists 0x475300 loads and 0x475630 frees, by world (DAT_0051fc70) and level - 1. Entry 0
// is the world's palette bitmap (8x1, Windows static colours), then slot 50, its sound, the
// vision's sprites and its lines.
static const struct {
	uint32 va;
	int count;
} kLists[Hints::kWorlds][3] = {
	{ { 0x4b3578, 4 }, { 0x4b3608, 4 }, { 0x4b3698, 0x0b } },
	{ { 0x4b4110, 4 }, { 0x4b41a0, 4 }, { 0x4b4230, 0x10 } },
	{ { 0x4b3ad8, 4 }, { 0x4b3b68, 4 }, { 0x4b3bf8, 0x0f } },
	{ { 0x4b3e18, 4 }, { 0x4b3ea8, 4 }, { 0x4b3f38, 0x0d } },
	{ { 0x4b3828, 4 }, { 0x4b38b8, 4 }, { 0x4b3948, 0x0b } }
};

Hints::Hints(Game *game) : _game(game), _hooked(false), _scene(-1), _world(0), _level(0), _facing(0),
	_xform(nullptr), _xformUser(nullptr), _optionsWere(false), _naf(false), _start(0), _abort(0),
	_pending(1), _pass(0), _done(0), _waitNed(0), _cur(-1), _noMirror(false), _voiceDone(false),
	_voiceSlot(-1), _talkIndex(-1), _talking(0), _w2Started(0), _w4Voice(0), _w3Voice(0), _w0Voice(0),
	_w1Voice(0), _w2Voice(0), _creditsCount(0) {
	// 0x465540
	for (int i = 0; i < 6; i++)
		_progress[i] = 0;
	for (int i = 0; i < kWorlds * kTriggers; i++)
		_triggered[i] = _enabled[i] = 0;
}

Hints::~Hints() {
	_game->freeList(_list);
	_game->freeList(_creditsBg);
	_game->freeList(_creditsSpr);
}

// 0x472960 + 0x472970. The srand(timeGetTime()) at the start of 0x472970 is left to the CRT seed.
// Test hooks: nightmarened_hints_all=true enables every trigger; nightmarened_hint_progress=<l0>,...,<l4>
// sets the five world levels (DAT_0051eb28[0..4]).
void Hints::newGame() {
	unhook();
	_abort = 0;
	for (int i = 0; i < kWorlds * kTriggers; i++)
		_triggered[i] = _enabled[i] = 0;
	for (int world = 0; world < kWorlds; world++) {
		for (int n = 3; n != 0; n--) {
			bool found = false;
			do {
				int i = (int)_game->crtRandom() % (world != 4 ? 6 : 5) + world * 6;
				if (!_enabled[i]) {
					found = true;
					_enabled[i] = 1;
				}
			} while (!found);
		}
	}

	if (ConfMan.hasKey("nightmarened_hints_all") && ConfMan.getBool("nightmarened_hints_all")) {
		for (int i = 0; i < kWorlds * kTriggers; i++)
			_enabled[i] = 1;
	}
	if (ConfMan.hasKey("nightmarened_hint_progress")) {
		Common::String spec = ConfMan.get("nightmarened_hint_progress");
		const char *p = spec.c_str();
		for (int i = 0; i < kWorlds && *p; i++) {
			char *end;
			_progress[i] = CLIP<int32>(strtol(p, &end, 0), 0, 3);
			p = end;
			while (*p == ',')
				p++;
		}
	}
	for (int w = 0; w < kWorlds; w++) {
		debugC(1, kDebugGraphics, "Hints: world %d triggers %d%d%d%d%d%d", w, _enabled[w * 6], _enabled[w * 6 + 1],
			_enabled[w * 6 + 2], _enabled[w * 6 + 3], _enabled[w * 6 + 4], _enabled[w * 6 + 5]);
	}
}

// 0x475180
int Hints::trigger(int index, bool facingRight, int x, int y, NedXform xform, void *user) {
	int scene = _game->getCurrentSceneId();
	_scene = scene;
	switch (scene) {
	case 1:
		_world = 0;
		break;
	case 2:
	case 7:
		_world = 4;
		break;
	case 3:
	case 6:
		_world = 2;
		break;
	case 4:
		_world = 1;
		break;
	case 5:
		_world = 3;
		break;
	default:
		break;
	}
	if (index < 0 || index >= kTriggers)
		return 0;
	int i = index + _world * 6;
	if (_triggered[i]) {
		debugC(1, kDebugGraphics, "Hints: trigger %d of world %d already used", index, _world);
		return 0;
	}
	_triggered[i] = 1;
	if (getLevel(scene) == 3)
		return 0;
	int result = 0;
	if (_enabled[i]) {
		_optionsWere = _game->_optionsAllowed;
		if (_optionsWere)
			_game->_optionsAllowed = false;
		raiseLevel(_scene);
		_level = getLevel(_scene);
		_xform = xform;
		_xformUser = user;
		_pos = Common::Point(x, y);
		_facing = facingRight ? 1 : 0;
		if (_enabled[i] && load() == 0) {
			_start = 1;
			_cur = -1;
			_hooked = true;
			_game->setInputHook(inputHook);
			result = 1;
		}
	}
	debugC(1, kDebugGraphics, "Hints: trigger %d of world %d (scene %d, enabled %d) -> level %d, %s", index, _world,
		scene, _enabled[i], _level, result ? "vision starts" : "nothing");
	return result;
}

// 0x475300
int Hints::load() {
	if (!_naf) {
		if (!_game->getNaf(kNafHints))
			return 5;
		_naf = true;
	}
	_noMirror = _facing != 0;
	if (_world < 0 || _world >= kWorlds || _level < 1 || _level > 3)
		return 0;
	_game->freeList(_list);
	_list = _game->readList(kLists[_world][_level - 1].va, kLists[_world][_level - 1].count);
	if (!_game->loadList(kNafHints, _list, kHintScene))
		return 5;
	if (_level == 3 && _game->ned())
		_game->ned()->listLoad(kNedList);
	return 0;
}

// 0x475ae0: polls the list's last sprite, then shows each sprite (0x459090: z 2, hidden) and
// gives every vision sprite but the swirl a horizontal flip when the swirl opens to the left.
// The loader is synchronous, so the list is always ready.
bool Hints::show() {
	RdxSurface *primary = _game->display().getPrimary();
	for (const Request &r : _list) {
		if (r.type != Request::kSprite || r.area != kHintArea)
			continue;
		Slot &s = slot(r.id);
		if (s.sprite) {
			s.sprite->setDrawOrder(kHintZ);
			s.sprite->setDestination(primary);
			s.sprite->setVisibility(false);
		}
		if (r.id != kSwirl && !_facing)
			flip(r.id);
	}
	if (_level == 3 && _game->ned())
		_game->ned()->listShow(kNedList);
	return true;
}

void Hints::inputHook(Game *game, uint32 input, uint32 now, uint32 &out) {
	game->hints().hook(input, now, out);
}

// 0x472a30. When it ends on _abort the original leaves out unwritten, so the scene's tick
// gets the clock value as its input bits for that one tick; here it gets the input.
void Hints::hook(uint32 input, uint32 now, uint32 &out) {
	pollVoice();
	if (_abort) {
		unhook();
		end();
		if (_game->ned())
			_game->ned()->resetIdle();
		return;
	}
	out = input & kInputButton2;
	if (_start) {
		_abort = 0;
		_talking = 0;
		_talkIndex = -1;
		_w2Started = 0;
		_done = 0;
		_start = 0;
		_pending = 1;
	}
	if (_pending && show()) {
		_pending = 0;
		_voiceDone = false;
		_voiceSlot = -1;
		if (!_facing)
			flip(kSwirl);
		if (!_list.empty() && _list[0].hasPalette)
			_game->display().applyPalette(_list[0].palette, kPalStatic);
		_cur = (int16)_game->display().slotSwitch(kHintArea, kSwirl, -1, true);
		playSound(kSwirl);
		// The swirl opens beside Ned (his standing width away), 0.6 of its height above his feet
		Slot &swirl = slot(kSwirl);
		int nedW = _game->res().getSlot(1, 1).w;
		int dy = (int)(swirl.h * 0.6);
		swirl.x = _facing ? _pos.x + nedW : _pos.x - nedW;
		swirl.y = _pos.y - dy;
		_pass = 1;
		place(_cur);
		if (_level == 3 && _game->ned()) {
			Ned *ned = _game->ned();
			ned->setActionCallback(actionDone, this);
			int f = ned->getFacing();
			int action = ((_facing == 0 && f != 0) || (_facing == 1 && f == 0)) ? 0x3c : 0x3b;
			ned->playAction(action, _pos, 0, 0);
		}
		debugC(1, kDebugGraphics, "Hints: world %d level %d at (%d, %d), facing %d", _world, _level, swirl.x,
			swirl.y, _facing);
	}
	if (!_done && !_pending) {
		switch (_world) {
		case 0:
			_done = stepWorld0(now);
			break;
		case 1:
			_done = stepWorld1(now);
			break;
		case 2:
			_done = stepWorld2(now);
			break;
		case 3:
			_done = stepWorld3(now);
			break;
		case 4:
			_done = stepWorld4(now);
			break;
		default:
			break;
		}
		if (_done) {
			if (slot(kSwirl).sprite)
				slot(kSwirl).sprite->setVisibility(false);
			if (_level == 3) {
				if (_game->ned())
					_game->ned()->releaseState();
				_waitNed = 1;
			} else {
				_waitNed = 0;
			}
			debugC(1, kDebugGraphics, "Hints: vision done%s", _waitNed ? ", waiting for Ned" : "");
		}
	}
	if (_done && !_waitNed) {
		_game->stopSlotSound(slot(kSwirl));
		unhook();
		end();
	}
}

// 0x4759a0: Ned's action 0x3b/0x3c ended (negated when cut short)
void Hints::actionDone(void *user, int16 action) {
	Hints *h = (Hints *)user;
	int a = ABS((int)action);
	if (a == 0x3c || a == 0x3b)
		h->_abort = 1;
}

// 0x4759c0
void Hints::cancel() {
	unhook();
	if (_game->ned())
		_game->ned()->resetIdle();
	end();
}

// 0x475630
void Hints::end() {
	if (_optionsWere)
		_game->_optionsAllowed = true;
	_abort = 0;
	_cur = -1;
	_game->stopSlotSound(slot(kSwirl));
	_game->display().draw();
	if (_world >= 0 && _world < kWorlds && _level >= 1 && _level <= 3) {
		_game->freeList(_list);
		_list.clear();
		if (_level == 3 && _game->ned())
			_game->ned()->listFree(kNedList);
	}
	if (_naf) {
		_game->closeNaf(kNafHints);
		_naf = false;
	}
	debugC(1, kDebugGraphics, "Hints: ended, levels %d %d %d %d %d", _progress[0], _progress[1], _progress[2],
		_progress[3], _progress[4]);
	if (allLevelsDone())
		ending();
}

// The rest of 0x475630: every world is at level 3, so the game ends. 0x45a350 (every scene's
// leave/unload/+0x24 hooks) is not ported, and the end clip uses Media::playClip (0x47a3e0)
// in place of 0x47aa00.
void Hints::ending() {
	_game->media().stopMusic();
	int clip = _progress[5] < 8 ? kEndClipShort : kEndClipLong;
	debugC(1, kDebugGraphics, "Hints: every world done, end clip %d", clip);
	_game->media().loadClip(clip);
	_game->media().playClip(clip, 2);
	_game->media().playMusic(kCreditsMusic);
	creditsStart();
	while (!creditsStep(_game->now())) {
		if (_game->getEngine()->shouldQuit())
			break;
		Common::Event event;
		while (g_system->getEventManager()->pollEvent(event)) {
		}
		g_system->delayMillis(10);
	}
	creditsEnd();
	_game->media().stopMusic();
	// PostMessageA(WM_CLOSE)
	_game->getEngine()->quitGame();
}

// 0x4613d0: NED86000's background 4 at z 1 and the credits strip (area-1 slot 0x95) at z 0
void Hints::creditsStart() {
	if (!_game->getNaf(kNafOptions))
		return;
	_creditsCount = 0;
	RdxSurface *primary = _game->display().getPrimary();
	_creditsBg = _game->readList(kCreditsBgList, 1);
	_game->loadList(kNafOptions, _creditsBg, 0);
	if (_creditsBg[0].bitmap && _creditsBg[0].bitmap->object) {
		RdxBackground *bg = _creditsBg[0].bitmap->object;
		bg->setDrawOrder(1);
		bg->setDestination(primary);
		bg->setVisibility(true);
	}
	if (_creditsBg[0].hasPalette)
		_game->display().applyPalette(_creditsBg[0].palette, kPalAll);
	_creditsSpr = _game->readList(kCreditsSprList, 1);
	_game->loadList(kNafOptions, _creditsSpr, 0);
	Slot &s = _game->res().getSlot(kCreditsArea, kCreditsSlot);
	if (s.sprite) {
		s.sprite->setDrawOrder(0);
		s.sprite->setDestination(primary);
		s.sprite->setVisibility(true);
	}
	_game->display().slotPlace(kCreditsArea, kCreditsSlot, kCreditsSlot, nullptr, nullptr, true, Common::Point());
	s.y = 10;
	s.x = 0x4b;
	if (s.sprite)
		s.sprite->setPosition(0x4b, 10);
	_game->display().draw();
}

// 0x461540: one 41 ms step; the strip rises 2 px a step from step 25 to 2568, then 50 more
bool Hints::creditsStep(uint32 now) {
	Slot &s = _game->res().getSlot(kCreditsArea, kCreditsSlot);
	if (s.deadline < now) {
		_creditsCount++;
		s.deadline = now + kMsPerStep;
		if (_creditsCount > 0x18 && _creditsCount < 0xa09) {
			if (s.sprite) {
				s.sprite->setVisibility(true);
				s.y -= 2;
				s.sprite->setPosition(s.x, s.y);
			}
			_game->display().draw();
			// Test hook: nightmarened_creditsshot=<step>:<png> saves that credits step
			if (ConfMan.hasKey("nightmarened_creditsshot")) {
				Common::String spec = ConfMan.get("nightmarened_creditsshot");
				size_t c = spec.findFirstOf(':');
				if (c != Common::String::npos && atoi(spec.substr(0, c).c_str()) == _creditsCount) {
					Common::DumpFile out;
					byte pal[256 * 3];
					g_system->getPaletteManager()->grabPalette(pal, 0, 256);
					if (out.open(Common::Path(spec.substr(c + 1), '/'), true))
						Image::writePNG(out, _game->display().getPrimary()->getBuffer(), pal);
				}
			}
			return false;
		}
		if (_creditsCount > 0xa39)
			return true;
	}
	return false;
}

// 0x4615f0 (the strip's list is never freed by the original)
void Hints::creditsEnd() {
	_game->freeList(_creditsBg);
	_creditsBg.clear();
	Slot &s = _game->res().getSlot(kCreditsArea, kCreditsSlot);
	if (s.sprite)
		s.sprite->setVisibility(false);
	_game->closeNaf(kNafOptions);
}

// ---- The five vision players (0x473af0, 0x4741f0, 0x474990, 0x473430, 0x472d80) ----

// The not-due path every player shares: re-place the current step
int Hints::notDue() {
	Slot &s = slot(_cur);
	if (s.sequenceLength <= s.step)
		return 0;
	place(_cur);
	return 0;
}

// The shared tail: place the current step and time the next one
int Hints::stepTail(uint32 now, int result) {
	Slot &s = slot(_cur);
	if (s.sequenceLength <= s.step)
		return result;
	int d = place(_cur);
	slot(_cur).deadline = now + (int16)d * kMsPerStep;
	return result;
}

// Levels 1 and 2, identical in all five players: the swirl opens, the creature (content)
// plays at the swirl's position, the swirl plays again and the vision is over
int Hints::stepGlimpse(uint32 now, int16 content) {
	if (_cur == -1)
		return 0;
	Slot &s = slot(_cur);
	if (now <= s.deadline)
		return notDue();
	s.step++;
	if (s.step < s.sequenceLength)
		return stepTail(now, 0);
	s.step = s.sequenceLength - 1;
	int result = 0;
	if (_cur == content) {
		_cur = switchTo(kSwirl);
		playSound(kSwirl);
	} else if (_cur == kSwirl) {
		if (_pass > 1) {
			result = 1;
		} else {
			_pass = 2;
			copyPos(content, kSwirl);
			place(kSwirl);
			_cur = switchTo(content);
		}
	}
	return stepTail(now, result);
}

// 0x473af0 (world 0, scene 1)
int Hints::stepWorld0(uint32 now) {
	if (_level == 1)
		return stepGlimpse(now, 3);
	if (_level == 2)
		return stepGlimpse(now, 5);
	if (_level != 3 || _cur == -1)
		return 0;
	Slot &s = slot(_cur);
	if (now <= s.deadline)
		return notDue();
	s.step++;
	if (s.step < s.sequenceLength)
		return stepTail(now, 0);
	s.step = s.sequenceLength - 1;
	int result = 0;
	switch (_cur) {
	case 8:
		if (_w0Voice == 0) {
			playVoice(0x93);
			_w0Voice++;
		}
		if (_voiceDone) {
			copyPos(0x6b, 8);
			playSound(0x6b);
			_cur = switchTo(0x6b);
			place(_cur);
			_w0Voice = 0;
		}
		break;
	case kSwirl:
		if (_pass < 2) {
			_pass = 2;
			copyPos(8, kSwirl);
			_cur = switchTo(8);
			place(_cur);
			playSound(8);
			_w0Voice = 0;
		} else {
			result = 1;
		}
		break;
	case 0x6b:
		if (_w0Voice == 0) {
			playVoice(0x94);
			_w0Voice++;
		}
		if (_voiceDone) {
			copyPos(0x83, 8);
			_cur = switchTo(0x83);
			place(_cur);
			playSound(0x83);
			_w0Voice = 0;
		}
		break;
	case 0x83:
		_cur = switchTo(kSwirl);
		playSound(kSwirl);
		break;
	default:
		break;
	}
	return stepTail(now, result);
}

// 0x4741f0 (world 1, scene 4: the attic)
int Hints::stepWorld1(uint32 now) {
	if (_level == 1)
		return stepGlimpse(now, 0x0d);
	if (_level == 2)
		return stepGlimpse(now, 0x0f);
	if (_level != 3 || _cur == -1)
		return 0;
	Slot &s = slot(_cur);
	if (now <= s.deadline)
		return notDue();
	s.step++;
	if (s.step < s.sequenceLength)
		return stepTail(now, 0);
	s.step = s.sequenceLength - 1;
	int result = 0;
	switch (_cur) {
	case 0x12:
		if (_w1Voice == 0) {
			playVoice(0xa1);
			_w1Voice = 1;
		}
		if (!_voiceDone)
			break;
		if (_w1Voice == 1) {
			playVoice(0x8f);
			_w1Voice = 2;
		}
		if (!_voiceDone || _w1Voice != 2)
			break;
		copyPos(0x6f, 0x12);
		playSound(0x6f);
		_cur = switchTo(0x6f);
		_w1Voice = 0;
		break;
	case kSwirl:
		if (_pass > 1) {
			result = 1;
			break;
		}
		_pass = 2;
		copyPos(0x12, kSwirl);
		place(kSwirl);
		playSound(0x12);
		_cur = switchTo(0x12);
		_w1Voice = 0;
		break;
	case 0x6f:
		if (_w1Voice == 0) {
			playVoice(0x90);
			_w1Voice++;
		}
		if (!_voiceDone)
			break;
		copyPos(0x7d, 0x12);
		_cur = switchTo(0x7d);
		playSound(0x7d);
		_w1Voice = 0;
		break;
	case 0x7d:
		if (_w1Voice == 0) {
			playVoice(0x91);
			_w1Voice++;
		}
		if (!_voiceDone)
			break;
		copyPos(0x7e, 0x12);
		_cur = switchTo(0x7e);
		playSound(0x7e);
		_w1Voice = 0;
		break;
	case 0x7e:
		if (_w1Voice == 0) {
			playVoice(0x92);
			_w1Voice++;
		}
		if (!_voiceDone)
			break;
		_cur = switchTo(kSwirl);
		playSound(kSwirl);
		_w1Voice = 0;
		break;
	default:
		break;
	}
	return stepTail(now, result);
}

// 0x474990 (world 2, scenes 3 and 6). The swirl's first end shows creature 0x1c frozen on
// step 0 without making it current (the original drops slot_switch's result) while line
// 0x9a plays; the swirl keeps stepping, hidden, until the line ends.
int Hints::stepWorld2(uint32 now) {
	if (_level == 1)
		return stepGlimpse(now, 0x17);
	if (_level == 2)
		return stepGlimpse(now, 0x19);
	if (_level != 3 || _cur == -1)
		return 0;
	Slot &s = slot(_cur);
	if (now <= s.deadline)
		return notDue();
	s.step++;
	if (s.step < s.sequenceLength)
		return stepTail(now, 0);
	s.step = s.sequenceLength - 1;
	int result = 0;
	switch (_cur) {
	case 0x1c:
		if (_w2Voice == 0) {
			playVoice(0x9b);
			_w2Voice++;
		}
		if (!_voiceDone)
			break;
		copyPos(0x73, 0x1c);
		playSound(0x73);
		_cur = switchTo(0x73);
		_w2Voice = 0;
		break;
	case kSwirl:
		if (_pass > 1) {
			result = 1;
			break;
		}
		if (_w2Started) {
			if (_voiceDone) {
				_pass = 2;
				copyPos(0x1c, kSwirl);
				place(kSwirl);
				playSound(0x1c);
				_cur = switchTo(0x1c);
				_w2Voice = 0;
			}
			break;
		}
		_voiceDone = false;
		_w2Started = 1;
		copyPos(0x1c, kSwirl);
		slot(0x1c).step = 0;
		place(0x1c);
		switchTo(0x1c);
		playVoice(0x9a);
		break;
	case 0x73:
		if (_w2Voice == 0) {
			playVoice(0x9c);
			_w2Voice++;
		}
		if (!_voiceDone)
			break;
		copyPos(0x7f, 0x1c);
		_cur = switchTo(0x7f);
		playSound(0x7f);
		_w2Voice = 0;
		break;
	case 0x7f:
		if (_w2Voice == 0) {
			playVoice(0x9d);
			_w2Voice++;
		}
		if (!_voiceDone)
			break;
		copyPos(0x80, 0x1c);
		_cur = switchTo(0x80);
		playSound(0x80);
		_w2Voice = 0;
		break;
	case 0x80:
		_cur = switchTo(kSwirl);
		playSound(kSwirl);
		_w2Voice = 0;
		break;
	default:
		break;
	}
	return stepTail(now, result);
}

// 0x473430 (world 3, scene 5): creature 0x2f loops while nine lines play in turn
int Hints::stepWorld3(uint32 now) {
	if (_level == 1)
		return stepGlimpse(now, 0x2b);
	if (_level == 2)
		return stepGlimpse(now, 0x2d);
	if (_level != 3 || _cur == -1)
		return 0;
	static const int16 kLines[8] = { 0x30, 0x96, 0x9e, 0x97, 0x9f, 0x98, 0xa0, 0x99 };
	if (_talking && _voiceDone) {
		if (_talkIndex >= 0 && _talkIndex < 8) {
			playVoice(kLines[_talkIndex]);
			_talkIndex++;
		} else if (_talkIndex == 8) {
			_cur = switchTo(kSwirl);
			playSound(kSwirl);
			_talking = 0;
		}
	}
	int result = 0;
	Slot &s = slot(_cur);
	if (s.deadline < now) {
		s.step++;
		if (s.sequenceLength <= s.step) {
			s.step = s.sequenceLength - 1;
			if (_cur == 0x2f) {
				slot(0x2f).step = 0;
			} else if (_cur == kSwirl) {
				if (_pass < 2) {
					_pass = 2;
					copyPos(0x2f, kSwirl);
					place(kSwirl);
					playVoice(0x95);
					_talkIndex++;
					_cur = switchTo(0x2f);
					_w3Voice = 0;
					_talking = 1;
				} else {
					result = 1;
				}
			}
		}
		return stepTail(now, result);
	}
	if (s.step < s.sequenceLength)
		place(_cur);
	return result;
}

// 0x472d80 (world 4, scenes 2 and 7)
int Hints::stepWorld4(uint32 now) {
	if (_level == 1)
		return stepGlimpse(now, 0x21);
	if (_level == 2)
		return stepGlimpse(now, 0x23);
	if (_level != 3 || _cur == -1)
		return 0;
	Slot &s = slot(_cur);
	if (now <= s.deadline)
		return notDue();
	s.step++;
	if (s.step < s.sequenceLength)
		return stepTail(now, 0);
	s.step = s.sequenceLength - 1;
	int result = 0;
	switch (_cur) {
	case 0x26:
		if (_w4Voice == 0) {
			playVoice(0x8c);
			_w4Voice++;
		}
		if (!_voiceDone)
			break;
		copyPos(0x77, 0x26);
		playSound(0x77);
		_cur = switchTo(0x77);
		_w4Voice = 0;
		break;
	case kSwirl:
		if (_pass > 1) {
			result = 1;
			break;
		}
		_pass = 2;
		copyPos(0x26, kSwirl);
		place(kSwirl);
		playSound(0x26);
		_cur = switchTo(0x26);
		_w4Voice = 0;
		break;
	case 0x77:
		if (_w4Voice == 0) {
			playVoice(0x8d);
			_w4Voice++;
		}
		if (_voiceDone) {
			copyPos(0x82, 0x26);
			_cur = switchTo(0x82);
			playSound(0x82);
			_w4Voice = 0;
		}
		break;
	case 0x82:
		_cur = switchTo(kSwirl);
		playSound(kSwirl);
		break;
	default:
		break;
	}
	return stepTail(now, result);
}

// ---- Helpers ----

Slot &Hints::slot(int id) {
	return _game->res().getSlot(kHintArea, id);
}

// 0x430850 with the caller's xform (a pure scroll offset in every caller): returns the step's duration
int Hints::place(int id) {
	if (id < 0 || id >= kAreaSlots)
		return 0;
	int sx = 0, sy = 0;
	if (_xform)
		_xform(_xformUser, sx, sy);
	int frame = 0, duration = 0;
	_game->display().slotPlace(kHintArea, id, id, &frame, &duration, _noMirror, Common::Point(sx, sy));
	return duration;
}

// 0x430890
int16 Hints::switchTo(int id) {
	debugC(4, kDebugGraphics, "Hints: slot %d -> %d at %u (frame %u)", _cur, id, _game->now(), _game->display()._drawCount);
	return (int16)_game->display().slotSwitch(kHintArea, id, _cur);
}

void Hints::copyPos(int dst, int src) {
	slot(dst).x = slot(src).x;
	slot(dst).y = slot(src).y;
}

// objApplyEffect(FX_HORIZ_FLIP) + fxSetActive(fx, DAT_004b3570 == 0)
void Hints::flip(int id) {
	Slot &s = slot(id);
	if (!s.sprite)
		return;
	s.flip = s.sprite->applyEffect(kFxHorizFlip);
	s.flip->active = !_noMirror;
}

// 0x455880
void Hints::playSound(int id) {
	_game->playSlotSound(slot(id), nullptr);
	debugC(1, kDebugSound, "Hints: sound, slot %d", id);
}

// 0x455800 with the end callback 0x475990, which sets DAT_0051fc80. A line that fails to
// play counts as ended (the original would wait for ever).
void Hints::playVoice(int id) {
	_voiceDone = false;
	_voiceSlot = id;
	bool ok = _game->playSlotSound(slot(id), nullptr);
	debugC(1, kDebugSound, "Hints: voice, slot %d%s", id, ok ? "" : " (no sound)");
}

void Hints::pollVoice() {
	if (_voiceSlot >= 0 && !_voiceDone && !_game->isVoicePlaying(slot(_voiceSlot).voice)) {
		_voiceDone = true;
		_voiceSlot = -1;
	}
}

// DAT_0051f760 = 0x472a20
void Hints::unhook() {
	_hooked = false;
	_game->setInputHook(nullptr);
}

// ---- Progress and saved state ----

// 0x467680
int Hints::getLevel(int scene) const {
	switch (scene) {
	case 2:
	case 7:
		return _progress[1];
	case 3:
	case 6:
		return _progress[2];
	case 4:
		return _progress[3];
	case 5:
		return _progress[4];
	default:
		return _progress[0];
	}
}

// 0x467600
void Hints::raiseLevel(int scene) {
	int i;
	switch (scene) {
	case 2:
	case 7:
		i = 1;
		break;
	case 3:
	case 6:
		i = 2;
		break;
	case 4:
		i = 3;
		break;
	case 5:
		i = 4;
		break;
	default:
		i = 0;
		break;
	}
	_progress[i]++;
	if (_progress[i] > 3)
		_progress[i] = 3;
}

// 0x475a60
bool Hints::allLevelsDone() const {
	for (int scene = 1; scene <= 7; scene++) {
		if (getLevel(scene) != 3)
			return false;
	}
	return true;
}

// 0x475900
void Hints::saveState(int32 *out) const {
	for (int i = 0; i < kWorlds * kTriggers; i++) {
		out[i] = _triggered[i];
		out[kWorlds * kTriggers + i] = _enabled[i];
	}
}

// 0x475920: anything but a full block clears both tables
void Hints::loadState(const int32 *in, int size) {
	for (int i = 0; i < kWorlds * kTriggers; i++) {
		bool ok = size == kSaveSize && in;
		_triggered[i] = ok ? in[i] : 0;
		_enabled[i] = ok ? in[kWorlds * kTriggers + i] : 0;
	}
}

} // End of namespace NightmareNed
