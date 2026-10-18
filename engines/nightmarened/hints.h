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

#ifndef NIGHTMARENED_HINTS_H
#define NIGHTMARENED_HINTS_H

#include "common/array.h"
#include "common/rect.h"

#include "nightmarened/ned.h"
#include "nightmarened/resources.h"

namespace NightmareNed {

class Game;

/**
 * Ned's visions (NITENED.EXE 0x472960-0x4759e0): a swirl (area-9 slot 50, NED92000 scene 1)
 * opens beside Ned and shows one of the world's shadow creatures. Each of the five worlds
 * has six trigger points, three of them enabled at random per game; each enabled one
 * raises the world's level (1..3). Levels 1 and 2 are a short glimpse; level 3 is a longer
 * scene with spoken lines while Ned plays action 0x3b/0x3c. When every world reaches
 * level 3 the game ends (the end clip and the credits, 0x4613d0).
 *
 * While a vision plays, the global input hook (0x472a30) passes only button 2 on to the scene.
 */
class Hints {
public:
	enum {
		kWorlds = 5,
		kTriggers = 6,
		kSaveSize = 0xf0
	};

	explicit Hints(Game *game);
	~Hints();

	/** 0x472960 + 0x472970: no hook; three of each world's six triggers enabled at random */
	void newGame();

	/**
	 * 0x475180: trigger point index (0..5) of the current world. facingRight is the side the
	 * swirl opens on; (x, y) is Ned's world position; xform maps world to screen (the scene's
	 * scroll). Returns 1 when a vision starts.
	 */
	int trigger(int index, bool facingRight, int x, int y, NedXform xform, void *user);

	/** 0x475970: a vision is playing (the hook is 0x472a30) */
	bool isActive() const { return _hooked; }
	/** 0x4759c0 (WinMain, on quit to the menus): ends the vision now */
	void cancel();

	/** 0x4758f0 / 0x475900 / 0x475920: the 60 trigger flags (triggered, then enabled) */
	void saveState(int32 *out) const;
	void loadState(const int32 *in, int size);

	/** 0x467680: the level reached in scene's world (0..3) */
	int getLevel(int scene) const;
	/** 0x467600 */
	void raiseLevel(int scene);
	/** 0x475a60: every world is at level 3 */
	bool allLevelsDone() const;
	/** DAT_0051eb28: the five levels and +0x14 (0x467670); scene 0 (0x465540..0x465600) owns its lifetime */
	int32 _progress[6];

private:
	static void inputHook(Game *game, uint32 input, uint32 now, uint32 &out);
	void hook(uint32 input, uint32 now, uint32 &out);
	static void actionDone(void *user, int16 action);

	int load();
	bool show();
	void end();
	void ending();
	void creditsStart();
	bool creditsStep(uint32 now);
	void creditsEnd();

	int stepGlimpse(uint32 now, int16 content);
	int stepWorld0(uint32 now);
	int stepWorld1(uint32 now);
	int stepWorld2(uint32 now);
	int stepWorld3(uint32 now);
	int stepWorld4(uint32 now);
	int notDue();
	int stepTail(uint32 now, int result);

	Slot &slot(int id);
	int place(int id);
	int16 switchTo(int id);
	void copyPos(int dst, int src);
	void flip(int id);
	void playSound(int id);
	void playVoice(int id);
	void pollVoice();
	void unhook();

	Game *_game;
	Common::Array<Request> _list;     // the world/level list (0x4b3578 ...), NED92000 scene 1
	Common::Array<Request> _creditsBg;  // 0x4b0980
	Common::Array<Request> _creditsSpr; // 0x4b095c
	bool _hooked;          // DAT_0051f760 == 0x472a30

	int32 _triggered[kWorlds * kTriggers]; // DAT_0051fb70
	int32 _enabled[kWorlds * kTriggers];   // DAT_0051fbe8
	int _scene;            // DAT_0051fc68
	int _world;            // DAT_0051fc70
	int _level;            // DAT_0051fc74
	int _facing;           // DAT_0051fc60
	Common::Point _pos;    // DAT_0051f750 / DAT_0051f754
	NedXform _xform;       // DAT_0051fc78
	void *_xformUser;
	bool _optionsWere;     // DAT_0051fc90
	bool _naf;             // DAT_0051fc6c: NED92000 is open
	int _start;            // DAT_0051fc7c
	int _abort;            // DAT_0051fc8c
	int _pending;          // DAT_004b4474: the list is not shown yet
	int _pass;             // DAT_004b4470: 2 once the swirl has opened
	int _done;             // DAT_0051fc94
	int _waitNed;          // DAT_0051fc9c
	int16 _cur;            // DAT_004b3568: the slot playing
	bool _noMirror;        // DAT_004b3570
	bool _voiceDone;       // DAT_0051fc80 (set by 0x475990 when a line ends)
	int _voiceSlot;        // the slot whose line 0x475990 waits for
	int _talkIndex;        // DAT_004b356c
	int _talking;          // DAT_0051fc84
	int _w2Started;        // DAT_0051fc88
	int _w4Voice;          // DAT_0051fca0
	int _w3Voice;          // DAT_0051fca4 (written only)
	int _w0Voice;          // DAT_0051fca8
	int _w1Voice;          // DAT_0051fcac
	int _w2Voice;          // DAT_0051fcb0
	int _creditsCount;     // DAT_0051d85c
};

} // End of namespace NightmareNed

#endif // NIGHTMARENED_HINTS_H
