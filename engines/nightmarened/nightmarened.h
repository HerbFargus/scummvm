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

#ifndef NIGHTMARENED_NIGHTMARENED_H
#define NIGHTMARENED_NIGHTMARENED_H

#include "common/array.h"
#include "common/keyboard.h"
#include "common/str.h"
#include "audio/mixer.h"
#include "engines/advancedDetector.h"
#include "engines/engine.h"
#include "graphics/surface.h"

#include "nightmarened/resources.h"

namespace Common {
class InstallShieldV3;
}

namespace NightmareNed {

class Display;
class Game;
class Options;
class NafArchive;
class PlacementDemo;
struct Background;
struct SpriteFrame;

enum {
	kScreenWidth = 640,
	kScreenHeight = 480
};

/** The NAF files in NITENED.EXE's file table order (0x4a05d0) */
extern const int kNafNumbers[12];

class NightmareNedEngine : public Engine {
public:
	NightmareNedEngine(OSystem *syst, const ADGameDescription *gameDesc);
	~NightmareNedEngine() override;

	Common::Error run() override;
	bool hasFeature(EngineFeature f) const override;

	// Saved games: ScummVM slots 1..8 are the game's GAME01..GAME08
	bool canLoadGameStateCurrently(Common::U32String *msg = nullptr) override;
	bool canSaveGameStateCurrently(Common::U32String *msg = nullptr) override;
	Common::Error loadGameStream(Common::SeekableReadStream *stream) override;
	Common::Error saveGameStream(Common::WriteStream *stream, bool isAutosave = false) override;
	Common::Error saveGameState(int slot, const Common::String &desc, bool isAutosave = false) override;
	/** The options screen's save (0x460580): name is the 12-byte name the game lists */
	bool saveSlot(int slot, const Common::String &name);

	/** Reads pending events into the key state (for the loops that run inside a tick) */
	void pumpEvents();
	/** Whether a key is held now (the DirectInput key state, 0x488770) */
	bool isKeyDown(Common::KeyCode key) const;
	Options *getOptions() { return _options; }

	/** Opens a game file from the CD, or from DATA32.Z (DATA\, VIDEO\, SOUND\) */
	Common::SeekableReadStream *openFile(const Common::String &name);
	/** Opens NEDnnnnn.NAF (number = 0, 1000, 86000, ...) */
	NafArchive *openNaf(int number);

	/** Plays a Smacker video; false if the player pressed Escape (skip the rest) */
	bool playVideo(const Common::String &name);
	/** DI, CAPERS, WINDP and INTRO, as NITENED.EXE 0x445990 */
	void playIntro();

	const ExeImage &getExe() const { return _exe; }

private:
	bool mountData();
	void viewer();
	void runDemo();
	void runGame();
	void mainLoop(PlacementDemo *demo);
	/** The game's input bitmask from the keys held now (0x446700) */
	uint32 readInput();
	bool showBackground(int index);
	void showNaf(int nafIndex);
	void redraw();
	void playMusic(int nafNumber);
	bool saveShot(const Common::String &path);
	void dumpChecksums(const Common::String &path);

	const ADGameDescription *_gameDescription;
	Common::InstallShieldV3 *_data32;
	ExeImage _exe;
	Game *_game;
	Options *_options;
	bool _idle;                 // between two main-loop iterations: the ScummVM menu may save or load
	Common::String _saveName;   // the 12-byte name saveGameStream writes first
	Display *_display; // owned by _game
	Common::Array<bool> _keysDown;
	Graphics::Surface _screen;
	byte _palette[256 * 3];

	// Resource viewer (stands in for the game until the scene code exists)
	NafArchive *_naf;
	int _nafIndex;
	Common::Array<int> _bgKeys; // scene << 16 | id
	int _bgIndex;
	Graphics::Surface _view;    // the background, or a level stitched from strips
	int _scrollX, _scrollY;
	Common::Array<SpriteFrame *> _frames;
	int _spriteIndex, _frameIndex;
	bool _showSprite;
	Audio::SoundHandle _musicHandle;
};

} // End of namespace NightmareNed

#endif // NIGHTMARENED_NIGHTMARENED_H
