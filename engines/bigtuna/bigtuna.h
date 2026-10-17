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

#ifndef BIGTUNA_BIGTUNA_H
#define BIGTUNA_BIGTUNA_H

#include "common/array.h"
#include "common/random.h"
#include "common/rect.h"
#include "common/str.h"
#include "engines/advancedDetector.h"
#include "engines/engine.h"

#include "bigtuna/value.h"

namespace BigTuna {

class ImpPlayer;
class Movie;
class ScorePlayer;
class Script;
class Sound;
class Stage;
struct Sprite;

class BigTunaEngine : public Engine {
public:
	BigTunaEngine(OSystem *syst, const ADGameDescription *gameDesc);
	~BigTunaEngine() override;

	Common::Error run() override;
	bool hasFeature(EngineFeature f) const override;

	bool isV1() const;
	bool isImp() const;

	// Used by the Just Me and My Dad player
	Stage *getStage() const { return _stage; }
	void presentStage();
	void setPaletteData(const byte *pal);
	const byte *getPalette() const { return _palette; }
	void pumpEvents() { pollEvents(); }
	bool takeClick(Common::Point &p);
	byte takeKey();
	bool isFast() const { return _fast; }
	void frameDone();
	Movie *getMovie() const { return _movie; }
	ScorePlayer *getPlayer() const { return _player; }

	void setPalette(Movie *movie, uint res);
	// Loads the movie once the running script has stopped; frame is void, a number or a label
	void requestMovie(const Common::String &name, const Value &frame);
	void waitForSound(int channel);
	void updateStage();
	void redraw();
	bool saveShot(const Common::String &path);

	void setCursor(int id);
	void setCursorMember(uint res);
	// Topmost visible sprite under a point (FUN_00407800)
	const Sprite *spriteAt(int x, int y, bool needScript, uint *channel = nullptr) const;
	bool spritesIntersect(int a, int b);

	Value getPref(const Common::String &name);
	void setPref(const Common::String &name, const Common::String &text);

	Sound *_sound;
	Script *_script;
	Common::RandomSource _rnd;

	Common::Point _mousePos;
	bool _mouseDown;
	bool _doubleClick;
	byte _keyChar;
	uint _clickOn;
	uint32 _timerStart;

private:
	bool loadMovie(const Common::String &name, const Value &frame);
	void mainLoop();
	void runStep(uint frames);
	void pollEvents();
	void dispatchMouse();
	void sweepMovies();
	void readTestHooks();
	void checkTestHooks();
	void checkFrameShot();
	Common::String where() const;
	Common::Array<Common::Point> fuzzTargets() const;

	const ADGameDescription *_gameDescription;
	Movie *_movie;
	ScorePlayer *_player;
	Stage *_stage;
	byte _palette[256 * 3];
	int _cursorId;

	bool _movieRequested;
	Common::String _requestName;
	Value _requestFrame;

	bool _clicked, _moved;
	uint32 _lastClickTime;
	uint _hoverChannel;

	// Test hooks
	uint32 _startTime;
	bool _fast;
	uint _stopAfter;
	uint _framesPlayed;
	Common::Array<uint> _shots;
	Common::String _shotDir;
	struct TimedAction {
		uint32 time;
		char type; // 'c' click, 'k' key, 's' shot
		int x, y;
	};
	Common::Array<TimedAction> _actions;
	uint32 _fuzzNext;
	bool _fuzz;
	uint32 _quitAt;
	ImpPlayer *_imp;
};

} // End of namespace BigTuna

#endif // BIGTUNA_BIGTUNA_H
