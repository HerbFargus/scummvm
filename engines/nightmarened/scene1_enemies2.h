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

#ifndef NIGHTMARENED_SCENE1_ENEMIES2_H
#define NIGHTMARENED_SCENE1_ENEMIES2_H

#include "common/scummsys.h"

#include "nightmarened/resources.h"

namespace NightmareNed {

class Scene1;

/**
 * Graveyard creatures, part 2: the Reaper (record 0), the Tree (1), the Zombie (3),
 * the Ghoul (4), the Goblin (6), the Swamp Creature (8), the Statue (9) and record 10.
 */
class Scene1Enemies2 {
public:
	explicit Scene1Enemies2(Scene1 *scene);
	~Scene1Enemies2();

	/** The 0xffff resets 0x43c880 makes in this module's globals */
	void reset();
	/** 0x47ad70: the Reaper's state from scratch (also when a grave sub-scene is left) */
	void reaperReset();
	/** 0x484f70: the Ghoul's sprite (sub-scene 7 hides it behind its videos) */
	void ghoulVisible(bool on);

	// The records' show (+0x20) and hide (+0x24) callbacks
	void reaperShow();   // 0x43b170
	void reaperHide();   // 0x47ad10
	void treeShow();     // 0x43b0f0
	void treeHide();     // 0x467700
	void zombieShow();   // 0x4646d0
	void zombieHide();   // 0x464980
	void ghoulShow();    // 0x484b10
	void ghoulHide();    // 0x484bd0
	void goblinShow();   // 0x44e660
	void goblinHide();   // 0x44e7a0
	void swampShow();    // 0x428610
	void swampHide();    // 0x428670
	void statueShow();   // 0x43b360
	void statueHide();   // 0x477f30
	void record10Show(); // 0x477c40
	void record10Hide(); // 0x477ca0

	// Per tick, from 0x43d4d0 (x = the tile * 9)
	void statueTick(uint32 now, int16 x);  // 0x478590
	void ghoulTick(uint32 now);            // 0x484ee0
	void reaperTick(uint32 now);           // 0x47c590
	void zombieTick(uint32 now);           // 0x464fb0
	void record10Tick(uint32 now);         // 0x477cf0
	void treeTick(uint32 now, int16 x);    // 0x467930
	void goblinTick(uint32 now);           // 0x44e910
	void swampTick(uint32 now);            // 0x4286c0

	// The camera moved everything by (dx, dy) (0x43d8a0 / 0x43da40)
	void zombieScroll(int dx, int dy);    // 0x464f40
	void reaperScroll(int dx, int dy);    // 0x47b150
	void statueScroll(int dx, int dy);    // 0x4787d0
	void ghoulScroll(int dx, int dy);     // 0x484f00
	void swampScroll(int dx, int dy);     // 0x428690
	void goblinScroll(int dx, int dy);    // 0x44e7d0
	void record10Scroll(int dx, int dy);  // 0x477cc0

	// Ned's hooks
	/** The slot's world position in pos when it is the creature's; nonzero if so */
	int statueRect(int16 found, int32 pos[2]);    // 0x4787f0
	int treeRect(int16 found, int32 pos[2]);      // 0x467ae0
	int reaperRect(int16 found, int32 pos[2]);    // 0x47c540
	int swampRect(int16 found, int32 pos[2]);     // 0x4288a0
	int record10Rect(int16 found, int32 pos[2]);  // 0x477ee0
	// (x, y, a, b, rect) of NedHooks::hitTarget; nonzero takes the hit
	int statueHit(int x, int y, int16 *a, int16 *b, const int32 rect[4]);    // 0x4785c0
	int treeHit(int x, int y, int16 *a, int16 *b, const int32 rect[4]);      // 0x467a50
	int reaperHit(int x, int y, int16 *a, int16 *b, const int32 rect[4]);    // 0x47c3e0
	int swampHit(int x, int y, int16 *a, int16 *b, const int32 rect[4]);     // 0x428840
	int record10Hit(int x, int y, int16 *a, int16 *b, const int32 rect[4]);  // 0x477e60
	// (id, flags, x, y, state) of NedHooks::region; nonzero claims the region
	int statueRegion(int32 id, uint32 flags, int x, int y, int16 *state);    // 0x478830
	int treeRegion(int32 id, uint32 flags, int x, int y, int16 *state);      // 0x467960
	int zombieRegion(int32 id, uint32 flags, int x, int y, int16 *state);    // 0x465250
	int ghoulRegion(int32 id, uint32 flags, int x, int y, int16 *state);     // 0x484fa0
	int reaperRegion(int32 id, uint32 flags, int x, int y, int16 *state);    // 0x47b1c0

private:
	Scene1 *_s;

	// Shared steps
	Slot &sl(int id);
	/** 0x430850 with xformPlain (0x43cb30); returns the step's duration */
	int placeP(int id, int ref, bool noMirror, int *frame = nullptr);
	void setDeadline(int id, uint32 now, int duration);
	void setZ(int id, int z);
	/** 0x488060 on the exit map (DAT_004bf91c) */
	void exits(int32 id, bool on);
	/** The user-map region follows the slot's hit box (box 0, or 3 when mirrored) */
	void regionFollow(int32 region, int id, int ref, int frame, bool noMirror);
	void rectAt(int id, int ref, int32 pos[2], bool noMirror);
	void playAction(int action);

	// The Reaper
	void reaperFlip(int on);           // 0x47af60
	void reaperStep(uint32 now);       // 0x47b690
	int16 _reaperSlot;   // DAT_004b5278: the body, against slot 5
	int16 _reaperArm;    // DAT_004b527c: the left arm, against 0x105
	int16 _reaperArm2;   // DAT_004b5280: the right arm, against 0x107
	int32 _reaperSave;   // DAT_004b5284: the anchors are saved once
	int32 _reaperRiseL;  // DAT_004b5288
	int32 _reaperRiseR;  // DAT_004b528c
	int16 _reaperLoops1; // DAT_00520618
	int16 _reaperLoops2; // DAT_0052061c
	int32 _reaperSide;   // DAT_00520620: which arm swings next
	int32 _reaperBeaten; // DAT_00520614
	int32 _reaperPos[6]; // DAT_00522888..0052289c: slots 0x105, 0x107 and 5 (x, y)
	int32 _reaperMirror; // DAT_005228a0
	int16 _reaperLeave;  // DAT_005228a4
	uint32 _reaperTimeL; // DAT_005228a8
	uint32 _reaperTimeR; // DAT_005228ac
	int32 _reaperRegion; // DAT_005228b0

	// The Tree
	void treeStep(uint32 now);         // 0x467710
	int16 _treeSlot;     // DAT_004b1fd4
	int16 _treeX;        // DAT_005228d2

	// The Zombie
	void zombieBody(uint32 now);       // 0x4649b0
	void zombieArm(uint32 now);        // 0x464b40
	void zombieHead(uint32 now);       // 0x464dc0
	int16 _zombieBase;   // DAT_005228d4: slot 8
	int16 _zombieBodySlot; // DAT_004b12fc, against 0x1a0
	int16 _zombieArmSlot;  // DAT_004b1300, against 0x1a1
	int16 _zombieHeadSlot; // DAT_004b1304, against 0x1a2
	int16 _zombieSpare;    // DAT_004b1308 (only reset by 0x43c880)
	uint32 _zombieThrown;  // DAT_004b130c
	uint32 _zombieNext;    // DAT_004b1310

	// The Ghoul
	void ghoulStep(uint32 now);        // 0x484c00
	int16 _ghoulSlot;    // DAT_004b6cdc
	uint32 _ghoulSound;  // DAT_0052287c
	int32 _ghoulRegion;  // DAT_00522880
	int32 _ghoulEat;     // DAT_005221b4

	// The Goblin
	void goblinStep(uint32 now, int16 id, int ref); // 0x44e840
	void goblinLoop();                 // 0x4558e0 on slot 0x82
	int16 _goblin[4];    // DAT_004a5534 (0x82), DAT_004a5538 (0x191), DAT_004a553c (0x65), DAT_004a5540 (0x1e5)

	// The Swamp Creature
	int16 _swampSlot;    // DAT_0049e228
	int32 _swampUp;      // DAT_00524878

	// The Statue
	void statueInit();                 // 0x477f10
	void statueStep(uint32 now);       // 0x477f40
	bool statueSees(bool noMirror);
	int16 _statueSlot;   // DAT_004b4484
	uint32 _statueNext;  // DAT_005228b4
	int32 _statueRegion; // DAT_005228b8
	int32 _statueAwake;  // DAT_005228bc

	// Record 10
	int16 _rec10Slot;    // DAT_004b4480
	int32 _rec10Up;      // DAT_005228c8
	int32 _rec10NedX;    // DAT_005228c0
	int32 _rec10NedY;    // DAT_005228c4
};

} // End of namespace NightmareNed

#endif // NIGHTMARENED_SCENE1_ENEMIES2_H
