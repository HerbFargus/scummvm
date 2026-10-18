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

#ifndef NIGHTMARENED_SCENE6_H
#define NIGHTMARENED_SCENE6_H

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
 * Scene 6, the playroom (registered by 0x434830; NED96000, area 4). A 640 x 480 room
 * entered from scene 3. A toy box (slot 0x2d) opens when Ned zaps it and shows a
 * spinner (0x2f); zapping that picks one of four rooms (the sub-scenes), each with a
 * monster to beat. Every room visited and every monster beaten leaves a token here;
 * four beaten monsters play the finale. Region 3 leads to the attic, region 4 to
 * scene 3, and so does the hole 0xf9 once zapped.
 */
class Scene6 : public Scene {
public:
	explicit Scene6(Game *game);
	~Scene6() override;

	int init() override;                             // m0 0x430d60
	void shutdown() override;                        // m1 0x430d90
	int load(int fromScene, int fromSub) override;   // m2 0x430f40
	int enter(int fromScene, int fromSub) override;  // m3 0x431980
	int leave(int toScene, int toSub) override;      // m4 0x430eb0
	int unload(int toScene, int toSub) override;     // m5 0x430dc0
	int tick(uint32 input, uint32 now) override;     // m7 0x432e20
	int getSaveSize() const override { return 0x24; }       // m8 0x4347a0
	void saveState(int32 *out) override;                    // m9 0x4347b0
	void restoreState(const int32 *in, int size) override;  // m10 0x4347d0

	enum {
		kArea = 4,
		kNafPlayroom = 8,          // ned96000
		kNedList = 5,
		kNedZ = 8,                 // DAT_004c56d4, Ned's draw order (always 8)
		kSharedList = 0x4a1a68,    // the counters, the vehicle and their sounds
		kSharedCount = 0x4a1bf4,   // i16 count of kSharedList
		kSharedCallback = 0x430d50,
		kWipeList = 0x4a1a40,      // 99:1, colour key 10 -> DAT_004be698: the frame the wipes play in
		kWipeBitmap = 0x4be698,
		kMusicPlayroom = 0x83,     // 96002208.wav
		kMusicWipe = 0x84,         // 96012208.wav, while a wipe closes
		kWipeClosePlayroom = 0x38, // 0404800p.smk
		kWipeCloseRoom = 0x37,     // 04048001.smk
		kWipeOpen = 0x39           // 0404800r.smk, into the next room or the playroom
	};

	// The saved block (DAT_004be6c0..DAT_004be6e0): per room, beaten then visited
	enum {
		kWon0, kVisited0, kWon1, kVisited1, kWon2, kVisited2, kWon3, kVisited3, kBoxOpen, kSavedCount
	};
	int32 _saved[kSavedCount];

	// ---- Resources, shared with the rooms (one NAF, area 4) ----
	Slot &slot(int id);
	void visible(int id, bool on);
	/** objSetDestination + objSetDrawOrder on a slot's sprite */
	void attach(int id, int z);
	BitmapRec *bmp(uint32 va);
	CollisionMap *map(uint32 va);
	/** A palette a bitmap entry stored at va (256 x rgb), or nullptr */
	const byte *palette(uint32 va);
	/** 0x4586e0 (synchronous): loads count entries at va with the scene id */
	int submit(uint32 va, int count, int scene);
	/** 0x458dc0: frees backwards */
	void release(uint32 va, int count);
	bool isLoaded(uint32 va) const { return va && _entries.contains(va); }
	/** 0x434340: the first entry of the list with the id (any type), or 0 */
	uint32 entryFor(int id, uint32 list, int count) const;
	static BitmapRec *lookupBitmap(void *user, uint32 va);
	/** 0x4870a0 with 0x43e460 over a placement list */
	void applyPlacements(const Common::Array<Placement> &list);

	int16 switchSlot(int newSlot, int oldSlot);
	int16 switchSlotPre(int newSlot, int oldSlot);
	/** 0x430410 slot_place_dur (noMirror, no xform): returns the step's duration */
	int place(int id, int ref);
	/** 0x486930 + objSetCurrentImage on a slot's current step: returns the duration */
	int setImage(int id);
	/** 0x486930 + 0x4860d0: rect k of the frame of a slot's step */
	void stepRect(int id, int step, int k, int32 out[4]);
	/** 0x455880 snd_play */
	void playSound(int id);
	/** 0x455950: every slot voice stops */
	void stopAllSounds();

	/** 0x4342b0: hides the old slot (when loaded), shows the new one (when loaded), unless they are the same */
	int16 swapShown(int16 newSlot, int16 oldSlot, uint32 newEntry, uint32 oldEntry);
	/** 0x433b20: one more step on the damage counter 0x12e; its last sets _beaten */
	void damageStep();
	/** 0x433b90: the three-stage timer 0x132 / 0x136 / 0x137; the end of the last sets _timeUp */
	void timerTick(uint32 now, bool reset);
	/** 0x4343b0 / 0x434480: the one-shot effects 0x133 and 0x134 */
	void effect133Tick(uint32 now);
	void effect134Tick(uint32 now);
	/** 0x433d70 / 0x433da0: the wipe frame bitmap */
	int loadWipe();
	void freeWipe();
	/**
	 * The Smacker wipes between the playroom and a room: 0x433db0 closes the playroom,
	 * 0x433f70 a room (both with kMusicWipe), 0x434130 opens the next one (then the
	 * playroom music, which the rooms keep)
	 */
	int closePlayroom();
	int closeRoom();
	int openWipe();
	/** Test hooks only: loads what the playroom leaves loaded for a room */
	void prepareRoomDirect();
	/** Leaving the world: the built list's memory, the shared list, (the music) and the NAF */
	void closeAll(bool stopMusic);
	/** 0x45a0f0 to a scene that may not be ported yet; returns 0 when it switched */
	int switchTo(int scene, int sub);
	/**
	 * 0x433980 / 0x427d90 and copies: the target in front of Ned in list, and box k 0 (3
	 * when it has a flip effect) of its step's frame at its sprite. clampStep reads step
	 * -1 as 0 (the rooms do, the playroom doesn't).
	 */
	bool findTarget(const Common::Array<Placement> &list, int16 &dir, int16 &found, int32 rect[4], bool clampStep);
	/** 0x4309c0 olist_add_sprite (noFlip 1) / 0x486f50 olist_remove on an area-4 list */
	static void targetAdd(Common::Array<Placement> &list, int id);
	static void targetRemove(Common::Array<Placement> &list, int id);
	Ned *ned() { return _game->ned(); }
	/** The collision overlay, then srfDraw */
	void draw(const Common::Array<CollisionMap *> &maps);

	Common::Point _nedPos;   // DAT_00525df8 / DAT_00525dfc (shared with the rooms)
	int _vehicleHomeX;       // DAT_004bde80: slot 0x2c's x, where the vehicle 0x31 parks
	int _timeUp;             // DAT_004bf7dc: a room's timer ran out
	int _beaten;             // DAT_004bf7e0: a room's damage counter is full
	int _effect133;          // DAT_004bf7e4
	int _effect134;          // DAT_004bf7e8
	int _unportedExit;       // the last exit to a scene that isn't registered (logged once)

private:
	typedef Common::HashMap<uint32, Request> EntryMap;

	void freeAll();
	void keepVehicleAnchor();
	/** 0x434560: one entry of the built list */
	void setEntry(int index, int area, int id, int type, uint32 dest, uint32 pal);
	int submitBuilt();
	void releaseBuilt();
	bool builtLoaded(int index) const;
	/** 0x459100 on a built bitmap entry: order, primary, optionally its record's position, then visible */
	void showBuilt(int index, uint32 dest, int z, bool position);
	int playWipe(int fileIndex, int music, int soundFrame, bool hideVehicle, bool hideAfter);
	Common::Array<CollisionMap *> overlayMaps() const;

	int boxTick(uint32 now);
	void spinnerTick(uint32 now);

	static int hookUse(void *user, int x, int y, int16 *state);
	static int hookRegion(void *user, int32 id, uint32 flags, int x, int y, int16 *state);
	static void hookFindTarget(void *user, int x, int y, int16 *a, int16 *b, int32 rect[4]);
	static int hookHitTarget(void *user, int x, int y, int16 *a, int16 *b, const int32 rect[4]);

	EntryMap _entries;                              // loaded request entries by VA
	Common::HashMap<uint32, BitmapRec *> _bitmaps;  // bitmap records by destination VA
	Common::HashMap<uint32, CollisionMap *> _maps;  // collision maps by destination VA
	Common::HashMap<uint32, Common::Array<byte> > _palettes; // 256 x rgb by destination VA
	Common::Array<Request> _built;  // DAT_004beef0: the 50-entry list 0x430f40 builds
	Common::Array<bool> _builtOk;
	int16 _builtCount;              // DAT_004befc8
	CollisionWorld _world;          // DAT_004bdd74
	CollisionMap _userMap;          // DAT_004be6b0
	Common::Array<Placement> _targets; // DAT_004bde7c
	Common::Array<Placement> _sst;     // DAT_004bde94: 96999000.SST
	NedHooks _hooks;                // DAT_004be6a0
	uint32 _lastTick;
	HitBoxes _staleBoxes;           // slot 0x2c's hit boxes after the playroom frees them
	bool _preparing;

	int16 _boxSlot;          // DAT_004be69c: the box slot shown
	int16 _entry1;           // DAT_004be6b4: built entry of room 1's token
	int _selected;           // DAT_004be6b8: the room the spinner points at (-1 = none)
	byte _palMain[256 * 3];  // DAT_004be6e8 (bitmap 9's, with Ned's colours)
	int _holeOpen;           // DAT_004beee8: the hole 0xf9 plays to its end, then scene 3
	int16 _entryIndex;       // DAT_004beeec
	int16 _state;            // DAT_004beefc
	int16 _entry0;           // DAT_004bf7d0
	int16 _entry2;           // DAT_004bde88
	int16 _entry3;           // DAT_004bde90
	int _sharedLoaded;       // DAT_004bf7d4 (set by the list callback 0x430d50)
	int _finale;             // DAT_004bf7ec
	int _allWon;             // DAT_004bf7f0
	int _allVisited;         // DAT_004bf7f4: every room visited, none beaten
	int _toAttic;            // DAT_004bf7f8
	int _toScene3;           // DAT_004bf7fc
	int _sharedShown;        // DAT_004bf800
	int _hintDone;           // DAT_004bf804
	int _timer1, _timer2, _timer3; // DAT_004bf808 / DAT_004bf80c / DAT_004bf810

	// .data statics (0x4a1bf8..0x4a1c1c), kept across visits
	int _boxSound;           // DAT_004a1bf8
	int _sound133;           // DAT_004a1bfc
	int _sound134;           // DAT_004a1c00
	int _sound6b;            // DAT_004a1c04
	int _sound6d;            // DAT_004a1c08
	int _slideAX, _slideAY;  // DAT_004a1c10 / DAT_004a1c14: bitmap 0x14
	int _slideBX, _slideBY;  // DAT_004a1c18 / DAT_004a1c1c: bitmap 0x15
};

/**
 * What the four rooms share: each has its own collision world, targets and hooks, on
 * the playroom's resources (its parent).
 */
class Scene6Room : public Scene {
public:
	Scene6Room(Game *game, int id) : Scene(game, id), _lastTick(0) {
		_hooks.user = this;
		_hooks.use = nullptr;
		_hooks.region = nullptr;
		_hooks.findTarget = nullptr;
		_hooks.hitTarget = nullptr;
	}

protected:
	Scene6 *hub() { return (Scene6 *)_parent; }
	Slot &slot(int id) { return hub()->slot(id); }
	Ned *ned() { return _game->ned(); }
	Common::Point &nedPos() { return hub()->_nedPos; }
	/** The tick throttle (see Scene6::tick) */
	bool throttle(uint32 now);
	void draw();
	/**
	 * The start of each room's m3: its SST, the list's collision map in a new world with
	 * an empty region map, and Ned dropped onto the floor.
	 */
	int setupWorld(int sst, uint32 list, uint32 mapVA);
	/** The common part of each room's m4 (0x426260 and its copies) */
	int leaveRoom(int toScene, int savedVisited);
	/** The common part of each room's m5 (0x4262b0 and its copies) */
	int unloadRoom(int toScene, uint32 list, int count);
	/** objSetDestination + objSetDrawOrder + objSetVisibility on a list entry's sprite */
	void showEntry(uint32 list, int count, int id, int z, bool on);
	/** 0x4500c0 + box: the target rect in front of Ned (the rooms' 0x427d90 and copies) */
	bool findTarget(int16 &dir, int16 &found, int32 rect[4]);

	CollisionWorld _world;
	CollisionMap _userMap;
	Common::Array<Placement> _targets;
	Common::Array<Placement> _sst;
	NedHooks _hooks;
	uint32 _lastTick;
};

} // End of namespace NightmareNed

#endif // NIGHTMARENED_SCENE6_H
