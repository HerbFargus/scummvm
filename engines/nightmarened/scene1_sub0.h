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

#ifndef NIGHTMARENED_SCENE1_SUB0_H
#define NIGHTMARENED_SCENE1_SUB0_H

#include "common/array.h"
#include "common/rect.h"

#include "nightmarened/scene1_base.h"

namespace NightmareNed {

/**
 * Sub-scene 0, the crypt (registered by 0x43c140: m2 0x480240, m3 0x480260, m4 0x480770,
 * m5 0x480a70, m7 0x480a90; NED99000 scene 14). A single fixed room entered through the
 * transition video file 0x56. Ned zaps the two things at the sides (slots 0x36 and
 * 0x38) and the thing in the middle (0x3f); once both sides are done the middle one
 * opens (0x43, 0x1b4) and the floor map 2 joins the world. Walking into region 3 then
 * takes Ned's input away, something rises (0x3c, 0x3d) and the hands (0x44) carry him
 * off (action 0x1b): back to the graveyard. On a revisit the hands (0x119) take him at once.
 */
class Scene1Sub0 : public GraveyardRoom {
public:
	Scene1Sub0(Game *game, Scene1 *top);
	~Scene1Sub0() override;

	int load(int fromScene, int fromSub) override;     // m2 0x480240
	int enter(int fromScene, int fromSub) override;    // m3 0x480260
	int leave(int toScene, int toSub) override;        // m4 0x480770
	int unload(int toScene, int toSub) override;       // m5 0x480a70
	int tick(uint32 input, uint32 now) override;       // m7 0x480a90

	int _revisit;  // DAT_00521808: set when a saved game had sub-scene 0 done (0x43eae0)

private:
	enum {
		kVideoList = 0x4b5d98,   // 1: the transition video's bitmap (scene 99)
		kMainList = 0x4b5dc0,    // 0x2a, scene 14
		kMainCount = 0x2a,
		kMainScene = 0xe,
		kListDone = 0x47ffc0,    // the main list's callback
		kMapFloor = 0x5213d0,    // cbix 1
		kMapOpen = 0x520fc4,     // cbix 2: the floor once the middle thing has opened
		kBmpRoom = 0x4ddd1c,     // bitmap 1 (palette kPaletteRoom)
		kBmpVideo = 0x520fac,
		kPaletteRoom = 0x520fc8,
		kVideoFile = 0x56,
		kNedList = 4
	};

	void listCallback(uint32 cb) override;

	void playVideo();                          // 0x481b40
	void setupSprites();                       // 0x47ffe0
	void placeSst(const Placement &p);         // 0x481a20
	void hideSprites();                        // the hides of 0x480770
	void fadeTick(uint32 now);                 // 0x480b50
	void flashTick(uint32 now);                // 0x480c10
	void riseTick(uint32 now);                 // 0x480d80
	void leftTick(uint32 now);                 // 0x480f50
	void rightTick(uint32 now);                // 0x4810b0
	void grabTick(uint32 now);                 // 0x4811f0
	void gateTick(uint32 now);                 // 0x481420
	/** 0x4558e0: the slot's sound, looped */
	void loopSound(int id);
	int findTarget(int16 &dir, int16 &found, int32 rect[4]);  // 0x481930

	// Hooks (DAT_00520f98)
	static int hookUse(void *user, int x, int y, int16 *state);                       // 0x481710
	static int hookRegion(void *user, int32 id, uint32 flags, int x, int y, int16 *state); // 0x4816f0
	static void hookFindTarget(void *user, int x, int y, int16 *a, int16 *b, int32 rect[4]); // 0x4818f0
	static int hookHitTarget(void *user, int x, int y, int16 *a, int16 *b, const int32 rect[4]); // 0x481760

	NedHooks _hooks;          // DAT_00520f98
	int16 _hookTag;           // DAT_00520f94 (10; nothing reads it)
	Common::Point _nedPos;    // DAT_00525df8 / DAT_00525dfc
	int _scrollX, _scrollY;   // DAT_00521828 / DAT_0052182c (never set)
	Common::Array<Placement> _sst; // DAT_005213e8: 99999008.SST
	byte _palette[256 * 3];   // DAT_00520fc8: the room's palette with Ned's colours
	byte _fadePal[256 * 3];   // DAT_005213f0
	int16 _fadeLevel;         // DAT_00520f90: 1..10 tenths
	uint32 _fadeTime;         // DAT_005213ec
	int _savedX;              // DAT_005217f8: the graveyard's Ned x on entry
	int _savedFacing;         // DAT_00521804
	int _from;                // DAT_00520fb4: the scene entered from
	int _listLoaded;          // DAT_00521820
	int _exit;                // DAT_00520fb0
	int _stage;               // DAT_0052181c: region 3 counts it up; nonzero takes Ned's input
	int _zapped;              // DAT_00521818: 1 = the right thing, 2 = the left thing
	int16 _flashSlot;         // DAT_00520fa8: 0x45, every 10 s
	uint32 _flashTime;        // DAT_005217fc
	int16 _leftSlot;          // DAT_00520fc0: 0x36, zapped 0x37
	int16 _rightSlot;         // DAT_005217f4: 0x38, 0x39, zapped 0x3a
	int16 _gateSlot;          // DAT_00520fb8: 0x3f and its reactions
	int16 _gateNextA;         // DAT_005213dc: what the middle plays after the left thing
	int16 _gateNextB;         // DAT_005213d8: likewise after the right thing
	int16 _lidSlot;           // DAT_005213e4: 0xad, 0xae
	int16 _riseSlot;          // DAT_00521800: 0x3b, 0x3c, 0x3d
	int16 _grabSlot;          // DAT_005213cc: 0x44, 0x11a, 0x1b7 (0x119 on a revisit)
	int _unused5213d4;        // DAT_005213d4 (cleared by m4 only)
	bool _testHookRead;
};

} // End of namespace NightmareNed

#endif // NIGHTMARENED_SCENE1_SUB0_H
