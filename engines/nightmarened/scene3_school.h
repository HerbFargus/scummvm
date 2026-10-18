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

#ifndef NIGHTMARENED_SCENE3_SCHOOL_H
#define NIGHTMARENED_SCENE3_SCHOOL_H

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
 * Scene 3 sub-scene 0, the chalkboard school (NED95000, area 4; code 0x421a30-0x425ad6).
 * A 1920 x 960 level of 24 strips of 80 x 960 that wraps horizontally (Ned's wrap mode 1)
 * and scrolls vertically between its two halves. Four platforms ride up and down and one
 * slides sideways; three traps bump Ned when he walks into them and change when zapped;
 * two dispensers (regions 200 and 0xd2) release a chalk creature that floats after Ned
 * and knocks him over until it is zapped. One door (region 0x96) takes Ned back to the
 * start; the other (region 0xdc) opens once the switch 0x187 is zapped and leads back to
 * the corridor hub (scene 3).
 */
class Scene3School : public Scene {
public:
	explicit Scene3School(Game *game);
	~Scene3School() override;

	int load(int fromScene, int fromSub) override;   // m2 0x4224f0
	int enter(int fromScene, int fromSub) override;  // m3 0x421b50
	int leave(int toScene, int toSub) override;      // m4 0x4225e0
	int unload(int toScene, int toSub) override;     // m5 0x422690
	int tick(uint32 input, uint32 now) override;     // m7 0x422bf0

	const Common::Point &getNedPosition() const { return _nedPos; }

private:
	enum {
		kArea = 4,
		kNafSchool = 7,            // ned95000
		kNedList = 0x11,
		kList = 0x49d1a0,          // 0x4d entries, scene 99
		kListCount = 0x4d,
		kListScene = 99,
		kStripList = 0x49d278,     // the 24 strips (bitmaps 0..23)
		kStripCount = 24,
		kStripW = 0x50,
		kWorldW = 0x780,
		kStripBitmaps = 0x4e3948,  // destinations of the strips
		kPaletteVA = 0x4baf70,     // bitmap 0's palette
		kMapLevel = 0x4bc1fc,      // cbix 1: the level
		kMapSlide = 0x4bb958,      // cbix 11: the sliding platform (slot 0x189)
		kMapLift1 = 0x4bb95c,      // cbix 12..15: the lifts (slots 0x118..0x11b)
		kMapLift2 = 0x4bb960,
		kMapLift3 = 0x4bb964,
		kMapLift4 = 0x4bb968,
		kSstAreas = 0x4afcd8,      // .SST area prefix per area index (i16)
		kMusic = 0x7f,             // 95002208.wav
		kLifts = 5,
		kTableSize = 360
	};

	// The slots of list 0x49d1a0
	enum {
		kTrapA = 0xde,       // + 1 bump, + 2 zapped, + 3 zapped bump, + 4 done
		kTrapB = 0xe3,
		kTrapC = 0xe8,
		kGhostGone = 0xed,
		kGhost = 0xee,
		kSpawnerA = 0xef,
		kSpawnerOpen = 0xf0,
		kSpawnerB = 0x1a5,
		kDoorStart = 0xd7,   // region 0x96: back to the start
		kDoorOpening = 0x116,
		kDoorOpen = 0xd9,
		kDoorExit = 0x11c,   // region 0xdc: back to the hub
		kSwitch = 0x187,
		kSlide = 0x189,
		kLift1 = 0x118
	};

	struct Strip {
		int32 worldX;   // +0x00
		int32 screenX;  // +0x04
		int32 screenY;  // +0x08
		int32 index;    // +0x0c
		int next, prev; // +0x10, +0x14 (always i + 1, i - 1 around the ring)
	};

	typedef Common::HashMap<uint32, Request> EntryMap;

	// Resources
	Slot &slot(int id);
	BitmapRec *bmp(uint32 va);
	CollisionMap *map(uint32 va);
	int submit(uint32 va, int count, int scene);
	void release(uint32 va, int count);
	bool isLoaded(uint32 va) const { return _entries.contains(va); }
	void freeAll();
	uint32 entryFor(int id, int type) const;
	int showSprite(int id, int z, bool visible);
	int showBitmap(uint32 va, int z, bool visible);

	// Display (area 4)
	int16 switchSlot(int newSlot, int oldSlot);
	int16 switchSlotPre(int newSlot, int oldSlot);
	int place(int id, int ref);
	void attachPos(int id, int ref, int out[2], bool noMirror);
	void boxRect(int id, int frame, int k, int32 out[4]);
	void nudge(int id, int dx, int dy);

	// Sound
	void playSound(int id);
	void playLoop(int id);
	void stopSound(int id);
	void stopAllSounds();

	// Collision
	void addMap(uint32 va);
	static void regionEnable(CollisionMap *m, int32 id, bool on);
	Common::Array<CollisionMap *> overlayMaps() const;

	// Targets (DAT_004bb998)
	void targetAdd(int id);
	void targetRemove(int id);

	// Ned
	Ned *ned();
	void nedTick(uint32 input, uint32 now);
	int stripScreenX(int x) const;
	static void xform(void *user, int &x, int &y);
	int switchTo(int scene);

	// The level
	void stripsInit();
	void stripsMove(int &dx, int &dy);
	void camera(uint32 now);
	void trapTick(int16 &cur, int base, int stepA, int actionA, int stepB, int actionB, uint32 now);
	void spawnerTick(int16 &cur, int base, uint32 now);
	void ghostTick(uint32 now);
	void ghostMove();
	void doorStartTick(uint32 now);
	bool doorExitTick(uint32 now);
	void liftsTick(uint32 now);
	void slideMove(uint32 now);
	void liftMove(uint32 va, uint32 now, int index, int16 id);
	int findTarget(int16 &dir, int16 &found, int32 rect[4]);
	int trapRegion(int16 &cur, int base);
	int trapHit(int16 &cur, int base, int16 found);

	// Hooks (DAT_004bc1b8)
	static int hookUse(void *user, int x, int y, int16 *state);
	static int hookRegion(void *user, int32 id, uint32 flags, int x, int y, int16 *state);
	static void hookFindTarget(void *user, int x, int y, int16 *a, int16 *b, int32 rect[4]);
	static int hookHitTarget(void *user, int x, int y, int16 *a, int16 *b, const int32 rect[4]);
	int region(int32 id, uint32 flags);
	int hitTarget(int16 a, int16 found);

	// ---- Resources and the world ----
	EntryMap _entries;                              // loaded request entries by VA
	Common::HashMap<uint32, BitmapRec *> _bitmaps;  // bitmap records by destination VA
	Common::HashMap<uint32, CollisionMap *> _maps;  // collision maps by destination VA
	byte _palette[256 * 3];       // DAT_004baf70
	CollisionWorld _world;        // DAT_004baf6c
	CollisionMap _userMap;        // DAT_004bbbf0: made and freed, never given to the world
	Common::Array<Placement> _targets; // DAT_004bb998: what Ned's zap can hit
	Common::Array<Placement> _sst;     // DAT_004bb99c: 95999001.SST
	NedHooks _hooks;              // DAT_004bc1b8
	Common::Point _nedPos;        // DAT_00525df8 / DAT_00525dfc
	int _scrollX, _scrollY;       // DAT_004bb950 / DAT_004bb954: strip 0's screen position
	Strip _strips[kStripCount];   // DAT_004bb9b0
	int _stripsUnused;            // DAT_004bbbf4
	int32 _sinTable[kTableSize];  // DAT_004bbbf8: sin(i * 0.0174) * 512
	int32 _cosTable[kTableSize];  // DAT_004bb3a0: cos(i * 0.0174) * 512
	int _unportedExit;            // the last exit to a scene that isn't registered (logged once)
	uint32 _lastTick;             // the 41 ms throttle (not in the original, see tick())

	// ---- .data (0x49d178..0x49d198) ----
	int16 _trapA;            // DAT_0049d178: 0xde..0xe2
	int16 _doorStart;        // DAT_0049d17c: 0xd7, 0x116, 0xd9
	int16 _doorExit;         // DAT_0049d180: 0x11c, 0x116, 0xd9
	int16 _trapB;            // DAT_0049d184: 0xe3..0xe7
	int16 _trapC;            // DAT_0049d188: 0xe8..0xec
	int16 _spawnerA;         // DAT_0049d18c: 0xef, 0xf0
	int16 _spawnerB;         // DAT_0049d190: 0x1a5, 0xf0
	int16 _unused194;        // DAT_0049d194 (only written)
	int16 _ghost;            // DAT_0049d198: 0xee, 0xed or -1

	// ---- .bss ----
	int _fromScene, _fromSub;     // DAT_004bb970 / DAT_004bc1e0 (only written)
	Common::Point _startPos;      // DAT_004bb978 / DAT_004bb97c: where the start door puts Ned
	int _startScrollX, _startScrollY; // DAT_004bb398 / DAT_004bb39c
	int _doorStartOn;        // DAT_004bc1c8: the start door is playing (input off)
	int _doorExitOn;         // DAT_004bc204: the exit door is playing (input off)
	int _switchOn;           // DAT_004bc1cc: the switch 0x187 plays to its end, then opens region 0xdc
	int _lastScreenX;        // DAT_004bc208
	int _lastScreenY;        // DAT_004bc20c
	uint32 _cameraTime;      // DAT_004bc210: when the camera last moved
	int _liftDir[kLifts];    // DAT_004bc19c, 1a4, 1a0, 1ac, 1a8: 1 = right / down
	uint32 _liftLast[kLifts]; // DAT_004bc200, 4bb980, 4baf68, 4bb984, 4bb988: last step
	int _liftMin[kLifts];    // DAT_004bb96c, 4bb9a8, 4bb9ac, 4bb9a0, 4bb9a4
	int _liftMax[kLifts];    // DAT_004bc1b0, 4bc1d4, 4bc1d0, 4bc1dc, 4bc1d8
	uint32 _liftPause[kLifts]; // DAT_004bc1e8[]: by slot (0x189 = 0, 0x118.. = 1..4)
	int _ghostPhaseX;        // DAT_004bb378
	int _ghostPhaseY;        // DAT_004bb37c
	int _ghostBaseX;         // DAT_004bb380
	int _ghostBaseY;         // DAT_004bb384
	int _ghostPeriodX;       // DAT_004bb388
	int _ghostPeriodY;       // DAT_004bb38c
	int _ghostAmpX;          // DAT_004bb390
	int _ghostAmpY;          // DAT_004bb394
	int _ghostX;             // DAT_004bb948
	int _ghostY;             // DAT_004bb94c
	int _ghostNedX;          // DAT_004bb990
	int _ghostNedY;          // DAT_004bb994: the top of Ned's box
};

} // End of namespace NightmareNed

#endif // NIGHTMARENED_SCENE3_SCHOOL_H
