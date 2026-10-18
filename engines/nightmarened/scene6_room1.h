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

#ifndef NIGHTMARENED_SCENE6_ROOM1_H
#define NIGHTMARENED_SCENE6_ROOM1_H

#include "nightmarened/scene6.h"

namespace NightmareNed {

/**
 * Scene 6, sub-scene 1 (NAF scene 1, 96999001.SST). Its monster (anchored to slot 0x51)
 * idles (0x52) and attacks at random: 0x53, 0x54, or 0x55, which picks up the thing on
 * 0x59 (0x17f), tosses it (0x17d), spins it (0x17e, four times) and throws it at where
 * Ned stood. Zapping 0x5d plays it; at its frame 9 the monster comes out (0x6e) and
 * opens (0x56, a target, looping); zapping 0x56 knocks it back (0x57, with 0x191 over
 * 0x5d) and fills the meter 0x12f. A full meter kills it (0x58). When 0x5d ends the
 * monster goes back in (0x6f) and 0x192 plays over 0x5d.
 */
class Scene6Room1 : public Scene6Room {
public:
	explicit Scene6Room1(Game *game);

	int load(int fromScene, int fromSub) override;   // m2 0x45a610
	int enter(int fromScene, int fromSub) override;  // m3 0x45a640
	int leave(int toScene, int toSub) override;      // m4 0x45acd0
	int unload(int toScene, int toSub) override;     // m5 0x45ad20
	int tick(uint32 input, uint32 now) override;     // m7 0x45af40

private:
	enum {
		kList = 0x4afd50,
		kCount = 0x1b,
		kNafScene = 1,
		kMapFloor = 0x51bfb4,
		kBmpRoom = 0x4e236c,
		kBmpFront = 0x4e2370,
		kPalette = 0x51bfe0
	};

	uint32 entry(int id) const;
	bool loaded(int id) const;
	void swap(int16 id);
	/** The first-use set-up the animations below share */
	void attachOnce(int &flag, int id, int z);

	void chooseAction();           // 0x45ae20
	void actionTick(uint32 now);   // 0x45b1d0
	void vehicleIn(uint32 now);    // 0x45b1d0 case 10
	void idleTick(uint32 now);     // 0x45b7f0
	void knockTick(uint32 now);    // 0x45b8c0
	void attackTick(uint32 now);   // 0x45bb10
	void openTick(uint32 now);     // 0x45bbd0
	void inTick(uint32 now);       // 0x45bd00
	void outTick(uint32 now);      // 0x45bdc0
	void attack2Tick(uint32 now);  // 0x45be90
	void boxTick(uint32 now);      // 0x45bf90
	void closeTick(uint32 now);    // 0x45c080
	void spinTick(uint32 now);     // 0x45c1e0
	void flyTick(uint32 now);      // 0x45c2f0
	void heldTick(uint32 now);     // 0x45c570
	void tossTick(uint32 now);     // 0x45c6b0
	void throwTick(uint32 now);    // 0x45c7c0
	void deathTick(uint32 now);    // 0x45c8a0
	void vehicleOut(uint32 now);   // 0x45c9a0
	void hitChecks(uint32 now);    // 0x45cdd0

	static int hookUse(void *user, int x, int y, int16 *state);
	static int hookRegion(void *user, int32 id, uint32 flags, int x, int y, int16 *state);
	static void hookFindTarget(void *user, int x, int y, int16 *a, int16 *b, int32 rect[4]);
	static int hookHitTarget(void *user, int x, int y, int16 *a, int16 *b, const int32 rect[4]);

	int16 _monster;          // DAT_0051bfc0: the monster slot shown (-1 = none)
	int16 _action;           // DAT_0051c800
	int _ready;              // DAT_0051c804: the monster may pick its next action
	int _box;                // DAT_0051c808: 0x5d plays
	int _close;              // DAT_0051c80c: 0x192 plays over 0x5d
	int _open;               // DAT_0051c810: the monster is out (0x6e / 0x56)
	int _won;                // DAT_0051c814
	int _leaving;            // DAT_0051c818: the vehicle is back
	int _fly;                // DAT_0051c81c: 0x17e flies at Ned
	int _held;               // DAT_0051c820: 0x17f shows instead of 0x59
	int _toss;               // DAT_0051c824: 0x17d plays
	int _spin;               // DAT_0051c828: 0x17e spins in place
	int _hit;                // DAT_0051c82c: 0x17e hit Ned
	int _busy;               // DAT_0051c830: the idle loops until the throw is over
	int _done;               // DAT_0051c834: switching back to the playroom
	int _set52, _set57, _set53, _set54, _setSpin, _setFly, _set17f, _unused854; // DAT_0051c838..DAT_0051c854
	int _set55, _set56, _set58, _set6e, _set6f, _set17d; // DAT_0051c858..DAT_0051c86c
	int16 _spins;            // DAT_0051c870: 0x17e's loops (never reset)
	int _falling;            // DAT_0051c874: 0x17e passed Ned's x and drops
	int _flyDX;              // DAT_0051bfc8
	int _flyDY;              // DAT_0051c7e0
	int _flyX, _flyY;        // DAT_0051c7e8 / DAT_0051c7ec
	int _aimX, _aimY;        // DAT_0051c7f8 / DAT_0051c7fc: Ned's position at the throw
	int _flyHomeX, _flyHomeY; // DAT_0051bfb8 / DAT_0051bfbc: 0x17e's slot position

	// .data (0x4b011c..0x4b0140)
	int _chose;              // DAT_004b011c
	int _knockNew;           // DAT_004b0120
	int _sound57;            // DAT_004b0124
	int _sound54;            // DAT_004b0128
	int _sound5d;            // DAT_004b012c
	int _closeNew;           // DAT_004b0130
	int _soundClose;         // DAT_004b0134: 0x5d's sound, for 0x192
	int _flyNew;             // DAT_004b0138
	int _sound58;            // DAT_004b013c
	int _vehicleNew;         // DAT_004b0140
};

} // End of namespace NightmareNed

#endif // NIGHTMARENED_SCENE6_ROOM1_H
