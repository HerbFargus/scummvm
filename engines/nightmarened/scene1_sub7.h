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

#ifndef NIGHTMARENED_SCENE1_SUB7_H
#define NIGHTMARENED_SCENE1_SUB7_H

#include "common/array.h"
#include "common/rect.h"

#include "nightmarened/scene1_base.h"

namespace NightmareNed {

/**
 * Sub-scene 7 (registered by 0x43c140: m2 0x447610, m3 0x447630, m4 0x448770, m5 0x448820,
 * m7 0x448880; NED99000 scene 23). A dim room (the palette at 3/4) entered through the
 * video file 0x4e and left through file 0x4f by walking off the left edge (x < 11),
 * which region 1 of map 1 blocks at first. Ned zaps the thing at the door (0x67, which
 * lets map 2 in), the figure (0x70, four reactions in turn) and the thing on the right
 * (0x6a): zapped from its right side it knocks 0x6d over (0x6e), which drops 0x1e3.
 * When that lands the figure walks its path (table 0x4a4968, Ned's input off), the
 * lights come up, 0x6d rolls out to the left (0x6f, 0x1c7), region 1 opens and 0x195
 * plays. A revisit starts with all of that done.
 */
class Scene1Sub7 : public GraveyardRoom {
public:
	Scene1Sub7(Game *game, Scene1 *top);
	~Scene1Sub7() override;

	int load(int fromScene, int fromSub) override;     // m2 0x447610
	int enter(int fromScene, int fromSub) override;    // m3 0x447630
	int leave(int toScene, int toSub) override;        // m4 0x448770
	int unload(int toScene, int toSub) override;       // m5 0x448820
	int tick(uint32 input, uint32 now) override;       // m7 0x448880

	int _revisit;  // DAT_004c3ba4: set when a saved game had sub-scene 7 done (0x43eae0)

private:
	enum {
		kVideoList = 0x4a4a38,   // 1: the transition videos' bitmap (scene 99)
		kMainList = 0x4a4a60,    // 0x2f, scene 23
		kMainCount = 0x2f,
		kMainScene = 0x17,
		kMapFloor = 0x4c3ba8,    // cbix 1
		kMapDoor = 0x4c4000,     // cbix 2: joins the world when 0x67 is zapped
		kBmpBack = 0x4dff7c,     // bitmap 1 (palette kPaletteRoom)
		kBmp2 = 0x4dff80,
		kBmp3 = 0x4dff84,
		kBmpVideo = 0x4c3bb0,
		kPaletteRoom = 0x4c4018,
		kVideoIn = 0x4e,
		kVideoOut = 0x4f,
		kPathSteps = 17,         // 0x4a4968, 12 bytes each
		kReactions = 5           // 0x4a4958
	};

	/** A record of the figure's path (0x4a4968) */
	struct PathStep {
		int32 flip;     // the flip effect is active
		int16 left;     // pixels still to walk (counts down by dx; the original's table changes)
		int16 dx;
		int16 slot;
	};

	void videoIn();                            // 0x449c40
	void videoOut();                           // 0x449dd0
	void playVideo(int fileIndex);             // the frame loop of both
	/** 0x484f70 */
	void ghoulVisible(bool on);
	/** 0x446f80: every colour times f */
	static void scalePalette(byte *pal, double f);
	/** 0x43e460 (olist_foreach callback), with the bitmap entries (kinds 0 and 1) */
	void applyPlacementRoom(const Placement &p);
	void showSprite(int id, uint32 z);
	void coverTick(uint32 now);                // 0x448a40
	void dropTick(uint32 now);                 // 0x448bc0
	void boxTick(uint32 now);                  // 0x448d40
	void shelfTick(uint32 now);                // 0x449000
	void figureTick(uint32 now);               // 0x449220
	/** 0x4558e0: the slot's sound, looped */
	void loopSound(int id);
	int findTarget(int16 &dir, int16 &found, int32 rect[4]);  // 0x449900

	// Hooks (DAT_004c3fc8)
	static int hookUse(void *user, int x, int y, int16 *state);                       // 0x449a00
	static int hookRegion(void *user, int32 id, uint32 flags, int x, int y, int16 *state); // 0x4499f0
	static void hookFindTarget(void *user, int x, int y, int16 *a, int16 *b, int32 rect[4]); // 0x449c00
	static int hookHitTarget(void *user, int x, int y, int16 *a, int16 *b, const int32 rect[4]); // 0x449a50

	NedHooks _hooks;          // DAT_004c3fc8
	Common::Point _nedPos;    // DAT_00525df8 / DAT_00525dfc
	int _scrollX, _scrollY;   // DAT_004c4428 / DAT_004c442c (never set)
	Common::Array<Placement> _sst; // DAT_004c3bac: 99999003.SST
	byte _paletteRoom[256 * 3]; // DAT_004c4018: bitmap 1's palette with Ned's colours
	byte _palette[256 * 3];   // DAT_004c3bb8: what is shown (dimmed until the lights come up)
	PathStep _path[kPathSteps];
	int16 _reactions[kReactions]; // DAT_004a4958: 0x13f, 0x72, 0x141, 0x142, 0
	int16 _bounce;            // DAT_004a4a34: 0x6d's drop per step while it rolls (divided by 3)
	uint32 _entryZ;           // DAT_004c3fe0 / DAT_004c3ff0: slot 0x70's z on entry (unread)
	int16 _coverSlot;         // DAT_004c3fe8: 0x67, zapped 0x68
	int16 _boxSlot;           // DAT_004c441c: 0x6d, 0x6e, 0x6f, 0x1c7
	int16 _shelfSlot;         // DAT_004c3ff8: 0x6a, 0x6b, 0x6c
	int16 _figureSlot;        // DAT_004c3fc0: 0x70, 0x71, the reactions, the path
	int16 _dropSlot;          // DAT_004c400c: 0x1e3 falling, 0x1e4
	int16 _doorSlot;          // DAT_004c3fbc: 0x195, holds its last step
	int _knocked;             // DAT_004c3b94
	int _reactPending;        // DAT_004c3b90
	int _reacted;             // DAT_004c3ffc (unread)
	uint32 _idleTime;         // DAT_004c3fe4: when 0x70 next turns into 0x71
	int _landed;              // DAT_004c3fd8
	int _walking;             // DAT_004c3b9c
	int _pathIndex;           // DAT_004c3b98
	int _inputLock;           // DAT_004c3fec
	int _reaction;            // DAT_004c4430
	int16 _fallSpeed;         // DAT_004c3ba0
	int16 _fallAccel;         // DAT_004c3ff4
	int16 _floorY;            // DAT_004c4420
	bool _testHookRead;
};

} // End of namespace NightmareNed

#endif // NIGHTMARENED_SCENE1_SUB7_H
