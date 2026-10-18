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

#ifndef NIGHTMARENED_SCENE1_ENEMIES1_H
#define NIGHTMARENED_SCENE1_ENEMIES1_H

#include "common/scummsys.h"

#include "nightmarened/data.h"

namespace NightmareNed {

class Scene1;
struct Slot;

/**
 * Graveyard creatures, part 1: the Pumpkin (record 2), the Mourners (record 5) and
 * the Coffin (record 7).
 */
class Scene1Enemies1 {
public:
	explicit Scene1Enemies1(Scene1 *scene);
	~Scene1Enemies1();

	/** The 0xffff resets 0x43c880 makes in this module's globals */
	void reset();
	/** 0x438870, from the level setup 0x43e6c0 */
	void init();

	// The records' show (+0x20) and hide (+0x24) callbacks
	void pumpkinShow();      // 0x47ce00
	void pumpkinHide();      // 0x47d180 (0x43c880 calls it too)
	void mournersShow();     // 0x43b440
	void mournersHide();     // 0x4388a0
	void coffinShow();       // 0x43a9c0
	void coffinHide();       // 0x43ab50

	// Per tick, from 0x43d4d0
	void coffinTick(uint32 now);    // 0x43ab80
	void mournersTick(uint32 now);  // 0x43a900
	void pumpkinTick(uint32 now);   // 0x47d700
	/** 0x47d7e0: more than three pumpkins done (DAT_004bf854 > 3) */
	bool pumpkinsDone() const;

	// The camera moved everything by (dx, dy) (0x43d8a0 / 0x43da40)
	void pumpkinScroll(int dx, int dy);   // 0x434b70
	void pumpkinsPlace();                 // 0x47c700
	void mournersScroll(int dx, int dy);  // 0x43a500
	void coffinScroll(int dx, int dy);    // 0x43ad90

	// Ned's hooks
	/** 0x438580: the slot's world position in pos when it is the pumpkin's; nonzero if so */
	int pumpkinRect(int16 found, int32 pos[2]);
	/** 0x439590: likewise; noFlip = 0 makes 0x43ba30 use box 3 */
	int mournersRect(int16 found, int32 pos[2], int32 *noFlip);
	/** 0x4387a0: after the use hook's target search (needs the found slot: use the overload) */
	void pumpkinUse();
	/** 0x4387a0 with the found slot the use hook 0x43bf10 passes as its 4th argument */
	void pumpkinUse(int16 found);
	// (x, y, a, b, rect) of NedHooks::hitTarget; nonzero takes the hit
	int pumpkinHit(int x, int y, int16 *a, int16 *b, const int32 rect[4]);   // 0x437bf0
	int mournersHit(int x, int y, int16 *a, int16 *b, const int32 rect[4]);  // 0x4392f0
	// (id, flags, x, y, state) of NedHooks::region; nonzero claims the region
	int coffinRegion(int32 id, uint32 flags, int x, int y, int16 *state);    // 0x43add0
	int pumpkinRegion(int32 id, uint32 flags, int x, int y, int16 *state);   // 0x437b90
	int mournersRegion(int32 id, uint32 flags, int x, int y, int16 *state);  // 0x439050

private:
	Scene1 *_s;

	Slot &sl(int id);
	uint32 now() const;
	int16 sw(int newSlot, int oldSlot);
	/** 0x430850 against ref with the plain xform; returns the duration */
	int put(int id, int ref, bool noMirror, int *frame = nullptr);
	/** 0x430710 with the relative flag */
	int putRel(int id, int ref, bool noMirror, int *frame = nullptr);
	void setDeadline(int id, uint32 t, int duration);
	int screenX(int id);
	/** objApplyEffect(FX_HORIZ_FLIP) into the slot's +0x38 (a new, active effect) */
	void applyFlip(int id);
	/** z field and draw order */
	void setZ(int id, int z);
	/** objSetDrawOrder only */
	void drawOrder(int id, int z);
	/** 0x4558e0: the slot's sound, looping */
	void loopSound(int id);
	/** 0x487850: moves one region of the scene's region map */
	void regionMove(int32 id, int dx, int dy);
	/** Ned's box (facing right 0, left 3) on his sprite, screen coordinates */
	void nedSpriteBox(int32 out[4]);
	/** A slot's box k at its current frame on its sprite, screen coordinates */
	void spriteBox(int id, int frame, int k, int32 out[4]);
	void playNed(int action, int sx = 0, int sy = 0);

	// The Pumpkin
	void pumpkinBitmap(int sub);            // 0x47c6a0
	void pumpkinsPlace2();                  // 0x47c980 (no caller)
	void pumpkinApply(const Placement &p);  // 0x47cc00
	void pumpkinBitmapShow(int sub);        // 0x47cd80 (no caller)
	void pumpkinRise(uint32 now);           // 0x47d330
	void pumpkinRegions(int dx, int dy);    // 0x434a60
	bool bigArtShow();                      // 0x434de0
	bool bigShow();                         // 0x435220
	bool pair4Show();                       // 0x4354c0
	bool pair3Show();                       // 0x435690
	bool pair2Show();                       // 0x435860
	bool pair1Show();                       // 0x435a30
	bool pairShow(int &loaded, int &shown, int on, uint32 list, int a, int b, int flipSlot,
		int16 &slotA, int16 &slotB, int32 &regionA, int32 &regionB);
	void crowTick(int16 id, uint32 now);    // 0x435cb0
	void faceTick();                        // 0x435dd0
	void seedTick(uint32 now, int16 &slot, int dx, int amp); // 0x435f60
	void shotTick(uint32 now);              // 0x436100
	void bigTick(uint32 now);               // 0x4364d0
	void pairATick(uint32 now, int16 &slot, int ref, int32 &region); // 0x436c80
	void pairBTick(uint32 now, int16 &slot, int ref, int32 &region); // 0x4370f0
	void pairRegion(int16 slot, int ref, bool noMirror, int frame, int32 region);
	void nearest();                         // 0x4374f0
	void pumpkinUpdate(uint32 now);         // 0x4377a0
	void pairHit(int16 &slotA, int16 &slotB, int a, int b, bool aFirst, int32 &regionA, int32 &regionB);

	// The Mourners
	void mournersStart();                   // 0x43a5a0
	void bellTick(uint32 now);              // 0x438c20
	void idleTick(uint32 now);              // 0x439600
	void mournTick(uint32 now);             // 0x4397e0

	// ---- The Pumpkin (record 2) ----
	int16 _bigSlot;          // DAT_004bf880: the big pumpkin, 0x125..0x12a, 299
	uint32 _bigTimer;        // DAT_004bf830
	int _hits;               // DAT_004bf854
	int _pair1On;            // DAT_004bf858
	int _pair2On;            // DAT_004bf85c
	int _pair3On;            // DAT_004bf860
	int _pair4On;            // DAT_004bf864
	int16 _p1a, _p1b;        // DAT_004bf8b0 / DAT_004bf898: 0x51 / 0x52
	int16 _p2a, _p2b;        // DAT_004bf8a8 / DAT_004bf8a0: 0x55 / 0x56
	int16 _p3a, _p3b;        // DAT_004bf8ac / DAT_004bf89c: 0x59 / 0x5a
	int16 _p4a, _p4b;        // DAT_004bf8b8 / DAT_004bf8a4: 0x5d / 0x5e
	int32 _r1a, _r1b;        // DAT_004bf83c / DAT_004bf8c0
	int32 _r2a, _r2b;        // DAT_004bf844 / DAT_004bf8c4
	int32 _r3a, _r3b;        // DAT_004bf840 / DAT_004bf8c8
	int32 _r4a, _r4b;        // DAT_004bf848 / DAT_004bf8cc
	int32 _bigRegion;        // DAT_004bf824
	uint32 _t1, _t2, _t3, _t4; // DAT_004bf888 / DAT_004bf88c / DAT_004bf890 / DAT_004bf894
	int _nearA;              // DAT_004bf838
	int _nearB;              // DAT_004bf850
	int _crow;               // DAT_004bf884: 0x61 or 99, the one that animates
	int32 _biteRect[4];      // DAT_004bf8d0..dc: region 6000, grown by 1
	int32 _spitRect[4];      // DAT_004bf870..7c: region 6001
	int16 _seed1;            // DAT_004a1c60
	int16 _seed2;            // DAT_004a1c64
	int16 _face;             // DAT_004a1c68
	int16 _shot;             // DAT_004a1c6c
	int _amp1;               // DAT_004bf8b4
	int _amp2;               // DAT_004bf834
	int _shotX;              // DAT_004bf818
	int _shotY;              // DAT_004bf81c
	uint32 _shotHold;        // DAT_004bf820
	int _shotDrop;           // DAT_004bf828
	int _shotVy;             // DAT_004bf82c
	int _shotDecay;          // DAT_004bf84c
	int _shotFloor;          // DAT_004bf868
	int _shotMax;            // DAT_004bf86c
	int _expLoaded, _expShown;     // DAT_00520658 / DAT_005206b8 (0x122, 0x1be)
	int _p1Loaded, _p1Shown;       // DAT_005206a0 / DAT_0052064c
	int _p2Loaded, _p2Shown;       // DAT_00520684 / DAT_0052069c
	int _p3Loaded, _p3Shown;       // DAT_00520698 / DAT_00520628
	int _p4Loaded, _p4Shown;       // DAT_00520640 / DAT_00520668
	int _crowALoaded, _crowAShown; // DAT_00520680 / DAT_005206ac (0x61)
	int _crowBLoaded, _crowBShown; // DAT_0052067c / DAT_0052062c (99)
	int _bigLoaded, _bigShown;     // DAT_00520644 / DAT_005206bc (0x125)
	int _artLoaded, _artShown;     // DAT_00520648 / DAT_005206c0 (0x126..)
	int _patchAsked;         // DAT_005206b0
	uint32 _riseTime;        // DAT_005206c4
	int _x94, _x90, _x85, _x82; // DAT_0052068c / DAT_00520688 / DAT_005206b4 / DAT_00520690
	int _mapDx, _mapDy;      // DAT_00520670 / DAT_00520674
	int32 _unk660, _unk664;  // DAT_00520660 / DAT_00520664 (written only)

	// ---- The Mourners (record 5) ----
	int16 _right;            // DAT_004a1c70: 0x1b8, 0x1b9, 0x1bb, 0x1bc, 0x136, 0x137
	int16 _rightIdle;        // DAT_004a1c74: 0x1ba
	int16 _left;             // DAT_004a1c78: 0x11b, 0x47..0x4a, 0x136, 0x137
	int16 _leftIdle;         // DAT_004a1c7c: 0x46
	int32 _mirror;           // DAT_004a1c80: 0 = Ned is right of them
	int _caught;             // DAT_00524854
	int _unk84c;             // DAT_0052484c (written only)
	int _rightHit;           // DAT_004bf8e0
	int _leftHit;            // DAT_004bf8e4
	int _unk8e8;             // DAT_004bf8e8 (written only)
	int16 _leftX;            // DAT_00524860
	int16 _rightX;           // DAT_0052486c
	int16 _rightNext;        // DAT_00524868
	int16 _leftNext;         // DAT_0052486e
	int32 _region;           // DAT_00524870: the pit (0x4a)
	int16 _leftCount;        // DAT_00524850
	int16 _rightCount;       // DAT_0052486a
	int32 _bellX, _bellY;    // DAT_00524858 / DAT_0052485c (written only)

	// ---- The Coffin (record 7) ----
	int16 _coffinX;          // DAT_00524844
	uint32 _coffinTime;      // DAT_004a1c8c (written only)
};

} // End of namespace NightmareNed

#endif // NIGHTMARENED_SCENE1_ENEMIES1_H
