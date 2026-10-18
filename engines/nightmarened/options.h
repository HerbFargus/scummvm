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

#ifndef NIGHTMARENED_OPTIONS_H
#define NIGHTMARENED_OPTIONS_H

#include "common/array.h"
#include "common/rect.h"
#include "common/stream.h"
#include "graphics/surface.h"

#include "nightmarened/data.h"
#include "nightmarened/resources.h"

namespace NightmareNed {

class Game;
class RdxSurface;
class Scene;

/**
 * The options screen (NITENED.EXE 0x45da60-0x461320): Ned's head with eight items, picked
 * with left/right and Space. Music sets the volume; "Who made this?" scrolls the credits;
 * "I'm done" quits; "Play a new game" restarts at the quilt; "Play an old game" and "Save
 * this game" list the eight saved games (typed names); "What keys do I use?" shows the
 * keys; "Take me back to the game" closes it. It draws over the scene from NED86000 and a
 * render-callback surface for the names and the blinking caret, and game time stops.
 *
 * Also the saved-game format (0x459960, 0x4599d0, 0x459b90, 0x459d90, 0x459f50).
 */
class Options {
public:
	enum {
		kSaveSlots = 8,
		kNameSize = 12
	};

	explicit Options(Game *game);
	~Options();

	/** 0x45da60 (start-up): the sprite places (86999019.SST), NED86000, the music level */
	int init();
	/** DAT_0051dce4 */
	bool isOpen() const { return _open; }
	/** 0x45db30 */
	int open();
	/**
	 * 0x45e060, called instead of the scene tick while open. keys: the arrows (1, 2, 4,
	 * 0x20) and Space (0x200), read straight from the keyboard (0x45e650)
	 */
	void tick(uint32 keys, uint32 now);
	/** 0x45e740: WM_KEYDOWN while open (a Windows virtual-key code) */
	void keyDown(uint32 vk);
	/** 0x45df80(0): closes for a load made from the ScummVM menu */
	void closeForLoad();

	/** 0x459960: Hints::newGame (0x472970), then every scene's m1, last first */
	void newGame();
	/** 0x459d90 + 0x459f50: everything after the 12-byte name */
	bool writeGame(Common::WriteStream &out);
	/** 0x4599d0 + 0x459b90: resets every scene, then restores them; scene and sub to switch to */
	bool readGame(Common::SeekableReadStream &in, int &scene, int &sub);

private:
	enum {
		kArea = 1,
		kNafOptions = 2,           // ned86000
		kList = 0x4b0398,          // 42 entries
		kListCount = 0x2a,
		kCreditsList = 0x4b0980,   // the swirl bitmap with its palette
		kItems = 0x4b0318,         // the 8 item slots in left/right order
		kDialogs = 0x4b0290,       // per dialog: slot, ok x, y, cancel x, y (-1 none)
		kOk = 0x8a,
		kCancel = 0x8b,
		kKnob = 0x16d,
		kVolume = 0x16e,
		kCredits = 0x95,
		kCaretColor = 0x22,
		kRepeatMs = 0xfa,
		kSaveTag = MKTAG('N', 'N', 'S', 'V'),
		kSaveVersion = 1
	};

	enum State {
		kStateLoading = 0,
		kStateMenu = 1,
		kStateLoad = 2,
		kStateSave = 3,
		kStateReplace = 4,
		kStateMusic = 5,
		kStateCredits = 6,
		kStateKeys = 7,
		kStateKeysHelp = 8,
		kStateDialog = 9,
		kStateNotice = 10
	};

	Slot &slot(int id);
	void spriteVisible(int id, bool on);
	/** 0x459100 res_show_bitmap */
	void showBitmap(Request &req, int z, bool visible);
	/** 0x45e6c0: plays entry's sound (stopping the last one); waits for it unless async */
	void playEntrySound(int entry, bool async);
	/** The end-of-sound waits on DAT_0051dcfc */
	void waitSound();
	void draw();

	/** 0x45df80: close, restart the clock, maybe restore the palette */
	void close(bool restorePalette);
	/** 0x45f050 */
	void closeQuiet();
	/** 0x460d40: every item and dialog sprite, z 1, hidden */
	void attachSprites();
	/** 0x45e980 */
	void showDialog(int16 dialog);
	/** 0x45f100 */
	void hideDialog();
	/** 0x45f150 */
	void toggleButton();
	bool isRepeat(uint32 input, uint32 now);

	/** 0x45eab0: ok / cancel on a dialog */
	bool dialogInput(uint32 input, uint32 now);
	/** 0x45f1b0 */
	bool musicInput(uint32 input, uint32 now);
	/** 0x460690 */
	void showVolume(int level);
	/** 0x45f3c0 */
	bool loadInput(uint32 input, uint32 now);
	/** 0x45f8f0 */
	bool saveInput(uint32 input, uint32 now);
	/** 0x460350 */
	void openSave();
	/** 0x460260: the names of the 8 saved games; returns how many exist */
	int readNames();
	/** 0x460580 */
	bool writeSave(int index, const char *name);
	/** 0x45feb0 */
	void selectNext(bool skipEmpty);
	/** Copies a name into the edit field and puts the caret round its row */
	void selectRow(int16 row);
	/** 0x45ffa0 */
	void editErase(bool forward, bool erase);

	// The caret (0x460150, 0x460cb0, 0x460d00)
	void setCaret(int16 row, int16 column);
	bool caretTick(uint32 now);
	bool caretShow(bool on);
	void caretOff();

	// Credits
	void creditsStart();
	void creditsTick(uint32 now);
	/** 0x461250 */
	void creditsEnd();

	/** 0x460730 FX_RENDER_CALLBACK */
	static void render(Graphics::Surface &dst, const Common::Rect &area, void *user);
	/** 0x460a00 */
	void drawName(Graphics::Surface &dst, int16 row);
	/** 0x460bd0 */
	static void frameRect(Graphics::Surface &dst, int x0, int y0, int x1, int y1, byte color);

	bool writeRecord(Common::WriteStream &out, Scene *scene);
	bool readRecord(Common::SeekableReadStream &in);

	Game *_game;
	bool _open;                 // DAT_0051dce4
	int16 _state;               // DAT_0051d44c
	int16 _item;                // DAT_0051d854
	int16 _dialog;              // DAT_0051d8d4
	int16 _button;              // DAT_0051cde8: kOk or kCancel
	uint32 _lastInput;          // DAT_0051dcdc
	uint32 _lastInputTime;      // DAT_0051dce0
	uint32 _openedAt;           // DAT_0051cde4
	bool _nedWasEnabled;        // DAT_0051dcf0
	byte _savedPalette[256 * 3]; // 0x51d450
	byte _palette[256 * 3];     // 0x51d8d8
	byte _creditsPalette[256 * 3]; // 0x51cf40
	char _names[kSaveSlots][kNameSize + 1]; // DAT_0051d870
	char _edit[kNameSize + 4];  // DAT_0051cdf0
	int16 _slot;                // DAT_0051cf38: the saved game picked
	int16 _cursor;              // DAT_0051cf2c: -1 = the whole row is picked
	int16 _listX, _listY;       // DAT_0051cf18, DAT_0051cf1c
	int16 _rowStep;             // DAT_0051cf24
	int16 _boxW, _boxH;         // DAT_0051cddc, DAT_0051d868
	int16 _caretX, _caretY;     // DAT_0051cf04, DAT_0051cf00
	uint32 _blinkTime;          // DAT_0051cf08
	bool _blink, _blinkOn;      // DAT_0051dcf4, DAT_0051dcf8
	int _keyChoice;             // DAT_0051cf20: keyboard, joystick, gamepad
	int _creditsCount;          // DAT_0051d85c
	Slot *_lastSound;           // DAT_0051dd00
	int16 _volume;              // DAT_004b0288: 0..10
	Common::Array<Placement> _placements; // DAT_0051cf28
	Common::Array<Request> _list;         // 0x4b0398
	Common::Array<Request> _creditsList;  // 0x4b0980
	RdxSurface *_surface;       // DAT_0051d8d0
};

} // End of namespace NightmareNed

#endif // NIGHTMARENED_OPTIONS_H
