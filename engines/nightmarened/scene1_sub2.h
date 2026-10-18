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

#ifndef NIGHTMARENED_SCENE1_SUB2_H
#define NIGHTMARENED_SCENE1_SUB2_H

#include "common/array.h"
#include "common/rect.h"

#include "nightmarened/scene1_base.h"

namespace NightmareNed {

/**
 * Sub-scene 2 of the graveyard (registered by 0x43c140: m2 0x41e7b0, m3 0x41e7d0,
 * m4 0x420e30, m5 0x420ec0, m7 0x420c80): the zombie cave (NED99000 scene 27). Ned
 * sinks into a cave in a box (video 0x49 plays first); two coffins open and a zombie
 * and his wife climb out and walk at him. Each zap knocks an arm off (it flies to the
 * floor) and a third zap leaves a puff. Once both are armless the ladder regions come
 * on; Ned climbs (state 0xe, then 5) and back up to the graveyard. A zombie that
 * reaches Ned carries him off into its coffin, which shuts: back to the graveyard,
 * both zombies whole again.
 */
class Scene1Sub2 : public GraveyardRoom {
public:
	Scene1Sub2(Game *game, Scene1 *top);
	~Scene1Sub2() override;

	int load(int fromScene, int fromSub) override;     // m2 0x41e7b0
	int enter(int fromScene, int fromSub) override;    // m3 0x41e7d0
	int leave(int toScene, int toSub) override;        // m4 0x420e30
	int unload(int toScene, int toSub) override;       // m5 0x420ec0
	int tick(uint32 input, uint32 now) override;       // m7 0x420c80
	/** 0x41e510 (the top's level setup 0x43e6c0 calls it): the sub-scene's hooks DAT_00525190 */
	void initHooks();

	int _blockVision; // DAT_005248a8: the top's vision trigger 4 waits while it is set

private:
	enum {
		// Request lists (scene 27)
		kVideoList = 0x49cb40,  // area-10 bitmap 1 (scene 99) the video draws into
		kBoxList = 0x49cb68,    // the box Ned arrives in (slot 0x4f)
		kList = 0x49cb90,       // 0x2a: maps, bitmaps, sprites, sounds
		kListCount = 0x2a,
		kLastEntry = 0x49d154,
		// Where the original kept what those lists load
		kVideoBmp = 0x4baef8,
		kMapFloor = 0x52489c,   // cbix 1
		kMapTop = 0x524898,     // cbix 2
		kMapLadder3 = 0x5248b4, // cbix 3..6: added when the ladder opens
		kMapLadder4 = 0x5248b0,
		kMapLadder5 = 0x5248ac,
		kMapLadder6 = 0x5248b8,
		kMapRegions = 0x5251a0, // cbix 7: the ladder regions 1..3
		kBmpCave = 0x4e05bc,    // bitmap 1 (palette 0x5248c0)
		kBmpFront = 0x4e05c0,   // bitmap 2
		kBmpShaft = 0x4e05e4,   // bitmap 11
		kBmp13 = 0x4e05ec,
		kBmp14 = 0x4e05f0,
		kBmp15 = 0x4e05f4,
		kPaletteCave = 0x5248c0,
		// Slots (area 2)
		kBox = 0x4f,
		kMaleWalk = 0x84, kMaleGrab = 0x85, kMaleArm1 = 0x86, kMaleArm2 = 0x87,
		kFemaleWalk = 0x89, kFemaleGrab = 0x8a, kFemaleArm1 = 0x8b, kFemaleArm2 = 0x8c,
		kMale = 0x138, kFemale = 0x139,
		kMaleOneArm = 0x13b, kMaleNoArms = 0x13c, kFemaleOneArm = 0x13d, kFemaleNoArms = 0x13e,
		kFemaleCoffin = 0x192, kMaleCoffin = 0x193, kLid = 0x1bf,
		kArmLand4 = 0x1c0, kArmLand3 = 0x1c1, kArmLand2 = 0x1c2, kArmLand1 = 0x1c3,
		// Area 1 (Ned carried off)
		kCarryMale = 0x1a0, kCarryFemale = 0x53
	};

	/** One zombie's slots and how it differs from the other (0x41fa40 male / 0x41f420 female) */
	struct Zombie {
		int16 ref, walk, grab, emerge, oneArm, noArms;
		int dx, grabStep, carryStep, carrySlot;
	};

	static void xform(void *user, int &x, int &y);                                 // 0x41e490
	static int hookUse(void *user, int x, int y, int16 *state);                    // 0x421210
	static int hookRegion(void *user, int32 id, uint32 flags, int x, int y, int16 *state); // 0x421200
	static void hookFindTarget(void *user, int x, int y, int16 *a, int16 *b, int32 rect[4]); // 0x421830
	static int hookHitTarget(void *user, int x, int y, int16 *a, int16 *b, const int32 rect[4]); // 0x421320

	int findTargetRect(int16 &dir, int16 &found, int32 rect[4]);  // 0x421020
	int hitTarget(int16 found);                                   // 0x421320
	int arrival();                                                // 0x41e0b0
	bool sink();                                                  // 0x41e2c0
	void applyPlacements(bool sprites);                           // 0x4870a0 with 0x41e550
	void zombieTick(bool male, uint32 now);                       // 0x41fa40 / 0x41f420
	void openLadder();                                            // inside 0x41fa40 / 0x41f420
	void armTick(int16 &cur, int16 arm, int16 land, int32 *arc, int16 soundSlot, uint32 now); // 0x420280 ...
	void arcStart(int32 *arc, int16 from, int16 to);              // 0x421260
	void carryTick(int16 &cur, int dx, bool male, uint32 now);    // 0x420060 / 0x420170
	void maleCoffinTick(uint32 now);                              // 0x420940
	void femaleCoffinTick(uint32 now);                            // 0x420b00
	void climbScroll(uint32 now);                                 // 0x421870
	void adjBitmaps(int dy);
	/** 0x4558e0: plays a slot's sound looped */
	void loopSound(int id);
	void testHook();
	void staticPalette();

	NedHooks _hooks;          // DAT_00525190..0x52519c
	Common::Array<Placement> _sst; // DAT_004baf24: 99999007.SST
	uint32 _last;             // DAT_004baefc
	int _facing;              // DAT_004baef4: Ned's facing when he came in
	Common::Point _topPos;    // DAT_004baf28 / DAT_004baf2c: the graveyard's Ned position
	int _boxSavedX, _boxSavedY; // DAT_004baf08 / DAT_004baf0c: slot 0x4f's x, y on entry
	int _climbed;             // DAT_004baf00: Ned reached the ladder's climb (state 5)
	int _failed;              // DAT_004baf18: a coffin shut on Ned
	int _maleCarried;         // DAT_004baf4c: Ned was carried off left, the male's coffin shuts
	int _femaleCarried;       // DAT_004baf58: carried off right, the female's coffin shuts
	int _maleOpen;            // DAT_004baee8: the male's coffin opens
	int _femaleOpen;          // DAT_004baf50
	int _ladderOpen;          // DAT_00524880
	int _femaleHp;            // DAT_004baef0: arms left
	int _maleHp;              // DAT_004baf1c
	Common::Point _scroll;    // DAT_005248a0 / DAT_005248a4
	Common::Point _nedPos;    // DAT_005251b0 / DAT_005251b4
	int _boxX;                // DAT_00524888
	int _boxY;                // DAT_0052488c
	int _boxStop;             // DAT_005251a8: where the box stops sinking with Ned
	uint32 _scrollTime;       // DAT_004baf60
	int16 _box;               // DAT_004baeec
	int16 _maleCoffin;        // DAT_004baf14
	int16 _femaleCoffin;      // DAT_004baf20
	int16 _lid;               // DAT_004baf30
	int16 _male;              // DAT_004baf38
	int16 _female;            // DAT_004baf54
	int16 _maleArm1;          // DAT_004baf48
	int16 _maleArm2;          // DAT_004baf10
	int16 _femaleArm1;        // DAT_004baf44
	int16 _femaleArm2;        // DAT_004baf34
	int16 _carryFemale;       // DAT_004baf3c (area 1)
	int16 _carryMale;         // DAT_004baf40 (area 1)
	// The arms' flights (0x24 bytes each): from x, y, to x, y, dist, dy, steps, height, step
	int32 _arcMale1[9];       // DAT_00524d28
	int32 _arcMale2[9];       // DAT_00524d4c
	int32 _arcFemale1[9];     // DAT_00524ce0
	int32 _arcFemale2[9];     // DAT_00524d04
	int32 _regionIds[2];      // DAT_00524890 / DAT_00524894
	bool _testDone;
};

} // End of namespace NightmareNed

#endif // NIGHTMARENED_SCENE1_SUB2_H
