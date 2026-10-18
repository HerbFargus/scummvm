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

#ifndef NIGHTMARENED_SCENE6_ROOM2_H
#define NIGHTMARENED_SCENE6_ROOM2_H

#include "nightmarened/scene6.h"

namespace NightmareNed {

/**
 * Scene 6, sub-scene 2 (NAF scene 2, 96999002.SST). Its monster (anchored to slot
 * 0x38) idles (0x39) and attacks at random: a lunge (0x3b) that slides it left and
 * back at frame 6, pushing Ned along; a swipe (0x3a); or a throw (0x3c), which lights
 * 0x41, bursts 0x183, spins 0x184 four times and then flies it at Ned. Zapping the box
 * 0x44 makes the monster open up (0x3d); zapping it open knocks it back (0x3e) and
 * fills the meter 0x130. A full meter kills it (0x3f). Each hit on Ned adds to the
 * damage counter.
 */
class Scene6Room2 : public Scene6Room {
public:
	explicit Scene6Room2(Game *game);

	int load(int fromScene, int fromSub) override;   // m2 0x47d7f0
	int enter(int fromScene, int fromSub) override;  // m3 0x47d820
	int leave(int toScene, int toSub) override;      // m4 0x47ded0
	int unload(int toScene, int toSub) override;     // m5 0x47df20
	int tick(uint32 input, uint32 now) override;     // m7 0x47e140

private:
	enum {
		kList = 0x4b59e0,
		kCount = 0x19,
		kNafScene = 2,
		kMapFloor = 0x5206e0,
		kBmpRoom = 0x4e24fc,
		kBmpFront = 0x4e2500,
		kPalette = 0x520718
	};

	uint32 entry(int id) const;
	bool loaded(int id) const;
	void swap(int16 id);
	/** The first-use set-up the animations below share */
	void attachOnce(int &flag, int id, int z);

	void chooseAction();           // 0x47e020
	int actionTick(uint32 now);    // 0x47e3d0
	void idleTick(uint32 now);     // 0x47e920
	void knockTick(uint32 now);    // 0x47ea10
	void lungeTick(uint32 now);    // 0x47eb20
	void swipeTick(uint32 now);    // 0x47ecd0
	void spinTick(uint32 now);     // 0x47ed90
	void flightTick(uint32 now);   // 0x47eea0
	void glowTick(uint32 now);     // 0x47f100
	void burstTick(uint32 now);    // 0x47f260
	void throwTick(uint32 now);    // 0x47f370
	void openTick(uint32 now);     // 0x47f450
	void boxTick(uint32 now);      // 0x47f590
	void deathTick(uint32 now);    // 0x47f6c0
	void vehicleIn(uint32 now);    // 0x47e3d0 case 10
	void vehicleOut(uint32 now);   // 0x47f7c0
	void hitChecks(uint32 now);    // 0x47fbf0

	static int hookUse(void *user, int x, int y, int16 *state);
	static int hookRegion(void *user, int32 id, uint32 flags, int x, int y, int16 *state);
	static void hookFindTarget(void *user, int x, int y, int16 *a, int16 *b, int32 rect[4]);
	static int hookHitTarget(void *user, int x, int y, int16 *a, int16 *b, const int32 rect[4]);

	int16 _monster;          // DAT_00520f1c: the monster slot shown (-1 = none)
	int16 _action;           // DAT_00520710
	int _homeX;              // DAT_00520f28: 0x38's x before a lunge
	int _ready;              // DAT_00520f2c: the monster may pick its next action
	int _box;                // DAT_00520f30: the box 0x44 plays
	int _won;                // DAT_00520f34
	int _flying;             // DAT_00520f38: 0x184 flies at Ned
	int _glow;               // DAT_00520f3c: 0x41 shows (from the throw)
	int _burst;              // DAT_00520f40: 0x183 plays (after the throw)
	int _spin;               // DAT_00520f44: 0x184 spins (after the burst)
	int _shotHit;            // DAT_00520f48: 0x184 caught Ned
	int _throwing;           // DAT_00520f4c: the idle loops until the shot is over
	int _set39, _set3e, _set3b, _set3a, _set184a, _set184b, _set41, _set183, _set3c, _set3d, _set44, _set3f; // DAT_00520f50..DAT_00520f7c
	int _done;               // DAT_00520f80: switching back to the playroom
	int _lungeHeld;          // DAT_00520f84: the lunge is back home
	int16 _spins;            // DAT_00520f88
	int _falling;            // DAT_00520f8c: 0x184 passed Ned's x and drops
	// The flight of 0x184
	int _shotX, _shotY;      // DAT_005206d0 / DAT_005206d4
	int _shotHomeX, _shotHomeY; // DAT_005206d8 / DAT_005206dc: its slot position before the flight
	int _shotVY;             // DAT_005206e8
	int _shotVX;             // DAT_00520700
	int _shotTargetX;        // DAT_00520708: Ned's x at the start
	int _shotTargetY;        // DAT_0052070c: Ned's y at the start (unused)

	// .data (0x4b5d64..0x4b5d90)
	int _leaving;            // DAT_004b5d64: the vehicle is back
	int _chose;              // DAT_004b5d68
	int _sound39;            // DAT_004b5d6c
	int _sound3e;            // DAT_004b5d70
	int16 _lungeStep;        // DAT_004b5d74 (i16)
	int _lungeNew;           // DAT_004b5d78
	int _flightNew;          // DAT_004b5d7c
	int _place40;            // DAT_004b5d80
	int _sound41;            // DAT_004b5d84
	int _sound44;            // DAT_004b5d88
	int _sound3f;            // DAT_004b5d8c
	int _vehicleNew;         // DAT_004b5d90
};

} // End of namespace NightmareNed

#endif // NIGHTMARENED_SCENE6_ROOM2_H
