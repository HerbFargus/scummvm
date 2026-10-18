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

#ifndef NIGHTMARENED_SCENE3_H
#define NIGHTMARENED_SCENE3_H

#include "common/array.h"
#include "common/hashmap.h"
#include "common/rect.h"

#include "nightmarened/data.h"
#include "nightmarened/game.h"
#include "nightmarened/ned.h"
#include "nightmarened/puff.h"
#include "nightmarened/resources.h"
#include "nightmarened/world.h"

namespace NightmareNed {

/**
 * Scene 3, the "s" world (registered by 0x471b80): a school building of NED97000 (area 4),
 * 5280 x 960 and wrapping horizontally, built from nine rooms on two floors (the room
 * table 0x4b1fd8) that stream their lists while Ned is next to them. Creatures lurk in
 * the classrooms (zapped, they vanish in a puff or flee), doors open and close with the
 * button, and hidden passages behind the walls carry Ned between the floors. Doorways
 * lead to the chalkboard level (sub 0), the room of sub 1, and scenes 1, 2, 5 and 6.
 */
class Scene3 : public Scene {
public:
	explicit Scene3(Game *game);
	~Scene3() override;

	int init() override;                             // m0 0x468500
	void shutdown() override;                        // m1 0x4684d0
	int load(int fromScene, int fromSub) override;   // m2 0x46b760
	int enter(int fromScene, int fromSub) override;  // m3 0x46ba40
	int leave(int toScene, int toSub) override;      // m4 0x471ca0
	int unload(int toScene, int toSub) override;     // m5 0x46be50
	void suspend() override {}                       // m6 0x4684f0
	int tick(uint32 input, uint32 now) override;     // m7 0x46ea50
	/** m8 0x468520 */
	int getSaveSize() const override { return 0x20; }
	/** m9 0x468530 */
	void saveState(int32 *out) override;
	/** m10 0x468550 */
	void restoreState(const int32 *in, int size) override;

	const Common::Point &getNedPosition() const { return _nedPos; }
	int getRoom() const { return _room; }

private:
	enum {
		kArea = 4,
		kNaf = 9,                  // ned97000
		kNedList = 3,
		kMapList = 0x4b22f0,       // 15 collision maps
		kMapCount = 0xf,
		kPaletteList = 0x4b2510,   // the two palette bitmaps and slot 0x19a
		kPaletteCount = 3,
		kPaletteMain = 0x51ed98,
		kPaletteAlt = 0x51f1e0,
		kRoomTable = 0x4b1fd8,     // 0x58 bytes per room
		kRoomSize = 0x58,
		kRoomCount = 9,
		kSstAreas = 0x4afcd8,      // .SST area prefix per area index (i16)
		kSstAreaIndex = 2,
		kMusic = 0x79,             // 97002208.wav
		kWorldW = 0x14a0,
		kWrap = 0x149f,
		kWaitLimit = 1000          // room updates a load wait may take before it gives up
	};

	enum RoomFlags {
		kRoomIdle = 0x01,
		kRoomUnloading = 0x02,
		kRoomLoading = 0x04,
		kRoomActive = 0x08,    // shown; its neighbours may load
		kRoomLoaded = 0x10,    // both stages loaded
		kRoomArrived = 0x20,   // a stage's list completed: poll it
		kRoomReady = 0x40,     // polled: tick it
		kRoomVisited = 0x2000, // this update
		kRoomCancel = 0x4000,  // unload once loaded
		kRoomAbort = 0x8000
		// 0x80/0x100/0x200/0x400: the camera may not scroll past the room's left/top/right/bottom
	};

	/** A 0x58-byte room record (0x4b1fd8 + room * 0x58) */
	struct Room {
		int32 stages;   // +0x00: lists loaded
		int32 polled;   // +0x04: stages shown
		int32 exits[4]; // +0x08: up, down, left, right (-1 none)
		int32 rect[4];  // +0x18: the area Ned is in when in this room
		int32 view[4];  // +0x28: what the camera shows of it
		uint16 flags;   // +0x54
	};

	typedef Common::HashMap<uint32, Request> EntryMap;

	// Resources
	Slot &slot(int id);
	Slot &nedSlot(int id);
	void visible(int id, bool on);
	bool isVisible(int id);
	BitmapRec *bmp(uint32 va);
	CollisionMap *map(uint32 va);
	CollisionMap *mapAt(int i);
	int submit(uint32 va, int count, int scene = 0);
	int release(uint32 va, int count);
	bool isLoaded(uint32 va) const { return _entries.contains(va); }
	void runCallbacks();
	void freeAll();
	void showSprite(uint32 entry, int z);
	void showBitmap(uint32 entry);
	void bmpPlace(uint32 va, int x, int y);

	// Display (area 4)
	int16 switchSlot(int newSlot, int oldSlot);
	int16 switchSlotPre(int newSlot, int oldSlot);
	int place(int id, int ref, bool noMirror, int *outFrame = nullptr, NedXform xf = xform);
	void advance(int id, int ref, bool noMirror, uint32 now);
	void nudge(int id, int dx, int dy);
	void attachPos(int id, int ref, int out[2], bool noMirror);
	void boxRect(int id, int frame, int k, int32 out[4]);
	int currentFrame(int id);
	void applyFlip(int id, bool active);
	bool flipActive(int id);
	void setDrawOrder(int id, int z);
	Common::Point spritePos(int id);
	bool nedBoxHits(const int32 rect[4]);
	static bool intersect(const int32 a[4], const int32 b[4]);
	int wrapX(int x) const;

	// Sound
	void playSound(int id, int stop = -1);
	void stopSound(int id);
	void stopAllSounds();
	void playNear(int id, int stop, int ref);
	void footsteps(uint32 now);

	// Collision
	void addMap(CollisionMap *m);
	void removeMap(CollisionMap *m);
	static bool regionEnable(CollisionMap *m, int32 id, bool on);
	static bool regionRect(CollisionMap *m, int32 id, int32 rect[4]);
	int32 addRegion(uint32 type, int32 id);
	void removeRegion(int32 &id);
	void moveRegion(int32 id, const int32 rect[4]);
	void moveRegionTo(int32 id, int s, int ref, int frame, int k);
	void door(int &open, CollisionMap *regions, int32 region, int mapOpen, int mapShut);
	Common::Array<CollisionMap *> overlayMaps() const;

	// Targets (DAT_0051ed8c)
	void targetAdd(int id, bool noFlip);
	void targetRemove(int id);

	// Ned
	Ned *ned();
	void nedTick(uint32 input, uint32 now);
	void nedHide();
	void sideStep(int dx);
	static void xform(void *user, int &x, int &y);
	static void xformRight(void *user, int &x, int &y);
	static void actionFinished(void *user, int16 action);
	static void grabFinished(void *user, int16 action);
	void hint(int which, bool facingRight);
	int switchTo(int scene, int sub);

	// The rooms (0x4720b0-0x472770)
	void roomsEnter(int room);
	void roomsLeave();
	void roomsUpdate(uint32 now);
	void roomUpdate(int r, uint32 now);
	void roomEdge(int r);
	bool roomNeighbour(int r);
	bool roomFar(int r);
	void roomLoaded(int r);
	int roomLoad(int r);
	int roomLoad2(int r);
	int roomPoll(int r);
	void roomTick(int r, uint32 now);
	int roomUnload(int r);
	void listCallback(uint32 fn);

	// The level (scene3.cpp)
	void clearState();
	int addMaps();
	int camera(uint32 now);
	void cameraRooms(int dx, int dy);
	void passages(uint32 now);

	// The rooms' functions and actors (scene3_rooms.cpp)
	int r0Load();
	int r0Poll();
	void r0Tick(uint32 now);
	int r0Unload();
	int r1Load();
	int r1Poll();
	void r1Tick(uint32 now);
	int r1Unload();
	int r2Load();
	int r2Poll();
	int r2Unload();
	int r3Load();
	int r3Poll();
	int r3Unload();
	int r4Load();
	int r4Poll();
	int r4Unload();
	int r5Load();
	int r5Poll();
	void r5Tick(uint32 now);
	int r5Unload();
	int r6Load();
	int r6Poll();
	void r6Tick(uint32 now);
	int r6Unload();
	int r7Load();
	int r7Poll();
	void r7Tick(uint32 now);
	int r7Unload();
	int r8Load();
	int r8Poll();
	void r8Tick(uint32 now);
	int r8Unload();
	void doorTick(uint32 now, int16 anim, int open, int16 frame, int16 shut);
	void prowler(uint32 now, int16 &cur, int16 idle, int16 loop, int16 flee, int &hit, int &gone, int room);
	void r0Grabber(uint32 now);
	void r0Entrance(uint32 now);
	void r2Creature(uint32 now);
	void r3CreatureA(uint32 now);
	void r3CreatureB(uint32 now);
	void r4Creature(uint32 now);
	void r5Creature(uint32 now);
	void r6Creature(uint32 now);
	void r8Creature(uint32 now);
	void r8Slot(uint32 now);
	void r4Order();
	bool nedIn1e61() const;

	// Hooks (DAT_0051f698)
	int findTarget(int16 &dir, int16 &found, int32 rect[4]);
	static int hookUse(void *user, int x, int y, int16 *state);
	static int hookRegion(void *user, int32 id, uint32 flags, int x, int y, int16 *state);
	static void hookFindTarget(void *user, int x, int y, int16 *a, int16 *b, int32 rect[4]);
	static int hookHitTarget(void *user, int x, int y, int16 *a, int16 *b, const int32 rect[4]);
	int region(int32 id, int16 *state);
	int hitTarget(int16 found);

	// ---- Resources and the world ----
	EntryMap _entries;                              // loaded request entries by VA
	Common::Array<uint32> _pendingCallbacks;        // list completion callbacks not run yet
	Common::HashMap<uint32, BitmapRec *> _bitmaps;  // bitmap records by destination VA
	Common::HashMap<uint32, CollisionMap *> _maps;  // collision maps by destination VA
	byte _palette[256 * 3];       // DAT_0051ed98
	byte _paletteAlt[256 * 3];    // DAT_0051f1e0: shown for one frame of Ned's slot 0x8c
	CollisionWorld _world;        // DAT_0051f604
	CollisionMap _userMap;        // DAT_0051f654
	Common::Array<Placement> _targets; // DAT_0051ed8c: what Ned's zap can hit
	Common::Array<Placement> _sst;     // DAT_0051f65c: 97999001.SST
	NedHooks _hooks;              // DAT_0051f698
	ZapPuff _puff;                // DAT_004b4478
	Room _rooms[kRoomCount];      // 0x4b1fd8
	Common::Point _nedPos;        // DAT_00525df8 / DAT_00525dfc
	int _scrollX, _scrollY;       // DAT_0051ed58 / DAT_0051ed5c: world -> screen (x wraps)
	int _room;                    // DAT_0051f74c: Ned's room (-1 none)
	uint16 _edgeLocks;            // DAT_0051ed74
	int _unportedExit;            // the last exit to a scene that isn't registered (logged once)

	// ---- Saved (DAT_0051f1b0, 8 dwords) ----
	int32 _saved[8];              // [0] room 6's creature zapped, [1] room 4's vision seen, [2] room 8's ride taken

	// ---- .bss ----
	uint32 _lastTick;        // DAT_0051ed90
	int _startRoom;          // DAT_0051f6cc
	int _frontZ;             // DAT_0051f6c4: draw order of slot 0x19d (9, or 7 behind the walls)
	int _walkIn;             // DAT_0051f19c: walk in from the left until x 0x281
	int _walkInDoor;         // DAT_0051ed64: the same, once the entrance door has opened
	int _hintBack;           // DAT_0051ed60: vision 0 near x 0xe7e..0xf78 (back from sub 1)
	int _hintAction;         // DAT_0051f680: vision 3 near x 0x1076..0x1172 (after action 0x2c)
	int _behind;             // DAT_0051f658: Ned is behind the walls (costume 7)
	int _cameraBlocked;      // DAT_0051f740: a neighbour room isn't shown yet
	uint32 _cameraTime;      // DAT_0051f744
	uint32 _stepTime;        // DAT_0051f738: the next footstep in the passages
	int _exit;               // DAT_0051f6a8: 3..8, the pending scene switch
	int32 _region1e61[4];    // DAT_0051f5f0..DAT_0051f5fc: region 0x1e61 grown by 1
	int _passUp;             // DAT_0051f6f4: the passage up to room 6 (left)
	int _passDown;           // DAT_0051f6f8: the passage down to room 7 (right)
	int _passLeft;           // DAT_0051f6d0: the passage behind the wall (left, from 0x92f)
	int _passRight;          // DAT_0051f6d4: the passage behind the wall (right, from 0x93f)
	int _r6Enter;            // DAT_0051f6e8
	int _r7Enter;            // DAT_0051f6ec
	int _upstairs;           // DAT_0051f6f0: Ned is on the stairs between rooms 6 and 7
	int _doors[10];          // DAT_0051f710..DAT_0051f734: open
	CollisionMap *_mapSlots[16]; // DAT_0051f610 (map i = cbix i; 0 and 10 unused)
	// Room 0
	int16 _r0Grabber;        // DAT_0051f688: slot 0x21
	int16 _r0Entrance;       // DAT_0051f1a4: slot 0x179
	// Room 1
	int16 _r1Creature;       // DAT_0051f6b8
	int _r1Hit;              // DAT_0051ed94
	int _r1Gone;             // DAT_0051f678
	// Room 2
	int16 _r2Creature;       // DAT_0051f66c
	uint32 _r2Time;          // DAT_0051f664
	int32 _r2Region;         // DAT_0051f68c
	// Room 3
	int16 _r3a;              // DAT_0051ed70
	int16 _r3b;              // DAT_0051f694
	uint32 _r3aTime;         // DAT_0051f67c
	uint32 _r3bTime;         // DAT_0051ed78
	int32 _r3aRegion;        // DAT_0051f1d8
	int32 _r3bRegion;        // DAT_0051f5e8
	int _r3Done;             // DAT_0051f660
	int _r3aTouch;           // DAT_0051f6fc
	int _r3bTouch;           // DAT_0051f700
	// Room 4
	int16 _r4a;              // DAT_0051ed80
	int16 _r4b;              // DAT_0051f674
	int _r4aHit;             // DAT_0051f6b0
	uint32 _r4Time;          // DAT_0051f670
	int _r4Gone;             // DAT_0051f6b4
	// Room 5
	int16 _r5Creature;       // DAT_0051ed88
	int16 _r5Slot;           // DAT_0051f608: slot 400
	int _r5Touch;            // DAT_0051f704
	int32 _r5Region;         // DAT_0051f6ac
	// Room 6
	int16 _r6Creature;       // DAT_0051ed84
	uint32 _r6Time;          // DAT_0051f73c
	int32 _r6MinX;           // DAT_0051f1d0: region 0xc's x0
	int32 _r6MaxX;           // DAT_0051f600: region 0xc's x1
	// Room 8
	int16 _r8Creature;       // DAT_0051f6c0
	int16 _r8Slot;           // DAT_0051f668
	uint32 _r8Time;          // DAT_0051f70c
	int _r8Turn;             // DAT_0051f650
	int _r8Zap;              // DAT_0051f708
	int _r8Gone;             // DAT_0051f1a0
	int _r8Ride;             // DAT_0051f6dc: Ned rides (costume 6)
	int _r8RideOn;           // DAT_0051f6e0
	int _r8RideOff;          // DAT_0051f6e4
	int _r8Action;           // DAT_0051f6d8
};

} // End of namespace NightmareNed

#endif // NIGHTMARENED_SCENE3_H
