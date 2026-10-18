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

#ifndef NIGHTMARENED_SCENE2_H
#define NIGHTMARENED_SCENE2_H

#include "common/array.h"
#include "common/hashmap.h"
#include "common/rect.h"

#include "nightmarened/data.h"
#include "nightmarened/game.h"
#include "nightmarened/ned.h"
#include "nightmarened/resources.h"
#include "nightmarened/world.h"

namespace NightmareNed {

class Scene2;

/**
 * What the scene 2 parts share: the request entries they loaded (by VA), the slot
 * helpers 0x4411b0 / 0x441280, sounds with end callbacks, and the .SST placements.
 */
class Scene2Part : public Scene {
public:
	Scene2Part(Game *game, int id, Scene2 *world) : Scene(game, id), _s2(world) {}
	~Scene2Part() override;

protected:
	enum {
		kArea = 5,      // NED93000's slots (shared with scene 7's NED94000)
		kNedArea = 1,
		kNaf = 5,       // ned93000
		kSstAreas = 0x4afcd8,
		kSstArea = 5    // 0x45a560's first argument: prefix 94
	};

	typedef Common::HashMap<uint32, Request> EntryMap;

	Slot &slot(int area, int id);
	Ned *ned() { return _game->ned(); }

	// Request lists (0x4586e0 / 0x458dc0, synchronous)
	int submit(uint32 va, int count, int scene);
	int submitBuilt(uint32 va, Common::Array<Request> &list, int scene);
	void release(uint32 va, int count);
	void releaseAll();
	bool isLoaded(uint32 va) const { return _entries.contains(va); }
	BitmapRec *bmp(uint32 va);
	CollisionMap *map(uint32 va);
	const byte *palette(uint32 va);
	/** 0x434370: the list's entry of type and id, or 0 */
	uint32 findEntry(int type, int id, uint32 list, int count) const;
	/** 0x459090: a sprite entry gets draw order z and visibility on the primary surface */
	void showSprite(uint32 entry, int z, bool visible);
	/** 0x459100: the same for a bitmap entry */
	void showBitmap(uint32 entry, int z, bool visible);
	/** 0x459000: every sprite and bitmap of the list goes to the primary surface, hidden */
	void attachList(uint32 list, int count);
	/** hbmpSetTransparencyColor on a bitmap record */
	void setBitmapKey(BitmapRec *rec, int key);

	// Slots (0x430530 is slot_place without an xform)
	int place(int area, int id, int ref);
	/** 0x4411b0: the next step when it is due; 0 once the sequence has ended */
	int stepSlot(int area, int id, int ref, uint32 now);
	/** 0x441280: slot_switch, then place against ref and set the deadline (random: a random step) */
	int16 switchPlace(int area, int newSlot, int oldSlot, int ref, uint32 now, bool random);
	void visible(int area, int id, bool on);
	int frameCount(int area, int id);

	// Sound
	/** 0x455880 */
	void playSound(int area, int id);
	/** 0x455800: callback runs (once) when the sound has ended; one that fails to play counts as ended */
	void playSoundNotify(int area, int id, uint32 callback);
	void pollSounds();
	virtual void soundEnded(uint32 callback) {}
	/** 0x455950: every slot voice stops */
	void stopAllSounds();

	// Placements (0x45a560 / 0x45a5f0 / 0x4870a0 with 0x441330)
	bool loadSst(int index, Common::Array<Placement> &out);
	void applySst(const Common::Array<Placement> &sst, const int *offset);

	// The 0x41 ms tick wait (see Scene2Part::wait)
	bool wait(uint32 now);

	Scene2 *_s2;
	EntryMap _entries;
	Common::HashMap<uint32, BitmapRec *> _bitmaps;
	Common::HashMap<uint32, CollisionMap *> _maps;
	Common::HashMap<uint32, Common::Array<byte> > _palettes;
	struct Notify {
		int area, id;
		uint32 callback;
	};
	Common::Array<Notify> _notify;
	uint32 _lastTick = 0;
};

/**
 * Scene 2, the hospital (registered by 0x449f70; NED93000, area 5): the "m" patch of the
 * quilt. The top-level scene only opens the NAF and moves on to the sub-scene in
 * DAT_004c4434: 0 the dark room, 1 the gurney ride, 2 the jar room; 3 is empty. Exits
 * to scene 4 (the attic), 5 and 7 (the mouth) go through it too. Scene 7 comes back here.
 */
class Scene2 : public Scene {
public:
	enum {
		kNaf = 5 // ned93000
	};

	explicit Scene2(Game *game);

	int load(int fromScene, int fromSub) override;   // m2 0x44a140
	int unload(int toScene, int toSub) override;     // m5 0x44a1a0
	int tick(uint32 input, uint32 now) override;     // m7 0x44a1c0

	/** 0x482e60: the armchair (the attic exit) joins sub 0 (scene 7 cleared, or the jar room won) */
	void openExit() { _exitOpen = 1; }

	int16 _next;          // DAT_004c4434: the sub-scene (or 4, 5, 7) the top-level tick goes to
	int _from;            // DAT_004c4438: the scene the world was entered from
	int32 _exitOpen;      // *DAT_00521840 (sub 0's 4-byte saved state)
	int16 _caught;        // DAT_004a3df4: grabs on the ride (0..8), the jar room's critters
	Common::Point _nedPos; // DAT_00525df8 / DAT_00525dfc
	int _unportedExit;    // the last exit to a scene that isn't registered (logged once)
};

/**
 * Sub 0: a dark room where, under a lamp, a gurney (sub 1), a dentist's chair (scene 7) and,
 * once open, an armchair (the attic) appear in turn; Ned takes the one shown with a button
 */
class Scene2Hall : public Scene2Part {
public:
	Scene2Hall(Game *game, Scene2 *world);

	int init() override;                             // m0 0x481d40
	void shutdown() override;                        // m1 0x481d00 (nothing)
	int load(int fromScene, int fromSub) override;   // m2 0x481db0
	int enter(int fromScene, int fromSub) override;  // m3 0x481fb0
	int leave(int toScene, int toSub) override;      // m4 0x4821f0
	int unload(int toScene, int toSub) override;     // m5 0x4821e0
	void suspend() override;                         // m6 0x481d20
	int tick(uint32 input, uint32 now) override;     // m7 0x4822e0
	int getSaveSize() const override { return 4; }   // m8 0x481d60
	void saveState(int32 *out) override;             // m9 0x481d70
	void restoreState(const int32 *in, int size) override; // m10 0x481d80

private:
	enum {
		kMainList = 0x4b63b0,     // 12
		kDoorList = 0x4b6560,     // 2: 0xb1, 0x5d
		kAtticList = 0x4b65a8,    // 2: 0xae, 0x5e
		kMap = 0x521c84,
		kBackdrop = 0x4e50bc,
		kPalette = 0x521870,
		kHallMusic = 0x81,
		kListCb = 0x482e70,       // slot 100's sound ended
		kDoorBelt = 0xb3,
		kDoorMouth = 0xaf,
		kDoorAttic = 0xad
	};

	void doorTick(uint32 now);
	void soundEnded(uint32 callback) override;
	void nedTick(uint32 input, uint32 now);
	static void xform(void *user, int &x, int &y);
	static int hookUse(void *user, int x, int y, int16 *state);
	static int hookRegion(void *user, int32 id, uint32 flags, int x, int y, int16 *state);
	static int hookHit(void *user, int x, int y, int16 *a, int16 *b, const int32 rect[4]);
	void setupWorld();
	void placeAt(int area, int id, int x, int y);

	CollisionWorld _world;        // DAT_00521c80
	Common::Array<Placement> _sst; // DAT_0052183c: 94999000.SST
	NedHooks _hooks;              // DAT_00521860
	int _hintPending;             // DAT_004b63a8
	int _sstPending;              // DAT_004b63ac
	int16 _door;                  // DAT_00521834: the door that turns next (0xad, 0xaf, 0xb3)
	int16 _doorSlot;              // DAT_00521838: its slot on screen (-1 waiting, -2 none yet)
	int _useWanted;               // DAT_00521844 (u16)
	int _scrollX, _scrollY;       // DAT_00521850 / DAT_00521854 (always 0)
	int16 _doorOpening;           // DAT_00521858: the opening slot of _door
	int16 _lampSlot;              // DAT_00521c74: the lamp, 0x60 (lit) / 0xb8 (swinging)
	int16 _nedSlot;               // DAT_00521c78: Ned's walk through a door
	int16 _nedActive;             // DAT_00521c7c: ned_tick runs (< 0: input held off for that many ticks)
	uint32 _doorTime;             // DAT_00521c88: the next door may turn
	int _atticLoaded;             // DAT_00521c8c
	int _beltShown;               // DAT_00521c90
	int _mouthShown;              // DAT_00521c94
	int _atticShown;              // DAT_00521c98
	int _buzzEnded;                // DAT_00521c9c
	int _buzzStarted;              // DAT_00521ca0
	int _leaving;                 // DAT_00521ca4
};

/**
 * Sub 1: Ned rides a gurney down a corridor; a surgeon reaching out of a door at the height
 * of Ned's row (Up/Down) grabs him. The grabs (up to 8) become the jar room's critters.
 */
class Scene2Belt : public Scene2Part {
public:
	Scene2Belt(Game *game, Scene2 *world);

	int load(int fromScene, int fromSub) override;   // m2 0x43ffc0
	int enter(int fromScene, int fromSub) override;  // m3 0x440060
	int leave(int toScene, int toSub) override;      // m4 0x440290
	int unload(int toScene, int toSub) override;     // m5 0x440280
	int tick(uint32 input, uint32 now) override;     // m7 0x4404b0

private:
	enum {
		kMainList = 0x4a39e0,     // 26
		kEndList = 0x4a3d88,      // 3: the bitmap the belt ends at and slot 0x61
		kEndSprite = 0x4a3dac,
		kSoundList = 0x4bf988,    // 4, built by 0x441400
		kSoundIds = 0x4a39b8,     // 10 sound slots
		kRowSlots = 0x4951f8,     // Ned's slot per row
		kCatchSlots = 0x495200,   // Ned's catch per row
		kItems = 0x495208,        // 36 doors: the surgeon's row, -1 = none
		kBelt = 0x4e524c,
		kFront = 0x4e525c,
		kEnd = 0x4e53e0,
		kPalette = 0x4bfa28,
		kBeltMusic = 0x80,
		kLanes = 4,
		kCatch = 0x3e,
		kMiss = 0x58,
		kNed = 0xa3
	};

	void spawn();                                // 0x440350
	int nedTick(uint32 now);                     // 0x4408c0
	void laneTick(uint32 now);                   // 0x440cb0
	void laneTake(int lane, int16 kind, uint32 now); // 0x4410a0
	void buildSounds();                          // 0x441400

	struct Lane {
		int16 slot;     // DAT_004bf970: 300 + lane, kCatch / kMiss, -1 = empty
		int16 pending;  // DAT_004bf972
		int16 row;      // DAT_004bf974
	};
	Lane _lanes[kLanes];
	Common::Array<Request> _sounds;    // DAT_004bf988
	Common::Array<Placement> _sst;     // DAT_004bfe50: 94999001.SST
	int _loaded;                       // DAT_004bf968
	int16 _target;                     // DAT_004bfa1c: the row the player asks for
	int16 _row;                        // DAT_004bfa20: Ned's row (0..2)
	int16 _endSlot;                    // DAT_004bfa18
	int32 _soundPick[4];               // DAT_004bfe30
	int16 _rowY[4];                    // DAT_004bfe48: the lanes' y per row
	uint16 _catching;                  // DAT_004bfe58: a bit per lane whose surgeon grabs Ned
	uint32 _lastInput;                 // DAT_004bfe5c
	int16 _nedSlot;                    // DAT_004bfe60
	int16 _spawned;                    // DAT_004bfe64
	int _phase;                        // DAT_004bfe68: 0 running, 1 ending, 2 the end slides in, 3 Ned rides off
	int _soundIndex;                   // DAT_004bfe6c
};

/**
 * Sub 2: the jar room. Ned, strapped to a spinning arrow, stops it (button 2) at a jar; a
 * creature hops there and eats the critter in it, then bites Ned. Eight turns ("now
 * serving"); with every critter gone Ned drops to the floor and walks out (left: sub 0,
 * right: scene 5) and the attic exit opens; otherwise he is sent back to sub 0.
 */
class Scene2Board : public Scene2Part {
public:
	Scene2Board(Game *game, Scene2 *world);

	int load(int fromScene, int fromSub) override;   // m2 0x482ef0
	int enter(int fromScene, int fromSub) override;  // m3 0x482f70
	int leave(int toScene, int toSub) override;      // m4 0x4834d0
	int unload(int toScene, int toSub) override;     // m5 0x4834a0
	int tick(uint32 input, uint32 now) override;     // m7 0x483570

private:
	enum {
		kMainList = 0x4b6620,     // 0x2f
		kMap = 0x521cd8,
		kBackdrop = 0x4e5574,
		kLayer = 0x4e5588,
		kBox = 0x4e558c,
		kShelf = 0x4e5590,
		kPalette = 0x521d78,
		kPieceSlots = 0x495490,   // 8
		kBoardMusic = 0x80,
		kNedRef = 0x145,
		kHome = 8,                // position 0x198
		kNedSpot = 9,             // position 0x199
		kCbPiecesHigh = 0x484ae0,
		kCbPiecesLow = 0x484af0,
		kCbSpin = 0x484b00
	};

	void nedAnimTick(uint32 input, uint32 now);  // 0x483a80
	void creatureTick(uint32 now);               // 0x4840a0
	void piecesTick(uint32 now);                 // 0x484680
	void meterTick(uint32 now);                  // 0x4847a0
	void camera(uint32 now, int d[2]);           // 0x4848c0
	void soundEnded(uint32 callback) override;
	void nedTick(uint32 input, uint32 now);
	static void xform(void *user, int &x, int &y);
	static int hookUse(void *user, int x, int y, int16 *state);
	static int hookRegion(void *user, int32 id, uint32 flags, int x, int y, int16 *state);
	static int hookHit(void *user, int x, int y, int16 *a, int16 *b, const int32 rect[4]);
	void moveSlot(int area, int id, int dx, int dy);

	struct Piece {
		int32 used;   // DAT_00521ce0
		int16 slot;   // DAT_00521ce4
	};
	Piece _pieces[8];
	CollisionWorld _world;        // DAT_00522194
	Common::Array<Placement> _sst; // DAT_00521d70: 94999002.SST
	NedHooks _hooks;              // DAT_00521d30
	int _scroll[2];               // DAT_00521d20 / DAT_00521d24
	int _highSound;               // DAT_004b6610
	int _lowSound;                // DAT_004b6614
	int _spinSound;               // DAT_004b6618
	int16 _jumpTo;                // DAT_00521cac: 0xba / 0x42, how the creature eats
	int16 _creature;              // DAT_00521cb0
	int16 _at;                    // DAT_00521cb4: the position the creature is at
	int _scrollMode;              // DAT_00521cb8: 0 none, 1 up, 2 at the bottom, 3 follows Ned
	int16 _pieceCount;            // DAT_00521cbc
	int _meterState;              // DAT_00521cc0
	int16 _nedSlot;               // DAT_00521cc4
	int16 _jumpStep;              // DAT_00521cc8
	int _jumpFrom[2];             // DAT_00521cd0 / DAT_00521cd4
	int _scrollModeWas;           // DAT_00521d28
	int16 _misses;                // DAT_00521d40
	int _meterWanted;             // DAT_00521d44
	int _jumpPhase;               // DAT_00521d48
	uint32 _waitTime;             // DAT_00521d4c
	int16 _atSlot;                // DAT_00521d50: 0x190 + _at
	int _walking;                 // DAT_00521d58: Ned walks (the board is won)
	int _jumpHeight;              // DAT_00521d64
	int _jumpTarget[2];           // DAT_00521d68 / DAT_00521d6c
	int16 _meterSlot;             // DAT_0052217c
	int16 _jumpSteps;             // DAT_00522180
	int _nedState;                // DAT_00522184
	int16 _goal;                  // DAT_00522188: the position the creature goes to
	int _spin;                    // DAT_00522190: ms per step of Ned's spin
	int _wanted;                  // DAT_00521ca8: the Ned state asked for
	int _done;                    // DAT_00522198
	int _loaded;                  // DAT_005221a0
	uint32 _scrollTime;           // DAT_005221b0
};

/** Sub 3: every method is empty (0x44e610-0x44e650) */
class Scene2Empty : public Scene {
public:
	explicit Scene2Empty(Game *game) : Scene(game, 3) {}
};

} // End of namespace NightmareNed

#endif // NIGHTMARENED_SCENE2_H
