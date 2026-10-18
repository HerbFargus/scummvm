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

#ifndef NIGHTMARENED_SCENE5_H
#define NIGHTMARENED_SCENE5_H

#include "common/array.h"
#include "common/hashmap.h"
#include "common/rect.h"

#include "nightmarened/data.h"
#include "nightmarened/game.h"
#include "nightmarened/ned.h"
#include "nightmarened/resources.h"
#include "nightmarened/world.h"

namespace NightmareNed {

/**
 * What scene 5 and its two sub-scenes share: NED90000 (file 3, DAT_004baee0), area 6,
 * Ned's list 0x16, request lists kept by VA, a collision world, a target list, an
 * .SST placement table, and the slot helpers. Every sprite position here is a screen
 * position (slot_place_nx, no xform); the cameras move the slots by the scroll delta.
 */
class Scene5Base : public Scene {
public:
	Scene5Base(Game *game, int id);
	~Scene5Base() override;

protected:
	enum {
		kArea = 6,
		kNaf = 3,          // ned90000
		kNedList = 0x16,
		kSstAreaIndex = 9, // 0x4afcd8[9] = 90
		kSstAreas = 0x4afcd8,
		kMusic = 0x85,     // 90002208.wav
		kKey = 10          // the colour key of every bitmap here
	};

	typedef Common::HashMap<uint32, Request> EntryMap;

	// Resources
	Slot &slot(int id);
	void visible(int id, bool on);
	void order(int id, int z);
	BitmapRec *bmp(uint32 va);
	CollisionMap *map(uint32 va);
	int submit(uint32 va, int count, int scene);
	void release(uint32 va, int count);
	bool isLoaded(uint32 va) const { return _entries.contains(va); }
	void freeAll();
	void attachList(uint32 va, int count);
	void saveVisibility(uint32 list, int count, bool restore, Common::Array<int> &state);
	BitmapRec *showBitmap(uint32 va, int z);

	// Slots (area 6)
	int16 switchSlot(int newSlot, int oldSlot);
	int place(int id, int ref);
	bool step(int id, int ref, uint32 now, int16 duration = 0);
	int16 start(int newSlot, int oldSlot, int ref, uint32 now, int16 stepIndex);
	void nudge(int id, int dx, int dy);
	void showSlot(int id, int z, bool on);
	void attachPos(int id, int ref, int out[2]);
	static bool intersect(const int32 a[4], const int32 b[4]);

	// Sound
	void playSound(int id);
	void loopSound(int id);
	void stopSound(int id);
	void stopAllSounds();

	// Collision
	void addMap(CollisionMap *m);
	void addMap(CollisionMap *m, const int32 rect[4]);
	void removeMap(CollisionMap *m);
	static void bounds(const CollisionMap *m, int32 out[4]);
	void regionRects(CollisionMap *m, int32 (*out)[4], int first, int count);
	int nedInRect(const int32 (*rects)[4], int count);
	Common::Array<CollisionMap *> overlayMaps() const;

	// Targets and the .SST table
	void targetAdd(int id, bool noFlip);
	void targetRemove(int id);
	int findTarget(int16 &dir, int16 &found, int32 rect[4], int scrollX, int scrollY);
	bool loadSst(int n);
	void applySst(int dx, int dy);

	// Ned and the scene graph
	Ned *ned();
	void nedTick(uint32 input, uint32 now, int scrollX, int scrollY);
	void nedLoad(int fromScene);
	void hint(int which, int x, int y, NedXform xform);
	int switchTo(int scene, int sub);

	EntryMap _entries;                              // loaded request entries by VA
	Common::HashMap<uint32, BitmapRec *> _bitmaps;  // bitmap records by destination VA
	Common::HashMap<uint32, CollisionMap *> _maps;  // collision maps by destination VA
	byte _palette[256 * 3];
	uint32 _paletteVA;
	CollisionWorld _world;
	Common::Array<Placement> _targets;
	Common::Array<Placement> _sst;
	NedHooks _hooks;
	Common::Point _nedPos;     // DAT_00525df8 / DAT_00525dfc
	int _unportedExit;         // the last exit to a scene that isn't registered (logged once)
	bool _nedListLoaded;       // the test hooks enter a sub-scene without scene 5
	uint32 _lastDraw;
};

/**
 * Scene 5, the plumbing under the bathroom (registered by 0x41de90; NED90000 scene 1,
 * area 6). A 4608 x 1600 level of 45 tiles (9 x 5, 512 x 320) that wraps around
 * horizontally, in front of two parallax layers. Walking bathtubs carry Ned (each step
 * of their walk has its own floor map); drips from the pipes electrify them; a lever
 * drives the drain that pulls Ned down; rats drop from the ceiling and 13 of them caught
 * open a way; a tentacle pokes out of the pipes. Ladders lead up to a toilet (sub-scene
 * 0) and down to scenes 1 and 3, the outhouse leads to the attic.
 */
class Scene5 : public Scene5Base {
public:
	explicit Scene5(Game *game);

	int init() override;                             // m0 0x419c00
	void shutdown() override;                        // m1 0x419120
	int load(int fromScene, int fromSub) override;   // m2 0x419140
	int enter(int fromScene, int fromSub) override;  // m3 0x4191e0
	int leave(int toScene, int toSub) override;      // m4 0x419c50
	int unload(int toScene, int toSub) override;     // m5 0x419d00
	int tick(uint32 input, uint32 now) override;     // m7 0x419ff0
	int getSaveSize() const override { return 4; }   // m8 0x41dfb0
	void saveState(int32 *out) override;             // m9 0x41dfc0
	void restoreState(const int32 *in, int size) override; // m10 0x41dfd0

private:
	enum {
		kMainList = 0x49ade0,   // 0x9c entries, NAF scene 1
		kMainCount = 0x9c,
		kMainLast = 0x49c3ac,   // the last entry, waited for by m3
		kTileList = 0x49c3d0,   // 45 tiles, NAF scene 99
		kTileCount = 0x2d,
		kTileDest = 0x4f4928,   // DAT_004ddb88[(9 * 15 + 99) * 100]
		kMaps = 0x4bac60,       // cbix 1..11
		kTubMaps = 0x4bada8,    // cbix 20..39, one per step of a tub's walk
		kTubMapCount = 20,
		kPaletteVA = 0x4ba5e8,
		kBackdrop = 0x4eb010,   // bitmap 2, 640 x 480, fixed
		kMidLayer = 0x4eb00c,   // bitmap 1, 1280 wide, scrolls at 1/6
		kFarLayer = 0x4eb014,   // bitmap 3, 2560 wide, scrolls at 1/3
		kAltMusic = 0x86,       // 90012208.wav
		kTubs = 9,
		kWorldW = 0x1200,
		kRatsNeeded = 13
	};

	/** The tile grid record DAT_004bae18 */
	struct Grid {
		int area, scene, x, y, cellW, cellH, cols, rows;
		uint32 list;
	};

	CollisionMap *mapN(int n) { return map(kMaps + 4 * (n - 1)); }
	CollisionMap *tubMap(int i) { return i >= 0 && i < kTubMapCount ? map(kTubMaps + 4 * i) : nullptr; }

	// The level (scene5.cpp)
	void startLevel(uint32 now);
	void clipBreak(uint32 now);
	void exitDrop();

	// Camera and tiles (scene5_level.cpp)
	static void xform(void *user, int &x, int &y);
	void camera(uint32 now, int out[2]);
	void cameraLimits(int32 r[4]);
	void cameraWant(uint32 now, int out[2]);
	void streamTiles();
	void streamRow(int first, int last, int row);
	void exitRegions();

	// Hooks (scene5_level.cpp)
	static int hookUse(void *user, int x, int y, int16 *state);
	static int hookRegion(void *user, int32 id, uint32 flags, int x, int y, int16 *state);
	static void hookFindTarget(void *user, int x, int y, int16 *a, int16 *b, int32 rect[4]);
	static int hookHitTarget(void *user, int x, int y, int16 *a, int16 *b, const int32 rect[4]);

	// The actors (scene5_actors.cpp)
	void tubsTick(uint32 now, int dx, int dy);
	void drainTick(uint32 now, int dx, int dy);
	void ratsTick(uint32 now, int dx, int dy);
	void dripsTick(uint32 now, int dx, int dy);
	void leverTick(uint32 now, int dx, int dy);
	void doorTick(uint32 now, int dx, int dy);
	void tentacleTick(uint32 now, int dx, int dy);
	void restartDrain(uint32 now);

	// ---- Test hook state ----
	int _warpTest;            // nightmarened_s5_warp=<map>: the dev keys 1..9 (0x419ff0), once
	Common::Point _posTest;   // nightmarened_s5_pos=<x>,<y>: Ned is put there once he stands idle
	int _exitTest[2];         // nightmarened_s5_exit=<scene>:<sub>: that exit is taken once Ned stands idle

	// ---- .data ----
	int16 _tubA[kTubs];       // DAT_0049ca28: the tubs 61..67, 168, 169
	int16 _tubB[kTubs];       // DAT_0049ca40: their legs 68..74, 175, 176
	int _tubOrder;            // DAT_0049ca54: 0x72 behind the floor, 7 in front
	int _normalSpeed;         // DAT_0049ca58: Ned walks at the normal speed (not on a tub)
	int _exitClip;            // DAT_0049add8: the TV clip on the way out

	// ---- .bss ----
	uint32 _ratTime;          // DAT_004ba5c0: the last rat drop
	int _drainPull;           // DAT_004ba5c4: Ned is pulled down the drain next tick
	int _cameraFrozen;        // DAT_004ba5c8: regions 0x457/0x458, the drop through the pipe
	int16 _ratSlot[3];        // DAT_004ba5d8: the slot each of the three rat lanes shows
	int16 _leverSlot;         // DAT_004ba5e0: 0x6c (up, a target) or 0x52 (down)
	int _drainWant;           // DAT_004ba5e4
	int32 _tubMapBounds[kTubMapCount][4]; // DAT_004ba9f0: the tub maps' own bounds
	int16 _ratLanded;         // DAT_004bab30
	int16 _splashEnd[kTubs];  // DAT_004bab38
	uint32 _dripTime[kTubs];  // DAT_004bab50
	int _drainDelta;          // DAT_004bab74
	int16 _dropSlot[kTubs];   // DAT_004bab78: the drop falling from each pipe, or -1
	int32 _scene3Rect[4];     // DAT_004bab90: map 6's bounds, 100 high: the fall to scene 3
	int32 _exitRects[10][4];  // DAT_004baba8: regions 100..109 of map 11
	int32 _ratsCaught;        // DAT_004bac48 (saved; the low 16 bits count)
	int _climbEase;           // DAT_004bac4c
	int _doorWant;            // DAT_004bac50
	int _midRemX, _midRemY;   // DAT_004bac58 / DAT_004bac5c
	int32 _musicRect[4];      // DAT_004bac90: the alternative music plays here
	int16 _tentaclePos;       // DAT_004baca0: 0..7 at slots 0x118.., or -1
	int _scrollX, _scrollY;   // DAT_004baca8 / DAT_004bacac: world -> screen
	int16 _doorSlot;          // DAT_004bacb0: 0xb7 (shut) or 0x53 (open)
	int16 _tubMapIndex;       // DAT_004bacb4: the tub map in the world, or -1
	int16 _drainSlot;         // DAT_004bacb8: 4 (still) or 5 (turning)
	int32 _tubZones[kTubs][4]; // DAT_004bacc0: regions 200..208 of map 11
	BitmapRec *_backdrop;     // DAT_004bad50
	int _doorState;           // DAT_004bad54
	int _farRemX, _farRemY;   // DAT_004bad58 / DAT_004bad5c
	int _drainState;          // DAT_004bad70
	int _leverWant;           // DAT_004bad74
	BitmapRec *_midLayer;     // DAT_004bad7c
	int _ledgeEase;           // DAT_004bad80
	int16 _dripState[kTubs];  // DAT_004bad88: 0xf0 (dripping) or 0xfa (about to drop)
	int16 _ratLane;           // DAT_004bad9c: the lane a rat falls in, or -1
	int _leverState;          // DAT_004badf8
	int16 _dropFall[kTubs];   // DAT_004bae00
	int16 _ratZapped;         // DAT_004bae14
	Grid _grid;               // DAT_004bae18
	int32 _drainRect[1][4];   // DAT_004bae40: region 300 of map 11
	int16 _ratFall;           // DAT_004bae50
	int16 _tubZone;           // DAT_004bae54
	int _fallDone;            // DAT_004bae58 (set by 0x41e050 when slot 125's sound ends)
	uint32 _tentacleTime;     // DAT_004bae5c
	int _drainSpeed;          // DAT_004bae60: ticks per step << 8
	int16 _splash[kTubs];     // DAT_004bae80: 3 = a drop landed, 1 = sparking
	int _clipPending;         // DAT_004bae94: region 9999, clip 0x1f
	int _drainIdle;           // DAT_004bae98
	int _ratsDone;            // DAT_004bae9c
	int _hint1Armed;          // DAT_004baea0
	int _hint3Armed;          // DAT_004baea4
	int _exitStage;           // DAT_004baea8
	int _exitScene, _exitSub; // DAT_004baeac / DAT_004baeb0
	int _climbing;            // DAT_004baeb4: Ned climbs (state 5 or 6)
	int _lastNedX, _lastNedY; // DAT_004baeb8 / DAT_004baebc
	int _fallSound;           // DAT_004baec0
	int _altMusic;            // DAT_004baec4
	int _screenX, _screenY;   // DAT_004baec8 / DAT_004baecc (written only)
	uint32 _scrollTime;       // DAT_004baed0
	int _camNedX, _camNedY;   // DAT_004baed8 / DAT_004baedc
	uint32 _ambientTime;      // DAT_004baee4
	BitmapRec *_farLayer;     // DAT_004baba4
	bool _fallPolling;        // the sound 0x455800 plays with the callback 0x41e050
};

/**
 * Scene 5 sub-scene 0, the toilet hall (0x478a20..0x479fb0; NED90000 scene 2). A 1752
 * wide room of toilets in front of two parallax layers, entered by falling in from the
 * top. Monsters pop out of five toilets, two at a time, and grab Ned when he ducks
 * there; the chain (slot 0x7c) changes the three bubbling pipes. Ned leaves down the
 * ladder or by falling off the bottom; both lead to sub-scene 1.
 */
class Scene5Toilets : public Scene5Base {
public:
	explicit Scene5Toilets(Game *game);

	int load(int fromScene, int fromSub) override;   // 0x478a20
	int enter(int fromScene, int fromSub) override;  // 0x478a40
	int leave(int toScene, int toSub) override;      // 0x478f60
	int unload(int toScene, int toSub) override;     // 0x479000
	int tick(uint32 input, uint32 now) override;     // 0x479050

private:
	enum {
		kList = 0x4b4488,  // 0x28 entries, NAF scene 2
		kCount = 0x28,
		kLast = 0x4b4a04,
		kPaletteVA = 0x51fdc0,
		kMainBitmap = 0x4eb19c,  // bitmap 1, 1752 wide
		kStillBitmap = 0x4eb1a8, // bitmap 4
		kMidBitmap = 0x4eb1ac,   // bitmap 5, 1/6
		kFarBitmap = 0x4eb1b0,   // bitmap 6, 1/3
		kMap1 = 0x51fcc8,
		kMap2 = 0x51fccc,
		kMap3 = 0x51fcd0,
		kLanes = 5,
		kPipes = 3
	};

	static void xform(void *user, int &x, int &y);
	void cameraWant(uint32 now, int out[2]);
	void pipesTick(uint32 now, int dx, int dy);
	bool monsterGrabbing(int lane);

	static int hookUse(void *user, int x, int y, int16 *state);
	static int hookRegion(void *user, int32 id, uint32 flags, int x, int y, int16 *state);
	static void hookFindTarget(void *user, int x, int y, int16 *a, int16 *b, int32 rect[4]);
	static int hookHitTarget(void *user, int x, int y, int16 *a, int16 *b, const int32 rect[4]);

	int16 _laneSlots[kLanes];  // 0x495478 (every other u16): 40, 50, 42, 48, 44
	int16 _pipeSlot[kPipes];   // DAT_0051fcb8
	int16 _active;             // DAT_0051fcc0: monsters up
	int16 _lane[kLanes];       // DAT_0051fcd8: the monster up in each toilet, or -1
	uint32 _laneTime[kLanes];  // DAT_0051fcf8
	BitmapRec *_far;           // DAT_0051fd0c
	int32 _rects[8][4];        // DAT_0051fd10: regions 100..107 of map 3
	int _chainPulls;           // DAT_0051fd90
	int _midRemX, _midRemY;    // DAT_0051fd98 / DAT_0051fd9c
	int _chainUnpulled;        // DAT_0051fda0
	BitmapRec *_still;         // DAT_0051fda4
	int _farRemX, _farRemY;    // DAT_0051fdb0 / DAT_0051fdb4
	int _scrollX, _scrollY;    // DAT_005201c8 / DAT_005201cc
	BitmapRec *_main;          // DAT_005201d0
	BitmapRec *_mid;           // DAT_005201d4
	int _exit;                 // DAT_005201dc: -1 on the ladder, 1 down it, 2 fell
	int _jumped;               // DAT_005201e0
	int _screenX, _screenY;    // DAT_005201e8 / DAT_005201ec (written only)
	uint32 _scrollTime;        // DAT_005201f0
	int _camNedX, _camNedY;    // DAT_005201f8 / DAT_005201fc
};

/**
 * Scene 5 sub-scene 1, the washroom (0x45d1a0..0x45da20; NED90000 scene 4): one
 * screen. Zapping the hatch (slot 0x6e) opens it; the regions lead back to scene 5
 * and on to scene 2.
 */
class Scene5Washroom : public Scene5Base {
public:
	explicit Scene5Washroom(Game *game);

	void shutdown() override;                        // 0x45d4d0
	int load(int fromScene, int fromSub) override;   // 0x45d1a0
	int enter(int fromScene, int fromSub) override;  // 0x45d1c0
	int leave(int toScene, int toSub) override;      // 0x45d4f0
	int unload(int toScene, int toSub) override;     // 0x45d580
	int tick(uint32 input, uint32 now) override;     // 0x45d6d0

	/** 0x45da20: the toilet hall was left down its ladder, so this room's vision may play */
	void armHint() { _hintArmed = 1; }

private:
	enum {
		kList = 0x4b0148,  // 7 entries, NAF scene 4
		kCount = 7,
		kLast = 0x4b0220,
		kPaletteVA = 0x51c890,
		kBitmap = 0x4eb4bc,
		kMap1 = 0x51cc98,
		kMap2 = 0x51cc9c,
		kHatch = 0x6e
	};

	static void xform(void *user, int &x, int &y);
	void hatchTick(uint32 now);

	static int hookUse(void *user, int x, int y, int16 *state);
	static int hookRegion(void *user, int32 id, uint32 flags, int x, int y, int16 *state);
	static void hookFindTarget(void *user, int x, int y, int16 *a, int16 *b, int32 rect[4]);
	static int hookHitTarget(void *user, int x, int y, int16 *a, int16 *b, const int32 rect[4]);

	int _scrollX, _scrollY;   // DAT_0051c888 / DAT_0051c88c (always 0)
	int32 _rects[2][4];       // DAT_0051cca0: regions 100, 101 of map 1
	int _hatchState;          // DAT_0051ccc8
	BitmapRec *_room;         // DAT_0051cccc
	int _hintArmed;           // DAT_0051ccd4
};

} // End of namespace NightmareNed

#endif // NIGHTMARENED_SCENE5_H
