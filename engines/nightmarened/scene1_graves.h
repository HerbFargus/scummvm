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

#ifndef NIGHTMARENED_SCENE1_GRAVES_H
#define NIGHTMARENED_SCENE1_GRAVES_H

#include "nightmarened/ned.h"
#include "nightmarened/scene1_base.h"

namespace NightmareNed {

class RdxGrid;
class RdxObject;
struct RdxEffect;

/**
 * What the three open graves (sub-scenes 3, 4, 5) share: the drop through the graveyard
 * video (0x441790), the fall down the shaft past three wall creatures (slots 25..33,
 * 0x441900), the climb back up (0x442a10) and the shaft itself, a grid of one 640 x 148
 * tile twelve times (0x442fb0). The globals the original shared between them
 * (DAT_00522d20..DAT_0052320c) live here; only one grave is loaded at a time.
 */
class GraveRoom : public GraveyardRoom {
public:
	GraveRoom(Game *game, int id, Scene1 *top);
	~GraveRoom() override;

protected:
	enum {
		kVideoList = 0x4a3e00,  // 1: the drop's frame bitmap (scene 99, key 10)
		kBmpVideo = 0x522d78,
		kBmpTop = 0x4de680,     // the grave's top bitmap (each grave's own list)
		kBmpShaft = 0x4de688,   // the shaft tile
		kWallFirst = 25,        // slots 25..33: three wall creatures, each idle / bitten / snapping
		kNedFall = 0x14,        // area-1 slots
		kNedTumble = 0x1a1,
		kNedClimb = 0x35,
		kNedFlip417 = 417,      // area-1 slot whose flip 0x441900 adds
		kNedBase = 1,           // area-1 slot 1: the reference Ned's sprites are placed against
		kNedSlot16 = 16,
		kNedSlot418 = 418,
		kNedSlot4 = 4,
		kShaftH = 0x6f0,        // DAT_00522d7c
		kMapFloor = 0x4bf918,   // the top's maps (Scene1::kMapFloor / kMapExits)
		kMapExits = 0x4bf91c
	};

	// ---- Shared code (scene1_graves.cpp) ----
	/** 0x441790: the drop, file 0x48 (a Smacker video) played into bitmap kBmpVideo */
	void dropVideo();
	/** 0x441900 (subs 3, 5): Ned falls down the shaft, tumbling off the three wall creatures */
	void fallShaft();
	/** 0x442a10: the climb back up the shaft (mode 0: Ned rises 6 px a step, 1: 2 px) */
	void climbOut(int16 mode);
	/** 0x442fb0: the shaft grid (1 x 12 cells of the kBmpShaft tile), order 99, hidden */
	int shaftCreate();
	void shaftFree();

	/** 0x4449d0 / 0x4013e0: the target Ned's zap finds, its box 0 rect in world coordinates */
	int zapTarget(int16 *dir, int16 *found, int32 rect[4]);
	/** 0x444020 / 0x444d00 / 0x401020: world -> screen (+ the room's scroll) */
	static void xform(void *user, int &x, int &y);
	/** 0x454920 ned_tick on this room's world, hooks and scroll */
	void nedTick(uint32 input, uint32 now);
	/** One climbing step of Ned's area-1 slot 0x35 at _climb, rising by rise (the loop bodies of 0x442680, 0x442a10, 0x445050, 0x401050) */
	void climbStep(uint32 now, int rise);

	/** 0x45a560(0, 999, n) + olist_foreach(0x43e460) */
	void loadPlacements(int n, Common::Array<Placement> &list);
	/** 0x446c10 on a palette loaded by a bitmap entry */
	void applyPaletteVA(uint32 va, int mode);
	/** submit() with one entry's NAF id replaced (the TEAM cheat of sub-scene 5) */
	int submitWithId(uint32 va, int count, int scene, uint32 entry, int id);
	/** 0x455800 snd_play_cb: the callback is returned by pollSoundEnd once the voice stops */
	void playSoundCallback(int id, int callback);
	/** The pending sound-end callback (once) when its voice is done, else 0 */
	int pollSoundEnd();
	/** 0x4558e0: plays a slot's sound looping */
	void loopSound(int id);

	// Display helpers, area 2 unless an area is given
	RdxObject *bkg(uint32 va);
	int16 bmpHeight(uint32 va);
	void setPos(int id, int x, int y) { setPosAt(kArea, id, x, y); }
	void setPosAt(int area, int id, int x, int y);
	Common::Point spritePos(int area, int id);
	void setOrder(int area, int id, int z);
	void setImage(int area, int id, int frame);
	/** 0x430530 on area 1 against slot 1: returns the duration, pos gets the sprite position */
	int nedPlace(int id, bool noMirror, Common::Point &pos);
	/** The 0x487c50 loops: the first solid y at x scanning up from y0; false if none */
	bool floorAt(uint32 mapVA, int x, int y0, int &outY);

	static int16 wallSlot(int i) { return (int16)(kWallFirst + i); }

	// ---- The shared globals ----
	RdxGrid *_grid;          // DAT_00522d5c
	Common::Point _fallEnd;  // DAT_00522dc8 / DAT_00522dcc: slot 1 when the fall ends
	int _wallX[3];           // DAT_005231e8 / DAT_00523200 / DAT_00523208: the wall creatures' x
	int _wallY[3];           // DAT_005231ec / DAT_00523204 / DAT_0052320c
	Common::Point _climb;    // DAT_005231f0 / DAT_005231f4: Ned's climbing sprite
	Common::Point _savedPos; // DAT_00527c20 / DAT_00527c24: the top's Ned on entry
	static int _savedFacing; // DAT_00528084 (sub 4 reads what sub 3 or 5 last saved)
	int _scrollX, _scrollY;  // sub 3, 4: DAT_00522d00 / DAT_00522d04; sub 5: DAT_00528090 / DAT_00528094
	NedHooks _hooks;         // sub 3: DAT_00522cf0; sub 5: DAT_005280b0

private:
	int _soundCbSlot;        // the slot whose end callback is pending (0x455800)
	int _soundCb;
};

/**
 * Sub-scene 3 (m2 0x444040, m3 0x444070, m4 0x4447d0, m5 0x4447f0, m7 0x444ad0): the parlour.
 * Ned drops in (video, the shaft) to an underground parlour (bitmap 5 of scene 7, map 8:1)
 * where a skeleton reads in an armchair (slot 34) beside a lamp creature (431). Zapping the
 * skeleton makes it flinch (35); walking up to it (region 3) gets him swatted back with the
 * book (268). Zapping the lamp (0x1af) lights it (38): the skeleton reaches for it (36, 415)
 * and the lamp hops (40, 41); while that lasts, region 3 lets Ned climb the footstool (42)
 * and the room scrolls away: back to the graveyard.
 */
class Scene1Sub3 : public GraveRoom {
public:
	Scene1Sub3(Game *game, Scene1 *top);
	~Scene1Sub3() override;

	int load(int fromScene, int fromSub) override;     // m2 0x444040
	int enter(int fromScene, int fromSub) override;    // m3 0x444070
	int leave(int toScene, int toSub) override;        // m4 0x4447d0
	int unload(int toScene, int toSub) override;       // m5 0x4447f0
	int tick(uint32 input, uint32 now) override;       // m7 0x444ad0

private:
	enum {
		kList = 0x4a3e28,      // 0xf, scene 7 (callback 0x4414f0)
		kListCount = 0xf,
		kPitList = 0x4a4048,   // 0x16, scene 8 (callback 0x441500)
		kPitCount = 0x16,
		kMapPit = 0x522dbc,
		kBmpPit = 0x4de68c,
		kPalette = 0x522de0
	};
	// The parlour's slots (DAT_00522d80 ...)
	enum {
		kReader = 34, kReaderFlinch = 35, kReaderSwat = 268, kReaderReach = 36, kLamp = 128,
		kLampIdle = 431, kLampHopA = 40, kLampHopB = 41, kStoolClimb = 42, kStool = 149,
		kLampLit = 38, kReaderSpark = 415
	};

	void listCallback(uint32 cb) override;

	int16 lampTick(uint32 now);        // 0x443090
	int16 readerTick(uint32 now);      // 0x443200
	void lampHopAStep(uint32 now);     // 0x443320
	void lampLitPlace();               // 0x443450
	void lampHopBStep(uint32 now);     // 0x443550
	void lampIdleStep(uint32 now);     // 0x4436a0
	void exitStep(uint32 now);         // 0x4437f0
	void exitScroll();                 // 0x442680
	void readerReachStep(uint32 now);  // 0x443950
	void readerFlinchStep(uint32 now); // 0x443c30
	void readerSwatStep(uint32 now);   // 0x443d70

	/** One timed step of id anchored to ref's anchor at refPos; false when not due */
	bool anchoredStep(int id, int ref, const Common::Point &refPos, uint32 now, int *outFrame = nullptr);
	void anchoredPlace(int id, int ref, int frame, const Common::Point &refPos);

	static int hookUse(void *user, int x, int y, int16 *state);                      // 0x444be0
	static int hookRegion(void *user, int32 id, uint32 flags, int x, int y, int16 *state); // 0x444b90
	static void hookFindTarget(void *user, int x, int y, int16 *a, int16 *b, int32 rect[4]); // 0x444c90
	static int hookHitTarget(void *user, int x, int y, int16 *a, int16 *b, const int32 rect[4]); // 0x444c30

	Common::Array<Placement> _sst; // DAT_004bfe70: 99999002.SST
	int _entryX;             // DAT_004bfe80: the top's Ned x on entry
	int _exiting;            // DAT_004bfe74: Ned reached the stool while the lamp was lit
	int16 _unused78;         // DAT_004bfe78
	int _lampLit;            // DAT_004bfe7c
	int _listLoaded;         // DAT_004bfe90
	int _pitLoaded;          // DAT_004bfe94
	int _exitSound;          // DAT_004bfe98
	int16 _lampState;        // DAT_004a3df8: 6 / 7 hopping, 10 lit, 11 idle
	int16 _readerState;      // DAT_00523210: 0 reading, 1 flinch, 2 swat, 3 reaching
	Common::Point _readerPos; // DAT_00522d60 / 64: slot 34's placement
	Common::Point _lampPos;  // DAT_00522d70 / 74: slot 128's placement
	Common::Point _stoolPos; // DAT_00523218 / 1c: slot 149's placement
};

/**
 * Sub-scene 4 (m2 0x444d20, m3 0x444d40, m4 0x445250, m5 0x445260, m7 0x445410): the sofa.
 * A cut-scene: Ned drops in (video, the short fall 0x442290) to a purple room (scene 6) and
 * out of sight; a sofa with hands (slot 466, scene 9) plays its scene with sounds 44 and
 * 45; when it ends Ned climbs out (0x445050) and up the shaft, back to the graveyard. No
 * input is read.
 */
class Scene1Sub4 : public GraveRoom {
public:
	Scene1Sub4(Game *game, Scene1 *top);
	~Scene1Sub4() override;

	int load(int fromScene, int fromSub) override;     // m2 0x444d20
	int enter(int fromScene, int fromSub) override;    // m3 0x444d40
	int leave(int toScene, int toSub) override;        // m4 0x445250
	int unload(int toScene, int toSub) override;       // m5 0x445260
	int tick(uint32 input, uint32 now) override;       // m7 0x445410

private:
	enum {
		kList = 0x4a4360,      // 0xc, scene 6 (callback 0x444cf0)
		kListCount = 0xc,
		kSofaList = 0x4a4510,  // 4, scene 9 (callback 0x444ce0)
		kSofaCount = 4,
		kBmpRoom = 0x4de80c,
		kPalette = 0x5228e0,
		kSofa = 466,
		kSofaSound = 44,
		kSofaSound2 = 45
	};

	void listCallback(uint32 cb) override;
	void fall();                     // 0x442290
	void sofaTick(uint32 now);       // 0x445650
	void sofaStep(uint32 now);       // 0x445430
	void exitScroll();               // 0x445050

	Common::Array<Placement> _sst; // DAT_004bfe9c: 99999004.SST
	int _entryX;             // DAT_004bfea0
	int _sofaLoaded;         // DAT_004bfea4
	int _listLoaded;         // DAT_004bfea8
	int16 _state;            // DAT_004a45a0: 0 start the sofa, 2 playing
	Common::Point _sofaPos;  // DAT_005228d8 / DAT_005228dc
};

/**
 * Sub-scene 5 (m2 0x4012b0, m3 0x401f30, m4 0x4012e0, m5 0x402690, m7 0x401670): the
 * painting. Ned drops in to a sitting room (scene 10) with a painting over a chaise longue,
 * a lamp (slot 46) and a rug (50; region 3: the rug swallows him (51) and he climbs out to
 * the graveyard, 0x401050). Zapping the lamp plays it out, then the painting (bitmap 2, the
 * team photo 6 with TEAM typed) zooms up to fill the screen with FX_SMORPH (0x403430) and
 * nine slots 0x12d.. play in it for 6 s; it zooms back (0x402fa0) and a stone wall (bitmaps
 * 4, 5) rises with a skeleton (0x194) while sounds play; Ned (costume 3) is stuck until the
 * wall comes down. Zapping the skeleton brings it down early; zapping the stove (0x15e)
 * opens region 0x6f, which leads to the attic (scene 4) through the TV clip 8706.
 */
class Scene1Sub5 : public GraveRoom {
public:
	Scene1Sub5(Game *game, Scene1 *top);
	~Scene1Sub5() override;

	int load(int fromScene, int fromSub) override;     // m2 0x4012b0
	int enter(int fromScene, int fromSub) override;    // m3 0x401f30
	int leave(int toScene, int toSub) override;        // m4 0x4012e0
	int unload(int toScene, int toSub) override;       // m5 0x402690
	int tick(uint32 input, uint32 now) override;       // m7 0x401670

private:
	enum {
		kList = 0x497040,      // 0xf, scene 5 (callback 0x401010)
		kListCount = 0xf,
		kRoomList = 0x497260,  // 0x1a, scene 10 (callback 0x401000)
		kRoomCount = 0x1a,
		kTeamEntry = 0x4972a8, // bitmap 2 -> 6 with TEAM (DAT_004972b4)
		kMapRoom = 0x528088,
		kMapRoom2 = 0x527c60,
		kBmpRoom = 0x4de99c,
		kBmpZoom = 0x4de9a0,   // the painting FX_SMORPH zooms
		kBmpWall = 0x4de9a8,
		kBmpWall2 = 0x4de9ac,
		kPalette = 0x527c80,
		kSmorphSteps = 0x1e,
		kRows = 480
	};
	enum {
		kLamp = 46, kHumSound = 48, kRug = 50, kRugFall = 51, kWallTop = 272, kWall = 273,
		kVisionFirst = 301, kVisionCount = 9, kStove = 350, kSkeleton = 404
	};
	enum {
		kCbSoundsStart = 0x401640, // the wall's sound ends: play slot 301's
		kCbSoundsDone = 0x401660   // slot 301's ends: DAT_004ba57c
	};

	void listCallback(uint32 cb) override;
	void climbUp();                  // 0x401050
	void wallAnim(uint32 now);       // 0x4014e0
	void lampTick(uint32 now);       // 0x402840
	void rugTick(uint32 now);        // 0x402970
	void visionTick(uint32 now);     // 0x402ed0
	void zoomOut();                  // 0x402fa0
	void zoomIn();                   // 0x403430
	/** One zoom step: the rows, FX_SMORPH and the dest rect for the painting at w x h */
	void zoomStep(int16 step, int16 w, int16 h);
	void soundEnd(int cb);

	static int hookUse(void *user, int x, int y, int16 *state);                      // 0x401d90
	static int hookRegion(void *user, int32 id, uint32 flags, int x, int y, int16 *state); // 0x401d10
	static void hookFindTarget(void *user, int x, int y, int16 *a, int16 *b, int32 rect[4]); // 0x401ee0
	static int hookHitTarget(void *user, int x, int y, int16 *a, int16 *b, const int32 rect[4]); // 0x401e00

	Common::Array<Placement> _sst5;  // DAT_004ba560: 99999005.SST
	Common::Array<Placement> _sst6;  // DAT_004ba568: 99999006.SST
	int _entryX;             // DAT_004ba56c
	int _risen;              // DAT_004ba578: Ned is in costume 3 behind the wall
	int _soundsDone;         // DAT_004ba57c (0x401660)
	int _lampOn;             // DAT_004ba580
	int _skeletonHit;        // DAT_004ba584
	int _toAttic;            // DAT_004ba588
	int _rugFall;            // DAT_004ba58c
	int _roomLoaded;         // DAT_004ba598
	int _listLoaded;         // DAT_004ba59c
	int _stoveOpen;          // DAT_00525df0
	RdxEffect *_smorph;      // DAT_00525dec
	int32 _smorphMode;       // DAT_00525e10
	int32 _rows[kRows * 4];  // DAT_00525e20: {u0, v0, du, dv} 16.16 per line
	int16 _visionSlot;       // DAT_00525e18
	int16 _unused08;         // DAT_00525e08
	int _unused0c;           // DAT_00525e0c
	Common::Point _wall;     // DAT_00527c28 / DAT_00527c2c: kBmpWall's position
	int _unused34;           // DAT_00527c34
	Common::Point _skeleton0; // DAT_00527c38 / DAT_00527c3c: slot 404's placement
	uint32 _visionEnd;       // DAT_00527c4c: the vision ends (6 s after it starts)
	int _unused58;           // DAT_00527c58
	uint32 _riseTime;        // DAT_00527c5c
	int16 _lampSlot;         // DAT_0052809a (0x2e)
	Common::Point _wall2;    // DAT_005280a0 / DAT_005280a4: kBmpWall2's position
	uint32 _riseSlowTime;    // DAT_005280c0
	int16 _riseState;        // DAT_005280c8: 1 risen, 2 down again
	uint32 _sinkTime;        // DAT_005280cc
	int _hold;               // test hook nightmarened_s1_smorph: the zoom step to stop at
	bool _frozen;
};

} // End of namespace NightmareNed

#endif // NIGHTMARENED_SCENE1_GRAVES_H
