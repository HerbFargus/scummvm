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

#ifndef NIGHTMARENED_ATTIC_H
#define NIGHTMARENED_ATTIC_H

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
 * Scene 4, the attic hub (registered by 0x403a40). The attic is 22 rooms in one
 * NED98000 world; each room streams its own request lists in stages while Ned is next to
 * it and unloads when he leaves (the room table 0x497930). The doors lead to scenes 2, 3
 * and 5; a TV clip plays on the way in and out.
 */
class AtticScene : public Scene {
public:
	explicit AtticScene(Game *game);
	~AtticScene() override;

	int init() override;                             // m0 0x404090
	void shutdown() override;                        // m1 0x403d60
	int load(int fromScene, int fromSub) override;   // m2 0x403f60
	int enter(int fromScene, int fromSub) override;  // m3 0x404760
	int leave(int toScene, int toSub) override;      // m4 0x404290
	int unload(int toScene, int toSub) override;     // m5 0x4043e0
	void suspend() override;                         // m6 0x403ad0
	int tick(uint32 input, uint32 now) override;     // m7 0x404f10
	/** m8 0x403af0 */
	int getSaveSize() const { return 0x4c; }
	/** m9 0x403b00 */
	void saveState(int32 *out);
	/** m10 0x403c30 */
	void restoreState(const int32 *in, int size);

	int getRoom() const { return _room; }
	const Common::Point &getNedPosition() const { return _nedPos; }

private:
	enum {
		kArea = 3,
		kNafAttic = 10,              // ned98000
		kRoomCount = 26,
		kRoomTable = 0x497930,       // 0x54 bytes per room
		kRoomSize = 0x54,
		kBaseList = 0x497680,        // 9 collision maps + 10 backgrounds
		kBaseListCount = 0x13,
		kBitmapList = 0x4977c4,
		kBitmapListEnd = 0x49792c,
		kBaseMaps = 0x525940,        // destinations of the 9 maps
		kBitmapTable = 0x4ddb88,     // DAT_004ddb88: bitmap records by (area * 15 + scene) * 100 + id
		kPaletteAttic = 0x5259a0,
		kStartMaps = 0x497618,       // base map per InitialAtticScene room (-1 = not a start room)
		kSstAreas = 0x4afcd8,        // .SST area prefix per area index (i16)
		kAreaIndex = 0x4afc60,       // area index per NAF number (i16)
		kR7Variants = 0x498f10,      // i16 per show
		kR12Delays = 0x499648,       // i32 per drum sound
		kR25Steps = 0x49ac70,        // i32 first step per pump stroke
		kR25Ends = 0x49ac80,         // i32 end step per pump stroke
		kNedList1 = 7,
		kNedList2 = 0x15,
		kAtticMusic = 0x7d,          // 98002208.wav
		kWaitLimit = 1000            // updates a load wait may take before it gives up
	};

	enum RoomFlags {
		kRoomIdle = 0x01,      // nothing loaded
		kRoomUnloading = 0x02,
		kRoomLoading = 0x04,
		kRoomActive = 0x08,    // shown; its neighbours may load
		kRoomLoaded = 0x10,    // every stage loaded
		kRoomArrived = 0x20,   // a stage's list completed: poll it
		kRoomReady = 0x40,     // polled: tick it
		kRoomMore = 0x800,     // load the next stage
		kRoomVisited = 0x2000, // this update
		kRoomCancel = 0x4000,  // unload once loaded
		kRoomAbort = 0x8000    // unload now
		// 0x80/0x100/0x200/0x400: the camera may scroll past the right/top/left/bottom view edge
	};

	/** A 0x54-byte room record (0x497930 + room * 0x54) */
	struct Room {
		int32 stages;   // +0x00: lists loaded
		int32 polled;   // +0x04: stages shown
		int32 exits[4]; // +0x08: up, down, left, right (-1 none)
		int32 rect[4];  // +0x18: the area Ned is in when in this room
		int32 view[4];  // +0x28: what the camera shows of it
		uint32 fn[6];   // +0x38: shouldLoad, load, poll, tick, shouldUnload, unload
		uint16 flags;   // +0x50
	};

	typedef Common::HashMap<uint32, Request> EntryMap;

	// Resources
	Slot &slot(int id);
	void visible(int id, bool on);
	BitmapRec *bmp(uint32 va);
	void bmpVisible(uint32 va, bool on);
	void bmpPlace(uint32 va);
	void bmpSet(uint32 va, int x, int y);
	CollisionMap *map(uint32 va);
	int submit(uint32 va, int count, int scene);
	void release(uint32 va, int count);
	int resWait(uint32 va);
	void runCallbacks();
	void freeAll();
	void listCallback(uint32 fn);

	// Display (area 3)
	int16 slotSwitch(int newSlot, int oldSlot);
	int16 slotSwitchPre(int newSlot, int oldSlot);
	int place(int id, int ref, bool noMirror, int *outFrame = nullptr);
	void setDeadline(int id, uint32 deadline);
	void setStep(int id, int16 step);
	void nudge(int id, int dx, int dy);
	void showSlot(int id, int z, bool on);
	void showBitmap(int area, int scene, int id, int z, bool on);
	void attachPos(int id, int ref, int out[2], bool noMirror);
	void stepRect(int id, int k, int32 out[4], int *outFrame = nullptr);
	void attachedBox(int16 s, int16 ref, int k, int32 rect[4]);
	bool nedTouches(const int32 rect[4]);

	// Sound
	void playAt(int id, int stop = -1);
	void playAtNotify(int id, int stop, uint32 callback);
	void playSound(int id, int stop = -1);
	void stopSound(int id);
	void checkSoundEnd();

	// Collision
	void addMap(CollisionMap *m);
	void removeMap(CollisionMap *m);
	void addBaseMap(int i);
	void removeBaseMap(int i);
	bool solidAt(uint32 mapVA, int x, int y);
	int32 addRegion(const int32 rect[4], uint32 type, int32 id);
	void removeRegion(int32 &id);
	void setRegion(int32 id, const int32 rect[4], uint32 type);
	void r9MoveRegion(int32 id, int16 s, int16 ref);
	void r12MoveRegion(int32 id, int16 s, int16 ref, int &duration);

	// Targets
	void targetAdd(int id, bool noFlip);
	void targetRemove(int id);

	// Ned
	Ned *ned();
	int facing();
	void setFacing(int f);
	void playAction(int action);
	void autoWalk(int input, int facingRight, int x, int lockCamera);
	static void dimPalette(byte *pal, double f);
	static void xform(void *user, int &x, int &y);
	static void actionFinished(void *user, int16 action);
	void hint(int which, bool facingRight);

	// Scene
	void clearState();
	int applyPlacement(const Placement &e, int ox, int oy);
	void applyPlacements();
	Common::Array<CollisionMap *> overlayMaps() const;

	// Hooks (DAT_00525980)
	int nearestTarget(int16 &dir, int16 &found, int32 rect[4]);
	static int hookUse(void *user, int x, int y, int16 *state);
	static int hookRegion(void *user, int32 id, uint32 flags, int x, int y, int16 *state);
	static void hookFindTarget(void *user, int x, int y, int16 *a, int16 *b, int32 rect[4]);
	static int hookHitTarget(void *user, int x, int y, int16 *a, int16 *b, const int32 rect[4]);
	int region(int32 id, uint32 flags, int x, int y, int16 *state);
	int region2(int32 id, int16 *state);
	int regionTail(int32 id, int16 *state);
	int regionLast(int32 id, int16 *state);
	int hitTarget(int16 found);
	void r21Hit(int newSlot, bool ready);

	// Camera
	void camera(uint32 now);
	void cameraPull(int room, int16 state, int sx, int sy, int dx, int dy);
	void cameraRooms(int dx, int dy);

	// Room tracker
	void roomsEnter(int room);
	void roomsLeave();
	void roomsUpdate(uint32 now);
	void roomEdge(int room);
	void roomUpdate(int r, uint32 now);
	bool isNeighbour(int r);
	bool isFar(int r);
	bool roomShouldLoad(int r);
	bool roomShouldUnload(int r);
	int roomLoad(int r);
	int roomPoll(int r);
	void roomTick(int r, uint32 now);
	int roomUnload(int r);

	// The rooms (attic_rooms.cpp)
	void r2Loaded();
	int r2Load();
	int r2Poll();
	void r2Tick(uint32 now);
	int r2Unload();
	void r2TickDrops(uint32 now);
	void r2Drop(uint32 now, int k);
	void r2Splash(uint32 now, int k);
	void r2Tick14(uint32 now);
	void r2Tick4(uint32 now);
	void r2TickC9(uint32 now);
	int16 r2FindFloor(int x, int y, int16 old);
	void r3Loaded();
	int r3Load();
	int r3Poll();
	void r3Tick(uint32 now);
	int r3Unload();
	void r4Loaded();
	int r4Load();
	int r4Poll();
	void r4Tick(uint32 now);
	int r4Unload();
	void r4TickB3(uint32 now);
	void r4Tick21(uint32 now);
	void r4Tick24(uint32 now);
	void r4TickBall(uint32 now);
	bool r5ShouldLoad();
	bool r5ShouldUnload(int r);
	void r5Loaded();
	int r5Load();
	int r5Poll();
	void r5Tick(uint32 now);
	int r5Unload();
	void r5TickFighter(uint32 now, int which);
	void r5Tick2F(uint32 now);
	void r5Tick3B(uint32 now);
	int r5AddMap3();
	void r6Loaded();
	int r6Load();
	int r6Poll();
	int r6Unload();
	void r7Loaded();
	int r7Load();
	int r7Poll();
	void r7Tick(uint32 now);
	int r7Unload();
	void r7Tick46(uint32 now);
	void r7SoundEnd();
	void r8Loaded();
	int r8Load();
	int r8Poll();
	void r8Tick(uint32 now);
	int r8Unload();
	void r8TickBF(uint32 now);
	void r8TickChannel(uint32 now);
	void r9Loaded();
	int r9Load();
	int r9Poll();
	void r9Tick(uint32 now);
	int r9Unload();
	void r9Tick2B(uint32 now);
	void r9Tick82(uint32 now);
	void r10Loaded();
	int r10Load();
	int r10Poll();
	void r10Tick(uint32 now);
	int r10Unload();
	void r10Tick1C(uint32 now);
	void r10TickA(uint32 now);
	void r10Tick1D(uint32 now);
	void r10TickB(uint32 now);
	void r11Loaded();
	int r11Load();
	int r11Poll();
	void r11Tick(uint32 now);
	int r11Unload();
	void r12Loaded();
	int r12Load();
	int r12Poll();
	void r12Tick(uint32 now);
	int r12Unload();
	void r12Tick32(uint32 now);
	void r12Tick36(uint32 now);
	void r12Tick39(uint32 now);
	void r12Tick95(uint32 now);
	void r13Loaded();
	int r13Load();
	int r13Poll();
	void r13Tick(uint32 now);
	int r13Unload();
	void r13Tick47(uint32 now);
	void r13TickBall(uint32 now);
	void r15Loaded();
	int r15Load();
	int r15Poll();
	int r15Unload();
	void r16Loaded();
	int r16Load();
	int r16Poll();
	int r16Unload();
	void r17Loaded();
	int r17Load();
	int r17Poll();
	void r17Tick(uint32 now);
	int r17Unload();
	void r17Tick59(uint32 now);
	void r17Tick61(uint32 now);
	void r17Tick5C(uint32 now);
	void r17TickF(uint32 now);
	void r18Loaded();
	int r18Load();
	int r18Poll();
	void r18Tick(uint32 now);
	int r18Unload();
	void r18AddMap();
	void r18Tick5E(uint32 now);
	void r18Tick62(uint32 now);
	void r19Loaded();
	int r19Load();
	int r19Poll();
	void r19Tick(uint32 now);
	int r19Unload();
	void r19Tick67(uint32 now);
	void r19Tick65(uint32 now);
	void r19Tick6D(uint32 now);
	void r19TickAny(uint32 now);
	void r19TickEF(uint32 now);
	void r21Loaded();
	int r21Load();
	int r21Poll();
	void r21Tick(uint32 now);
	int r21Unload();
	void r21TickB0(uint32 now);
	void r21Tick208(uint32 now);
	void r21Tick218(uint32 now);
	void r22Loaded();
	int r22Load();
	int r22Poll();
	void r22Tick(uint32 now);
	int r22Unload();
	bool r23ShouldLoad();
	void r23Loaded();
	int r23Load();
	int r23Poll();
	void r23Tick(uint32 now);
	int r23Unload();
	void r24Loaded();
	int r24Load();
	int r24Poll();
	int r24Unload();
	void r25Loaded();
	int r25Load();
	int r25Poll();
	void r25Tick(uint32 now);
	int r25Unload();
	void r25Tick184(uint32 now);
	void r25TickPump(uint32 now);

	// ---- Resources and the world ----
	EntryMap _entries;                              // loaded request entries by VA
	Common::Array<uint32> _pendingCallbacks;        // list completion callbacks not run yet
	Common::HashMap<uint32, BitmapRec *> _bitmaps;  // bitmap records by destination VA
	Common::HashMap<uint32, CollisionMap *> _maps;  // collision maps by destination VA
	byte _palette[256 * 3];        // DAT_005259a0 (with Ned's colours merged in)
	byte _paletteDim[256 * 3];     // DAT_005252e0
	CollisionWorld _world;         // DAT_00525da4
	CollisionMap _userMap;         // DAT_00525da8: the scene's own regions
	bool _mapAdded[9] = {};             // DAT_00525dc0: base map i is in the world
	Common::Array<Placement> _targets; // DAT_00525924: what Ned's zap can hit
	Common::Array<Placement> _sst;     // DAT_00525de4: 98999001.SST
	NedHooks _hooks;               // DAT_00525980
	Room _rooms[kRoomCount];       // DAT_00497930
	bool _soundXform = false;              // DAT_004c6140 is the attic xform (0x455a40)
	int _soundEndSlot = -1;             // the slot whose sound end calls _soundEndCallback (slot +0x60)
	uint32 _soundEndCallback = 0;
	int32 _saveBuffer[19] = {};         // DAT_004ba5ac
	bool _hasSaveBuffer = false;

	// ---- Scene state ----
	int _initialRoom = 0;              // DAT_00525920: ned.ini InitialAtticScene
	int _startMap = 0;                 // DAT_00525970: the base map the start floor is in
	int _inputLocked = 0;              // DAT_00525900
	int _autoWalk = 0;                 // DAT_00525928 (0x4038b0)
	int _autoFacing = 0;               // DAT_00525de8
	int _autoTargetX = 0;              // DAT_00525964
	int _autoInput = 0;                // DAT_0052596c
	int _skipDraw = 0;                 // DAT_00525974
	int _cameraLocked = 0;             // DAT_00525904
	int _cameraJump = 0;               // DAT_0052590c
	int _jumpX = 0, _jumpY = 0;            // DAT_00525918 / DAT_0052591c
	int _viewRoomX = 0, _viewRoomY = 0;    // DAT_00525998 / DAT_00525994: rooms whose view the camera seeks
	int _pendingScene = 0;             // DAT_0052592c
	int _paletteDimmed = 0;            // DAT_00525968
	int _scrollX = 0, _scrollY = 0;        // DAT_00525910 / DAT_00525914: world -> screen
	Common::Point _nedPos;         // DAT_00525df8 / DAT_00525dfc
	uint32 _lastTick = 0;              // DAT_004ba5a0
	uint32 _lastCamMove = 0;           // DAT_004ba5b8
	int _lastNedX = 0, _lastNedY = 0;      // DAT_004ba5b0 / DAT_004ba5b4
	int _room = 0;                     // DAT_0051ed50: Ned's room
	uint16 _edgeLocks = 0;             // DAT_00525908
	int _endPan = 0;                   // DAT_005251d8: region 0x400, the pan to room 20 that ends the attic
	int _toRoom25 = 0;                 // DAT_005257bc: region 0x3f9, Ned went down the hole of room 9
	int _toRoom9 = 0;                  // DAT_005251c0: region 0x3fa, and the one of room 25
	int _hint0Shown = 0;               // DAT_005257f4 (saved)
	int _hint3Shown = 0;               // DAT_005257cc (saved)
	int _hint4Shown = 0;               // DAT_00525294 (saved)
	int _hint5Shown = 0;               // DAT_005257ac (saved)
	bool _hintTestDone = false;        // nightmarened_hint test hook

	// ---- Room 2 ----
	int16 _r2s14 = 0, _r2s4 = 0, _r2sC9 = 0, _r2s9 = 0, _r2sCD = 0; // DAT_005258e6, DAT_00525992, DAT_00525990, DAT_00525896, DAT_005258bc
	int _r2Hits4 = 0, _r2HitsC9 = 0;       // DAT_005258b8 / DAT_00525898 (saved)
	int _r2Open = 0;                   // DAT_0052589c (saved): the trapdoor to room 12 is open
	int _r2MapB0Added = 0, _r2MapB4Added = 0; // DAT_005258c0 / DAT_005258c4 (maps DAT_005258b0 / DAT_005258b4)
	int _r2Pos4X = 0, _r2Pos4Y = 0;        // DAT_005258e8 / DAT_005258ec
	int _r2PosC9X = 0, _r2PosC9Y = 0;      // DAT_005258c8 / DAT_005258cc
	uint32 _r2Time4 = 0, _r2TimeC9 = 0;    // DAT_005258a0 / DAT_005258a4
	int _r2DropX = 0, _r2DropY = 0;        // DAT_005258f8 / DAT_005258fc
	int _r2DropXb = 0, _r2DropYb = 0;      // DAT_005258d8 / DAT_005258dc
	int16 _r2Floor = 0, _r2FloorB = 0;     // DAT_005258e4 / DAT_005258d0
	int _r2NedHit = 0;                 // DAT_005258a8
	int _r2Fall = 0;                   // DAT_005258f4
	int _r2DropWait = 0;               // DAT_005258f0
	uint32 _r2DropTime = 0;            // DAT_005258e0

	// ---- Room 3 ----
	int16 _r3s2D = 0, _r3sEB = 0;          // DAT_0052588e / DAT_00525894
	int _r3Started = 0;                // DAT_00525890

	// ---- Room 4 ----
	int16 _r4sB3 = 0, _r4s21 = 0, _r4s24 = 0, _r4sBall = 0; // DAT_005213e4, DAT_00525868, DAT_0052586a, DAT_0052588c
	int32 _r4Index = 0;                // DAT_00525860 (saved): which ball (0x498970)
	uint32 _r4TimeB3 = 0;              // DAT_00525864
	int _r4Climbed = 0;                // DAT_0052586c
	int _r4Pos79X = 0, _r4Pos79Y = 0, _r4Pos7BX = 0, _r4Pos7BY = 0, _r4Pos26X = 0, _r4Pos26Y = 0; // DAT_00525870..DAT_00525888

	// ---- Room 5 ----
	int16 _r5s31 = 0, _r5s86 = 0, _r5s8A = 0, _r5s2F = 0, _r5s3B = 0, _r5sC2 = 0; // DAT_00498ed0..DAT_00498ee4
	int _r5Fight = 0;                  // DAT_00525834
	int _r5Done = 0;                   // DAT_0052583c (saved): the gate is open (base map 3)
	int _r5Hit = 0;                    // DAT_0052582c (saved): 0xc2 was hit
	int _r5Var828 = 0;                 // DAT_00525828
	int _r5Down31 = 0, _r5Down86 = 0, _r5Down8A = 0; // DAT_00525830 / DAT_00525844 / DAT_00525820
	int _r5Hits31 = 0, _r5Hits86 = 0, _r5Hits8A = 0; // DAT_0052584c / DAT_00525848 / DAT_00525850
	uint32 _r5Time31 = 0, _r5Time86 = 0, _r5Time8A = 0, _r5Time3B = 0; // DAT_0052585c / DAT_00525858 / DAT_00525840 / DAT_00525838
	int32 _r5Region31 = 0, _r5Region86 = 0, _r5Region8A = 0, _r5Region854 = 0; // DAT_0052581c / DAT_00525824 / DAT_005257f8 / DAT_00525854
	int _r5Pos31X = 0, _r5Pos31Y = 0, _r5Pos86X = 0, _r5Pos86Y = 0, _r5Pos8AX = 0, _r5Pos8AY = 0; // DAT_00525800..DAT_00525818

	// ---- Rooms 6, 7, 8 ----
	int _r6Look = 0;                   // DAT_005251c8: region 0x403 pans up to room 6
	int16 _r7s45 = 0, _r7s46 = 0;          // DAT_005257d0 / DAT_005257e4
	int _r7MapAdded = 0;               // DAT_005257e0 (map DAT_005257d4)
	int _r7Variant = 0;                // DAT_005257dc
	int _r7Plays = 0;                  // DAT_005257ec (saved)
	int _r7Shown = 0;                  // DAT_005257d8 (saved)
	uint32 _r7Start = 0, _r7Elapsed = 0;   // DAT_005257e8 / DAT_005257f0
	int16 _r8sBF = 0, _r8sCh = 0;          // DAT_004a3df8 / DAT_005257c8
	int _r8State = 0;                  // DAT_005257c4
	int _r8Channel = 0;                // DAT_005257c0 (saved)

	// ---- Rooms 9, 10, 11 ----
	int16 _r9s2B = 0, _r9s82 = 0;          // DAT_005257b2 / DAT_005257b0
	uint32 _r9Time = 0;                // DAT_005257b4
	int32 _r9Region = 0;               // DAT_005257b8
	int16 _r10s1C = 0, _r10sA = 0, _r10s1D = 0, _r10sB = 0; // DAT_005257a0, DAT_0052578c, DAT_005257a8, DAT_005257a2
	uint32 _r10Time1C = 0, _r10Time16 = 0; // DAT_00525784 / DAT_00525794
	int _r10Done = 0;                  // DAT_00525798 (saved): the guard was knocked out (base map 7, exit to room 13)
	int _r10Open = 0;                  // DAT_00525790
	int _r10Back = 0;                  // DAT_0052579c
	int _r10Return = 0;                // DAT_005257a4
	int _r10Enter = 0;                 // DAT_00525788: regions 0x3f7 / 0x3fc
	uint32 _r11Time = 0;               // DAT_00525780

	// ---- Room 12 ----
	int16 _r12s32 = 0, _r12s36 = 0, _r12s39 = 0, _r12s95 = 0; // DAT_0052571a, DAT_00525718, DAT_00525708, DAT_00525764
	uint32 _r12Time32 = 0, _r12Time36 = 0, _r12Time39 = 0, _r12Time95 = 0; // DAT_0052577c, DAT_005256fc, DAT_0052575c, DAT_0052573c
	int _r12Arm36 = 0, _r12Arm39 = 0, _r12Arm95 = 0; // DAT_00525730 / DAT_00525714 / DAT_00525768
	int _r12Open36 = 0, _r12Open39 = 0, _r12Open95 = 0; // DAT_00525770 / DAT_0052571c / DAT_00525704
	int _r12MapAAdded = 0, _r12MapBAdded = 0, _r12MapCAdded = 0; // DAT_00525750 / 54 / 58 (maps DAT_00525740 / 44 / 48)
	int32 _r12RegionA = 0, _r12RegionB = 0, _r12RegionC = 0; // DAT_00525710 / DAT_00525738 / DAT_00525778
	int _r12Touched = 0;               // DAT_00525720
	int _r12Armed = 0;                 // DAT_00525728
	uint32 _r12Time700 = 0;            // DAT_00525700
	uint32 _r12Time = 0;               // DAT_0052570c
	int _r12Count = 0;                 // DAT_00525774
	int _r12Exit = 0;                  // DAT_00525724: region 0x3fd
	int _r12Escaped = 0;               // DAT_0052572c (saved)
	int _r12Hint = 0;                  // DAT_00525760
	uint32 _r12HintTime = 0;           // DAT_0052576c
	int _r12HintFlag = 0;              // DAT_00525734

	// ---- Room 13 ----
	int16 _r13s47 = 0, _r13sBall = 0;      // DAT_005256e8 / DAT_005256f8
	uint32 _r13Time47 = 0, _r13TimeBall = 0; // DAT_005256ec / DAT_005256f4
	int _r13BallHit = 0;               // DAT_005256f0

	// ---- Rooms 17, 18 ----
	int16 _r17s59 = 0, _r17s61 = 0, _r17s5C = 0; // DAT_005252c0, DAT_005252ac, DAT_005252c2
	int16 _r17sF[4] = {};               // DAT_005252b0..DAT_005252b6
	uint32 _r17Time5A = 0, _r17Time61 = 0, _r17TimeF = 0, _r17TimeSound = 0; // DAT_005252bc, DAT_005256e4, DAT_005252b8, DAT_005252a8
	int16 _r18s5E = 0, _r18s5D = 0, _r18s62 = 0; // DAT_0052528e, DAT_005252a0, DAT_005252a2
	int _r18Pulled = 0;                // DAT_00525298
	int _r18Done = 0;                  // DAT_005252a4 (saved): the ladder is down
	int _r18MapAdded = 0;              // DAT_0052529c (map DAT_00525290)

	// ---- Room 19 ----
	int16 _r19s67 = 0, _r19s65 = 0, _r19s6D = 0, _r19sAny = 0, _r19sEF = 0; // DAT_0052528c, DAT_0052527c, DAT_0049a220, DAT_00525270, DAT_00525258
	uint32 _r19Time67 = 0, _r19Time65 = 0, _r19Time6D = 0; // DAT_00525280, DAT_00525260, DAT_0052526c
	int _r19Toggle = 0;                // DAT_00525284
	int _r19HitEF = 0;                 // DAT_00525254
	int _r19NedHit = 0;                // DAT_00525288
	int _r19Done = 0;                  // DAT_0052525c (saved): the sleeper woke
	int _r19Climbed = 0;               // DAT_00525264 (saved)
	int _r19MapAdded = 0;              // DAT_00525274 (map DAT_00525278)
	int32 _r19Region = 0;              // DAT_00525268

	// ---- Rooms 21..25 ----
	int16 _r21sB0 = 0, _r21s208 = 0, _r21s218 = 0; // DAT_00525244, DAT_00525208, DAT_00525218
	int _r21Ready = 0;                 // DAT_00525214
	int _r21Done = 0;                  // DAT_00525210 (saved)
	int _r21Count = 0;                 // DAT_005251f8
	int _r21Throws = 0;                // DAT_0052520c
	uint32 _r21TimeAE = 0;             // DAT_0052521c
	int _r21Var220 = 0;                // DAT_00525220
	int _r21Pos[2] = {}, _r21PosY[2] = {};   // DAT_00525228 / DAT_00525230: 0xec's and 0x194's start
	int _r21X = 0, _r21Y = 0, _r21X2 = 0, _r21Y2 = 0; // DAT_00525240 / DAT_0052523c / DAT_00525248 / DAT_0052524c
	int _r21Step = 0, _r21Steps = 0, _r21Arc = 0; // DAT_00525204 / DAT_00525200 / DAT_005251fc
	int _r21Drop = 0;                  // DAT_00525250
	int16 _r22s183 = 0;                // DAT_005251f0
	int _r22Spin = 0;                  // DAT_005251f4
	int _r22Teleport = 0;              // DAT_005251e4
	int _r22MapAAdded = 0, _r22MapBAdded = 0; // DAT_005251dc / DAT_005251ec (maps DAT_005251e0 / DAT_005251e8)
	int _r23Drop = 0;                  // DAT_00525238: region 0x3ff
	uint32 _r23Time = 0;               // DAT_005251d4
	int16 _r25s = 0, _r25s184 = 0;         // DAT_005251bc / DAT_005251be
	uint32 _r25Time = 0, _r25Time184 = 0;  // DAT_005251cc / DAT_005251b8
	int _r25Count = 0;                 // DAT_005251c4
	int32 _r25Region = 0;              // DAT_005251d0
};

} // End of namespace NightmareNed

#endif // NIGHTMARENED_ATTIC_H
