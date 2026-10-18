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
#include "common/ptr.h"
#include "common/savefile.h"
#include "common/system.h"
#include "common/textconsole.h"

#include "nightmarened/detection.h"
#include "nightmarened/display.h"
#include "nightmarened/game.h"
#include "nightmarened/hints.h"
#include "nightmarened/media.h"
#include "nightmarened/ned.h"
#include "nightmarened/nightmarened.h"
#include "nightmarened/options.h"
#include "nightmarened/rdx.h"

namespace NightmareNed {

// List entries named by the code
enum {
	kEntryBackdrop = 0,
	kEntryFont = 1,         // 2: the picked row's font
	kEntryTakes = 27,       // 27..36: sound ids picked at random on every open
	kEntryNewGame = 27,
	kEntryLoaded = 28,
	kEntrySaved = 29,       // 0x1d
	kEntryBack = 30,
	kEntryCreditsSound = 31,
	kEntryKeys = 32,
	kEntryDone = 33,        // 0x21
	kEntryQuit = 34,
	kEntryNo = 35,          // 0x23
	kEntryNedA = 36,        // 0x24
	kEntrySaveScreen = 37,
	kEntryNedB = 38,        // 0x26
	kEntryMusicOn = 39,
	kEntryMusicOff = 40
};

Options::Options(Game *game) : _game(game), _open(false), _state(kStateLoading), _item(0), _dialog(0),
	_button(kOk), _lastInput(0), _lastInputTime(0), _openedAt(0), _nedWasEnabled(false), _slot(0),
	_cursor(-1), _listX(0), _listY(0), _rowStep(0), _boxW(0), _boxH(0), _caretX(0), _caretY(0),
	_blinkTime(0), _blink(false), _blinkOn(false), _keyChoice(0), _creditsCount(0), _lastSound(nullptr),
	_volume(10), _surface(nullptr) {
	memset(_savedPalette, 0, sizeof(_savedPalette));
	memset(_palette, 0, sizeof(_palette));
	memset(_creditsPalette, 0, sizeof(_creditsPalette));
	memset(_names, 0, sizeof(_names));
	memset(_edit, 0, sizeof(_edit));
}

Options::~Options() {
	delete _surface;
	_game->freeList(_list);
	_game->freeList(_creditsList);
}

Slot &Options::slot(int id) {
	return _game->res().getSlot(kArea, id);
}

void Options::spriteVisible(int id, bool on) {
	if (slot(id).sprite)
		slot(id).sprite->setVisibility(on);
}

void Options::showBitmap(Request &req, int z, bool visible) {
	if (!req.bitmap || !req.bitmap->object)
		return;
	req.bitmap->object->setDrawOrder(z);
	req.bitmap->object->setDestination(_game->display().getPrimary());
	req.bitmap->object->setVisibility(visible);
}

void Options::draw() {
	_game->display().draw();
}

// 0x45e6c0 (also inlined in 0x45eab0, 0x45f1b0, 0x45f3c0, 0x460350): the end callback
// 0x45da30 clears DAT_0051dcfc
void Options::playEntrySound(int entry, bool async) {
	if (entry < 0 || entry >= (int)_list.size())
		return;
	Slot &s = slot(_list[entry].id);
	_game->playSlotSound(s, _lastSound);
	_lastSound = &s;
	if (!async) {
		waitSound();
		_lastSound = nullptr;
	}
}

void Options::waitSound() {
	NightmareNedEngine *vm = _game->getEngine();
	while (_lastSound && _game->isVoicePlaying(_lastSound->voice) && !vm->shouldQuit()) {
		vm->pumpEvents();
		g_system->delayMillis(10);
	}
}

// 0x45fe40, over 86999019.SST: every sprite's place, at step 0
int Options::init() {
	Common::ScopedPtr<Common::SeekableReadStream> sst(_game->getEngine()->openFile("86999019.SST"));
	if (!sst || !loadPlacements(*sst, _placements)) {
		warning("Can't read 86999019.SST");
		return 1;
	}
	for (const Placement &p : _placements) {
		if (p.kind != 2 || p.area < 0 || p.area >= kAreaCount || p.slot < 0 || p.slot >= kAreaSlots)
			continue;
		Slot &s = _game->res().getSlot(p.area, p.slot);
		s.x = p.x;
		s.y = p.y;
		s.step = 0;
	}
	if (!_game->getNaf(kNafOptions))
		return 5;
	// ned.ini [Ned Prefs] MusicVolume
	_volume = ConfMan.hasKey("MusicVolume") ? ConfMan.getInt("MusicVolume") : 10;
	if (_volume > 10)
		_volume = 10;
	_game->media().setMusicVolume(_volume);
	return 0;
}

// 0x45db30
int Options::open() {
	Display &display = _game->display();
	memcpy(_savedPalette, display.getPalette(), sizeof(_savedPalette));
	_openedAt = _game->now();

	_list = _game->readList(kList, kListCount);
	// Entries 27..36: each sound id is one of 2..5 takes
	static const struct {
		uint32 table;
		uint32 count;
	} kTakes[10] = {
		{ 0x4b0328, 5 }, { 0x4b0338, 5 }, { 0x4b0348, 5 }, { 0x4b0358, 4 }, { 0x4b0360, 3 },
		{ 0x4b0368, 3 }, { 0x4b0370, 2 }, { 0x4b0378, 5 }, { 0x4b0388, 3 }, { 0x4b0390, 3 }
	};
	for (int i = 0; i < 10; i++) {
		uint32 r = _game->crtRandom();
		_list[kEntryTakes + i].id = (int16)_game->exe().readUint16(kTakes[i].table + (r % kTakes[i].count) * 2);
	}
	_game->loadList(kNafOptions, _list, 0);
	_open = true;

	Ned *ned = _game->ned();
	_nedWasEnabled = ned && ned->isEnabled();
	if (_nedWasEnabled) {
		ned->show(false);
		ned->enable(false);
	}
	_surface = new RdxSurface(640, 480);
	_surface->setRenderCallback(render, this);
	_surface->setDrawOrder(1);
	_surface->setDestination(display.getPrimary());
	_state = kStateLoading;
	draw();

	showBitmap(_list[kEntryBackdrop], 2, true);
	if (_list[kEntryBackdrop].hasPalette)
		memcpy(_palette, _list[kEntryBackdrop].palette, sizeof(_palette));
	display.applyPalette(_palette, kPalAll);
	_item = 0;
	for (int i = 0; i < 8; i++) {
		if (_game->exe().readUint16(kItems + i * 2) == 0x87) {
			_item = i;
			break;
		}
	}
	_state = kStateMenu;
	attachSprites();
	_cursor = -1;
	selectRow(_slot);
	spriteVisible(_game->exe().readUint16(kItems + _item * 2), true);
	draw();
	return 0;
}

// 0x460d40
void Options::attachSprites() {
	static const int kDialogSprites[] = {
		0x8a, 0x8b, 0x16d, 0x16e, 0x16f, 0x170, 0x171, 0x172, 0x173, 0x174, 0x176, 0x177, 0x188, 0x175, 0x18a, 0xb9,
		0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89
	};
	RdxSurface *primary = _game->display().getPrimary();
	for (int id : kDialogSprites) {
		Slot &s = slot(id);
		if (!s.sprite)
			continue;
		s.sprite->setDrawOrder(1);
		s.sprite->setDestination(primary);
		s.sprite->setVisibility(false);
	}
	static const int kPlaced[] = {
		0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x16e, 0x16f, 0x170, 0x171, 0x172, 0x173, 0x174,
		0x176, 0x177, 0x188, 0x175, 0x18a, 0xb9
	};
	for (int id : kPlaced)
		_game->display().slotPlace(kArea, id, id, nullptr, nullptr, true, Common::Point(0, 0));
}

// 0x45df80
void Options::close(bool restorePalette) {
	closeQuiet();
	_game->adjustClock(_game->now() - _openedAt);
	if (restorePalette)
		_game->display().applyPalette(_savedPalette, kPalAll);
}

// 0x45f050
void Options::closeQuiet() {
	delete _surface;
	_surface = nullptr;
	waitSound();
	_lastSound = nullptr;
	_game->freeList(_list);
	_list.clear();
	_open = false;
	if (_nedWasEnabled && _game->ned()) {
		_game->ned()->show(true);
		_game->ned()->enable(true);
	}
}

void Options::closeForLoad() {
	if (_open)
		close(false);
}

// 0x45e980: the dialog, with ok picked; the ok and cancel highlights go where 0x4b0290 says
void Options::showDialog(int16 dialog) {
	_dialog = dialog;
	_button = kOk;
	spriteVisible(dialog, true);
	Slot &d = slot(dialog);
	const ExeImage &exe = _game->exe();
	for (int i = 0;; i++) {
		uint32 rec = kDialogs + i * 10;
		int16 id = (int16)exe.readUint16(rec);
		if (id == dialog) {
			int16 okX = exe.readUint16(rec + 2), okY = exe.readUint16(rec + 4);
			int16 noX = exe.readUint16(rec + 6), noY = exe.readUint16(rec + 8);
			if (slot(kOk).sprite)
				slot(kOk).sprite->setPosition(okX + 2 + d.x, okY - 2 + d.y);
			if (noX < 0) {
				_state = kStateNotice;
			} else {
				if (slot(kCancel).sprite)
					slot(kCancel).sprite->setPosition(d.x + 2 + noX, noY - 2 + d.y);
				_state = kStateDialog;
			}
			spriteVisible(_button, true);
			return;
		}
		if (id == 0)
			break;
	}
	_state = kStateNotice;
}

// 0x45f100
void Options::hideDialog() {
	spriteVisible(_dialog, false);
	spriteVisible(_button, false);
	_state = kStateMenu;
}

// 0x45f150
void Options::toggleButton() {
	spriteVisible(_button, false);
	_button = _button == kOk ? kCancel : kOk;
	spriteVisible(_button, true);
}

// The 250 ms key repeat every handler starts with
bool Options::isRepeat(uint32 input, uint32 now) {
	if (input == _lastInput && now <= _lastInputTime + kRepeatMs)
		return true;
	_lastInputTime = now;
	return false;
}

// ---- The caret ----

// 0x460150: column -1 frames the whole row, otherwise a bar before that letter
void Options::setCaret(int16 row, int16 column) {
	if (column < 0) {
		_caretY = row * _rowStep + _listY;
		_caretX = _listX;
	} else {
		_caretY = row * _rowStep + 1 + _listY;
		_caretX = _listX + column * 0xb + 2;
	}
	_blinkTime = _game->now();
	_blink = true;
	_blinkOn = true;
}

// 0x460cb0
bool Options::caretTick(uint32 now) {
	if (_blink && now - _blinkTime > 500) {
		_blinkTime = now;
		_blinkOn = !_blinkOn;
		return true;
	}
	return false;
}

// 0x460d00
bool Options::caretShow(bool on) {
	if (_blink && on != _blinkOn) {
		_blinkOn = on;
		return true;
	}
	return false;
}

void Options::caretOff() {
	if (_blink) {
		_blinkOn = false;
		_blink = false;
	}
}

void Options::selectRow(int16 row) {
	_slot = row;
	_cursor = -1;
	Common::strlcpy(_edit, _names[row], sizeof(_edit));
	setCaret(row, -1);
}

// 0x45feb0
void Options::selectNext(bool skipEmpty) {
	int16 row = _slot;
	do {
		row = row < 7 ? row + 1 : 0;
	} while (skipEmpty && !_names[row][0] && row != _slot);
	selectRow(row);
}

// ---- The main loop ----

// 0x45e060
void Options::tick(uint32 keys, uint32 now) {
	uint32 input = 0;
	if (keys & 0xff)
		input = keys & 0xff;
	if ((keys & 0xff00) && (keys & kInputButton2))
		input = kInputButton2;

	bool redraw = false;
	switch (_state) {
	case kStateLoad:
		redraw = caretTick(now);
		redraw |= loadInput(input, now);
		break;
	case kStateSave:
		redraw = caretTick(now);
		redraw |= saveInput(input, now);
		break;
	case kStateReplace:
		redraw = caretTick(now);
		redraw |= dialogInput(input, now);
		break;
	case kStateDialog:
	case kStateNotice:
		redraw = dialogInput(input, now);
		break;
	case kStateMusic:
		redraw = musicInput(input, now);
		break;
	case kStateCredits:
		creditsTick(now);
		break;
	case kStateKeys:
	case kStateKeysHelp: {
		if (_state == kStateKeys && _blink && now - _blinkTime >= 0x1f5) {
			_blinkTime = now;
			redraw = caretShow(!_blinkOn);
		}
		if (isRepeat(input, now))
			break;
		if (input & 0xff00) {
			_lastInput = input;
			hideDialog();
			if (_dialog == 0x176) {
				if (_button == kOk) {
					if (_keyChoice == 0)
						showDialog(0x177);
					else if (_keyChoice > 0 && _keyChoice < 3)
						showDialog(0x188);
					_state = kStateKeysHelp;
					redraw = true;
					break;
				}
				playEntrySound(kEntryNo, true);
			}
			redraw = true;
			break;
		}
		_lastInput = input;
		if (_dialog != 0x176)
			break;
		switch (input & 0xff) {
		case kInputLeft:
		case kInputRight:
			toggleButton();
			redraw = true;
			break;
		case kInputUp:
		case kInputDown:
			if (_blink) {
				caretShow(false);
				_blink = false;
			}
			if ((input & 0xff) == kInputUp)
				_keyChoice = _keyChoice < 1 ? 2 : _keyChoice - 1;
			else
				_keyChoice = _keyChoice < 2 ? _keyChoice + 1 : 0;
			setCaret(_keyChoice, -1);
			redraw = true;
			break;
		default:
			break;
		}
		break;
	}
	default: {
		if (isRepeat(input, now))
			break;
		uint16 item = _game->exe().readUint16(kItems + _item * 2);
		if (!(input & 0xff00)) {
			_lastInput = input;
			spriteVisible(item, false);
			if ((input & 0xff) == kInputLeft) {
				_item = _item < 1 ? 7 : _item - 1;
				redraw = true;
			} else if ((input & 0xff) == kInputRight) {
				_item = _item < 7 ? _item + 1 : 0;
				redraw = true;
			}
			spriteVisible(_game->exe().readUint16(kItems + _item * 2), true);
			break;
		}
		redraw = true;
		_lastInput = input;
		switch (item) {
		case 0x82:
			showVolume(_volume);
			_state = kStateMusic;
			break;
		case 0x83:
			showDialog(0x16f);
			break;
		case 0x84:
			playEntrySound(kEntryDone, true);
			showDialog(0x170);
			break;
		case 0x85:
			showDialog(0x171);
			break;
		case 0x86:
			if (readNames() > 0) {
				_listX = slot(0x172).x + 0x31;
				_listY = slot(0x172).y + 0x42;
				_boxW = 0x88;
				_boxH = 0xd;
				_rowStep = 0xd;
				_cursor = -1;
				if (!_names[_slot][0])
					selectNext(true);
				setCaret(_slot, -1);
				showDialog(0x172);
				_state = kStateLoad;
			} else {
				showDialog(0x18a);
			}
			break;
		case 0x87:
			showDialog(0x173);
			break;
		case 0x88:
			openSave();
			break;
		case 0x89:
			_listX = slot(0x176).x + 0x54;
			_listY = slot(0x176).y + 0x4b;
			_boxW = 0x8c;
			_boxH = 0x14;
			_rowStep = 0x20;
			_cursor = -1;
			_keyChoice = 0;
			setCaret(0, -1);
			playEntrySound(kEntryKeys, true);
			showDialog(0x176);
			_state = kStateKeys;
			break;
		default:
			break;
		}
		break;
	}
	}
	if (redraw)
		draw();
}

// 0x45eab0
bool Options::dialogInput(uint32 input, uint32 now) {
	if (isRepeat(input, now))
		return false;
	_lastInput = input;
	if (!(input & 0xff00)) {
		if (_state != kStateNotice && (input & 0xff) && (input & 0xff) < 3) {
			toggleButton();
			return true;
		}
		return false;
	}
	spriteVisible(_dialog, false);
	spriteVisible(_button, false);
	_state = kStateMenu;
	if (_button != kOk) {
		playEntrySound(kEntryNo, true);
		if (_dialog == 0x175)
			openSave();
		return true;
	}
	switch (_dialog) {
	case 0x16f:
		creditsStart();
		return true;
	case 0x170:
		playEntrySound(kEntryQuit, true);
		closeQuiet();
		_game->adjustClock(_game->now() - _openedAt);
		_game->getEngine()->quitGame();
		return true;
	case 0x171:
		playEntrySound(kEntryNewGame, true);
		closeQuiet();
		_game->adjustClock(_game->now() - _openedAt);
		newGame();
		_game->jumpToScene(0, -1);
		return true;
	case 0x173:
		playEntrySound(kEntryBack, true);
		closeQuiet();
		_game->adjustClock(_game->now() - _openedAt);
		_game->display().applyPalette(_savedPalette, kPalAll);
		return true;
	case 0x175:
		if (writeSave(_slot, _edit)) {
			playEntrySound(kEntrySaved, true);
			close(true);
			return true;
		}
		showDialog(0xb9);
		return true;
	default:
		return true;
	}
}

// 0x460690
void Options::showVolume(int level) {
	spriteVisible(kVolume, true);
	spriteVisible(kKnob, true);
	if (slot(kKnob).sprite)
		slot(kKnob).sprite->setPosition(level * 0xf + 0x37 + slot(kVolume).x, slot(kVolume).y + 0x1e);
}

// 0x45f1b0: ned.ini [Ned Prefs] MusicVolume is written on every change
bool Options::musicInput(uint32 input, uint32 now) {
	if (isRepeat(input, now))
		return false;
	_lastInput = input;
	if (input & 0xff00) {
		spriteVisible(kVolume, false);
		spriteVisible(kKnob, false);
		_state = kStateMenu;
		return true;
	}
	int16 old = _volume;
	bool changed = false;
	if ((input & 0xff) == kInputLeft) {
		if (_volume >= 1) {
			_volume = MAX<int16>(_volume - 1, 0);
			changed = true;
		}
	} else if ((input & 0xff) == kInputRight && _volume <= 10) {
		_volume = MIN<int16>(_volume + 1, 10);
		changed = true;
	}
	if (changed)
		showVolume(_volume);
	if (old == _volume)
		return changed;
	ConfMan.setInt("MusicVolume", _volume);
	ConfMan.flushToDisk();
	_game->media().setMusicVolume(_volume);
	if (_volume == 0)
		playEntrySound(kEntryMusicOff, true);
	else if (old == 0)
		playEntrySound(kEntryMusicOn, true);
	return changed;
}

// 0x460260 (and the first half of 0x460350): GAME01..GAME08 are ScummVM slots 1..8
int Options::readNames() {
	int count = 0;
	Common::SaveFileManager *saves = g_system->getSavefileManager();
	for (int i = 0; i < kSaveSlots; i++) {
		_names[i][0] = 0;
		Common::ScopedPtr<Common::InSaveFile> in(saves->openForLoading(_game->getEngine()->getSaveStateName(i + 1)));
		if (!in)
			continue;
		in->read(_names[i], kNameSize);
		_names[i][kNameSize] = 0;
		count++;
	}
	return count;
}

// 0x45f3c0
bool Options::loadInput(uint32 input, uint32 now) {
	if (isRepeat(input, now))
		return false;
	_lastInput = input;
	if (!(input & 0xff00)) {
		switch (input & 0xff) {
		case kInputLeft:
		case kInputRight:
			toggleButton();
			return true;
		case kInputUp: {
			caretOff();
			int16 row = _slot;
			do {
				row = row < 1 ? 7 : row - 1;
			} while (!_names[row][0] && row != _slot);
			selectRow(row);
			return true;
		}
		case kInputDown: {
			caretOff();
			int16 row = _slot;
			do {
				row = row < 7 ? row + 1 : 0;
			} while (!_names[row][0] && row != _slot);
			selectRow(row);
			return true;
		}
		default:
			return false;
		}
	}
	spriteVisible(_dialog, false);
	spriteVisible(_button, false);
	_state = kStateMenu;
	if (_button != kOk) {
		playEntrySound(kEntryNo, true);
		return true;
	}
	int scene = -1, sub = -1;
	bool ok = false;
	Common::ScopedPtr<Common::InSaveFile> in(g_system->getSavefileManager()->openForLoading(
		_game->getEngine()->getSaveStateName(_slot + 1)));
	if (in) {
		in->skip(kNameSize);
		ok = readGame(*in, scene, sub);
	}
	if (!ok) {
		showDialog(0xb9);
		return true;
	}
	playEntrySound(kEntryLoaded, true);
	close(false);
	debugC(1, kDebugLoading, "Loaded GAME%02d: scene %d:%d", _slot + 1, scene, sub);
	_game->jumpToScene(scene, sub);
	return false;
}

// 0x460350: the save list, the picked row's name ready to keep or retype
void Options::openSave() {
	readNames();
	_listX = slot(0x174).x + 0x44;
	_listY = slot(0x174).y + 0x3c;
	_boxH = 0xd;
	_rowStep = 0xd;
	_boxW = 0x88;
	selectRow(_slot);
	playEntrySound(kEntrySaveScreen, true);
	showDialog(0x174);
	_state = kStateSave;
}

// 0x45f8f0
bool Options::saveInput(uint32 input, uint32 now) {
	if (isRepeat(input, now))
		return false;
	_lastInput = input;
	if (!(input & 0xff00)) {
		switch (input & 0xff) {
		case kInputLeft:
		case kInputRight:
			toggleButton();
			return true;
		case kInputUp:
		case kInputDown:
			// Only while no name is being typed
			if (_cursor >= 0)
				return false;
			caretOff();
			if ((input & 0xff) == kInputUp)
				selectRow(_slot < 1 ? 7 : _slot - 1);
			else
				selectRow(_slot < 7 ? _slot + 1 : 0);
			return true;
		default:
			return false;
		}
	}
	if (_button != kOk) {
		hideDialog();
		playEntrySound(kEntryNo, true);
		return true;
	}
	if (!_edit[0]) {
		playEntrySound(kEntrySaveScreen, true);
		_state = kStateSave;
		return true;
	}
	if (_cursor >= 0) {
		// A typed name: "ned..." gets a special remark three times in four, any name a remark
		int entry = 0;
		if (scumm_strnicmp(_edit, "ned", 3) == 0 && _game->crtRandom() % 4 > 0)
			entry = kEntryNedB;
		if (!entry && _game->crtRandom() % 4 > 0)
			entry = kEntryNedA;
		if (entry)
			playEntrySound(entry, false);
	}
	if (_names[_slot][0]) {
		showDialog(0x175);
		_state = kStateReplace;
		return true;
	}
	spriteVisible(_dialog, false);
	spriteVisible(_button, false);
	_state = kStateMenu;
	if (writeSave(_slot, _edit)) {
		playEntrySound(kEntrySaved, true);
		close(true);
		return false;
	}
	showDialog(0xb9);
	return true;
}

// 0x460580: the name, then the game (0x459d90), as ScummVM slot index + 1
bool Options::writeSave(int index, const char *name) {
	Common::strlcpy(_names[index], name, sizeof(_names[index]));
	bool ok = _game->getEngine()->saveSlot(index + 1, name);
	debugC(1, kDebugLoading, "Saved GAME%02d \"%s\": %s", index + 1, name, ok ? "ok" : "failed");
	return ok;
}

// 0x45e740: typing a name in the save list (letters and space, at most 11), Backspace,
// Delete; during the credits any key ends them
void Options::keyDown(uint32 vk) {
	if (_state != kStateSave) {
		if (_state == kStateCredits)
			creditsEnd();
		return;
	}
	caretOff();
	if (vk == 8) {
		editErase(false, true);
	} else if (vk == 0x2e) {
		editErase(true, true);
	} else if (vk > 0x1f) {
		char c = (char)toupper((int)vk);
		if (c == ' ' || (c > '@' && c < '[')) {
			if (_cursor < 0) {
				_cursor = 0;
				_edit[0] = 0;
			}
			if (_cursor < 0xb) {
				int len = strlen(_edit);
				if (_cursor <= len)
					memmove(_edit + _cursor + 1, _edit + _cursor, len - _cursor + 1);
				_edit[_cursor++] = c;
			}
		}
	}
	setCaret(_slot, _cursor);
	draw();
}

// 0x45ffa0
void Options::editErase(bool forward, bool erase) {
	int16 len = strlen(_edit);
	if (len < 1)
		return;
	if (_cursor < 0) {
		if (erase) {
			_edit[0] = 0;
			_cursor = 0;
		} else {
			_cursor = forward ? len : 0;
		}
	} else {
		if (!forward && _cursor > 0)
			_cursor--;
		if (erase) {
			if (_cursor < len)
				memmove(_edit + _cursor, _edit + _cursor + 1, len - _cursor);
		} else if (forward && _cursor < len) {
			_cursor++;
		}
	}
	setCaret(_slot, _cursor);
}

// ---- Credits ----

// 0x45eab0 "Who made this?": the swirl replaces Ned's head and the strip scrolls up
void Options::creditsStart() {
	_state = kStateCredits;
	_creditsCount = 0;
	delete _list[kEntryBackdrop].bitmap;
	_list[kEntryBackdrop].bitmap = nullptr;
	NafArchive *naf = _game->getNaf(kNafOptions);
	_creditsList = _game->readList(kCreditsList, 1);
	if (naf)
		_game->loadList(kNafOptions, _creditsList, 0);
	showBitmap(_creditsList[0], 2, true);
	if (_creditsList[0].hasPalette)
		memcpy(_creditsPalette, _creditsList[0].palette, sizeof(_creditsPalette));
	_game->display().applyPalette(_creditsPalette, kPalAll);
	Slot &s = slot(kCredits);
	if (s.sprite) {
		s.sprite->setDrawOrder(1);
		s.sprite->setDestination(_game->display().getPrimary());
		s.sprite->setVisibility(true);
	}
	_game->display().slotPlace(kArea, kCredits, kCredits, nullptr, nullptr, true, Common::Point(0, 0));
	s.y = 10;
	s.x = 0x4b;
	if (s.sprite)
		s.sprite->setPosition(0x4b, 10);
	spriteVisible(0x83, false);
	playEntrySound(kEntryCreditsSound, true);
	draw();
}

// 0x461320: 2 pixels every 41 ms, after a second's wait
void Options::creditsTick(uint32 now) {
	Slot &s = slot(kCredits);
	if (now <= s.deadline)
		return;
	_creditsCount++;
	s.deadline = now + kMsPerStep;
	if (_creditsCount > 0x18 && _creditsCount < 0xa09) {
		if (s.sprite) {
			s.sprite->setVisibility(true);
			s.y -= 2;
			s.sprite->setPosition(s.x, s.y);
		}
		draw();
	} else if (_creditsCount > 0xa39) {
		creditsEnd();
	}
}

// 0x461250
void Options::creditsEnd() {
	_game->freeList(_creditsList);
	_creditsList.clear();
	spriteVisible(kCredits, false);
	if (_state != kStateCredits)
		return;
	NafArchive *naf = _game->getNaf(kNafOptions);
	if (naf && !_list[kEntryBackdrop].bitmap)
		_game->res().load(naf, _list[kEntryBackdrop]);
	showBitmap(_list[kEntryBackdrop], 2, true);
	_game->display().applyPalette(_palette, kPalAll);
	spriteVisible(0x83, true);
	_state = kStateMenu;
	draw();
}

// ---- Drawing ----

// 0x460bd0: x1 and y1 inclusive for the bottom and right edges, like the original's loops
void Options::frameRect(Graphics::Surface &dst, int x0, int y0, int x1, int y1, byte color) {
	if (x0 >= x1 || y0 >= y1)
		return;
	auto plot = [&dst, color](int x, int y) {
		if (x >= 0 && y >= 0 && x < dst.w && y < dst.h)
			*(byte *)dst.getBasePtr(x, y) = color;
	};
	for (int x = x0; x < x1; x++)
		plot(x, y0);
	for (int y = y0; y < y1; y++)
		plot(x1, y);
	for (int x = x0; x <= x1; x++)
		plot(x, y1);
	for (int y = y0; y < y1; y++)
		plot(x0, y);
}

// 0x460a00: 11x11 capitals from the 288x11 font strips (the picked row in the second
// font), colour 10 transparent; a space advances 11, anything else nothing
void Options::drawName(Graphics::Surface &dst, int16 row) {
	bool picked = row == _slot;
	int x = _listX + 2;
	int y = row * _rowStep + _listY + 1;
	const char *text = ((_state == kStateSave || _state == kStateReplace) && picked) ? _edit : _names[row];
	const Request &fontReq = _list[kEntryFont + (picked ? 1 : 0)];
	const Graphics::Surface *font = fontReq.bitmap ? &fontReq.bitmap->bitmap.surface : nullptr;
	for (; *text; text++) {
		char c = (char)toupper((byte)*text);
		if (c < 'A' || c > 'Z') {
			x += c == ' ' ? 0xb : 0;
			continue;
		}
		int src = (c - 'A') * 0xb;
		int h = MIN(0x1e0 - y, 0xb);
		int w = 0xb;
		if (x + 0xb > 0x280)
			w = 0x280 - x;
		if (w + src > 0x120)
			w = 0x120 - src;
		if (font && w > 0 && h > 0) {
			for (int dy = 0; dy < h; dy++) {
				if (y + dy < 0 || y + dy >= dst.h || dy >= font->h)
					continue;
				for (int dx = 0; dx < w; dx++) {
					if (x + dx < 0 || x + dx >= dst.w || src + dx >= font->w)
						continue;
					byte p = *(const byte *)font->getBasePtr(src + dx, dy);
					if (p != 10)
						*(byte *)dst.getBasePtr(x + dx, y + dy) = p;
				}
			}
		}
		x += 0xb;
	}
}

// 0x460730
void Options::render(Graphics::Surface &dst, const Common::Rect &area, void *user) {
	Options *o = (Options *)user;
	switch (o->_state) {
	case kStateLoading:
		dst.fillRect(area, 0);
		return;
	case kStateLoad:
	case kStateSave:
	case kStateReplace:
		for (int16 row = 0; row < kSaveSlots; row++)
			o->drawName(dst, row);
		break;
	case kStateKeys:
		break;
	default:
		return;
	}
	if (!o->_blink || !o->_blinkOn)
		return;
	if (o->_cursor < 0) {
		frameRect(dst, o->_caretX, o->_caretY - 2, o->_caretX - 1 + o->_boxW, o->_caretY - 3 + o->_boxH, kCaretColor);
	} else {
		for (int y = o->_caretY; y < o->_caretY + 0xb; y++) {
			if (y >= 0 && y < dst.h && o->_caretX >= 0 && o->_caretX < dst.w)
				*(byte *)dst.getBasePtr(o->_caretX, y) = kCaretColor;
		}
	}
}

// ---- Saved games ----

// 0x459960
void Options::newGame() {
	_game->hints().newGame();
	const Common::Array<Scene *> &scenes = _game->getScenes();
	for (int i = scenes.size() - 1; i >= 0; i--) {
		scenes[i]->shutdown();
		const Common::Array<Scene *> &subs = scenes[i]->getSubScenes();
		for (int j = subs.size() - 1; j >= 0; j--)
			subs[j]->shutdown();
	}
}

// 0x459f50: scene, sub (-1 for a top-level scene), size, then what m9 writes
bool Options::writeRecord(Common::WriteStream &out, Scene *scene) {
	int size = scene->getSaveSize();
	if (scene->getParent()) {
		out.writeSint32LE(scene->getParent()->getId());
		out.writeSint32LE(scene->getId());
	} else {
		out.writeSint32LE(scene->getId());
		out.writeSint32LE(-1);
	}
	out.writeUint32LE(size);
	if (size > 0) {
		Common::Array<int32> data((size + 3) / 4, 0);
		scene->saveState(data.data());
		for (int i = 0; i < size / 4; i++)
			out.writeSint32LE(data[i]);
		for (int i = size & ~3; i < size; i++)
			out.writeByte((byte)(data[i / 4] >> ((i & 3) * 8)));
	}
	return !out.err();
}

// 0x459d90. A "NNSV" tag, version and scene count come first so the scenes can be
// matched when more are ported; the rest is the original's layout.
bool Options::writeGame(Common::WriteStream &out) {
	const Common::Array<Scene *> &scenes = _game->getScenes();
	out.writeUint32BE(kSaveTag);
	out.writeUint32LE(kSaveVersion);
	out.writeUint32LE(scenes.size());
	Scene *current = _game->getCurrentScene();
	if (current && current->getParent()) {
		out.writeSint32LE(current->getParent()->getId());
		out.writeSint32LE(current->getId());
	} else {
		out.writeSint32LE(current ? current->getId() : 0);
		out.writeSint32LE(-1);
	}
	for (int i = scenes.size() - 1; i >= 0; i--) {
		if (!writeRecord(out, scenes[i]))
			return false;
		const Common::Array<Scene *> &subs = scenes[i]->getSubScenes();
		out.writeUint32LE(subs.size());
		for (int j = subs.size() - 1; j >= 0; j--) {
			if (!writeRecord(out, subs[j]))
				return false;
		}
	}
	int32 hints[Hints::kSaveSize / 4];
	_game->hints().saveState(hints);
	out.writeUint32LE(Hints::kSaveSize);
	for (int i = 0; i < Hints::kSaveSize / 4; i++)
		out.writeSint32LE(hints[i]);
	return !out.err();
}

// 0x459b90: the record goes to the scene it names, if that scene exists
bool Options::readRecord(Common::SeekableReadStream &in) {
	int32 id = in.readSint32LE();
	int32 sub = in.readSint32LE();
	uint32 size = in.readUint32LE();
	if (in.err() || in.eos() || size > 0x100000)
		return false;
	if (size == 0)
		return true;
	Common::Array<int32> data((size + 3) / 4, 0);
	for (uint32 i = 0; i < size / 4; i++)
		data[i] = in.readSint32LE();
	for (uint32 i = size & ~3; i < size; i++)
		data[i / 4] |= in.readByte() << ((i & 3) * 8);
	if (in.err() || in.eos())
		return false;
	Scene *scene = _game->getScene(id, sub);
	if (scene)
		scene->restoreState(data.data(), size);
	else
		debugC(1, kDebugLoading, "Saved state of scene %d:%d skipped (not ported)", id, sub);
	return true;
}

// 0x4599d0: every scene is reset first, as the original does before reading anything
bool Options::readGame(Common::SeekableReadStream &in, int &scene, int &sub) {
	if (in.readUint32BE() != kSaveTag || in.readUint32LE() != kSaveVersion)
		return false;
	uint32 count = in.readUint32LE();
	if (in.err() || count > 64)
		return false;
	newGame();
	scene = in.readSint32LE();
	sub = in.readSint32LE();
	for (uint32 i = 0; i < count; i++) {
		if (!readRecord(in))
			return false;
		uint32 subs = in.readUint32LE();
		if (in.err() || subs > 64)
			return false;
		for (uint32 j = 0; j < subs; j++) {
			if (!readRecord(in))
				return false;
		}
	}
	uint32 size = in.readUint32LE();
	if (in.err() || in.eos())
		return false;
	if (size == Hints::kSaveSize) {
		int32 hints[Hints::kSaveSize / 4];
		for (int i = 0; i < Hints::kSaveSize / 4; i++)
			hints[i] = in.readSint32LE();
		_game->hints().loadState(hints, size);
	} else {
		_game->hints().loadState(nullptr, size);
		in.skip(size);
	}
	if (in.err())
		return false;
	if (!_game->getScene(scene, sub)) {
		warning("Saved game is in scene %d:%d, which isn't ported; starting at the quilt", scene, sub);
		scene = 0;
		sub = -1;
	}
	return true;
}

} // End of namespace NightmareNed
