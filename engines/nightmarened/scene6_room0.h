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

#ifndef NIGHTMARENED_SCENE6_ROOM0_H
#define NIGHTMARENED_SCENE6_ROOM0_H

#include "nightmarened/scene6.h"

namespace NightmareNed {

/**
 * Scene 6, sub-scene 0 (NAF scene 3, 96999003.SST): the cupboard room. Its clay monster
 * (anchored to slot 0x45) idles (0x46) and attacks at random (0xf7 then 0xf8 or 0x47,
 * 0x48, or 0x49, which throws 0x4e). Zapping the box 0x17c makes it open up (0x4a);
 * zapping it then, at step 11, knocks it back (0x4b) and fills the meter 0x131. A full
 * meter kills it (0x4c). Each hit on Ned (and the jar 0x50, which tips 0x23) adds to
 * the damage counter.
 */
class Scene6Room0 : public Scene6Room {
public:
	explicit Scene6Room0(Game *game);

	int load(int fromScene, int fromSub) override;   // m2 0x425ae0
	int enter(int fromScene, int fromSub) override;  // m3 0x425b10
	int leave(int toScene, int toSub) override;      // m4 0x426260
	int unload(int toScene, int toSub) override;     // m5 0x4262b0
	int tick(uint32 input, uint32 now) override;     // m7 0x4264d0

private:
	enum {
		kList = 0x49dc78,
		kCount = 0x1e,
		kNafScene = 3,
		kMapFloor = 0x4bca3c,
		kBmpRoom = 0x4e268c,
		kBmpFront = 0x4e2690,
		kPalette = 0x4bc220
	};

	uint32 entry(int id) const;
	bool loaded(int id) const;
	void swap(int16 id);
	/** The first-use set-up the animations below share */
	void attachOnce(int &flag, int id, int z);

	void chooseAction();           // 0x4263b0
	void actionTick(uint32 now);   // 0x426810
	void idleTick(uint32 now);     // 0x426e20
	void openTick(uint32 now);     // 0x426f10
	void jarTick(uint32 now);      // 0x427060
	void tipTick(uint32 now);      // 0x427190
	void knockTick(uint32 now);    // 0x427390
	void attackTick(uint32 now);   // 0x4274b0
	void attackEndTick(uint32 now); // 0x427590
	void attackHitTick(uint32 now); // 0x427660
	void attack2Tick(uint32 now);  // 0x427750
	void throwTick(uint32 now);    // 0x427840
	void shotTick(uint32 now);     // 0x427910
	void deathTick(uint32 now);    // 0x427a40
	void vehicleIn(uint32 now);    // 0x426810 case 10
	void vehicleOut(uint32 now);   // 0x427b40
	void hitChecks(uint32 now);    // 0x427f70

	static int hookUse(void *user, int x, int y, int16 *state);
	static int hookRegion(void *user, int32 id, uint32 flags, int x, int y, int16 *state);
	static void hookFindTarget(void *user, int x, int y, int16 *a, int16 *b, int32 rect[4]);
	static int hookHitTarget(void *user, int x, int y, int16 *a, int16 *b, const int32 rect[4]);

	int16 _monster;          // DAT_004bca30: the monster slot shown (-1 = none)
	int16 _action;           // DAT_004bca68
	int _jar;                // DAT_004bca78: Ned was hit, the jar 0x50 rocks
	int _tip;                // DAT_004bca7c: 0x23 tips (from the jar's step 3)
	int _ready;              // DAT_004bca80: the monster may pick its next action
	int _won;                // DAT_004bca84
	int _leaving;            // DAT_004bca88: the vehicle is back
	int _box;                // DAT_004bca8c: the box 0x17c plays
	int _unused90;           // DAT_004bca90
	int _open;               // DAT_004bca94: the monster is open (0x4a)
	int _done;               // DAT_004bca98: switching back to the playroom
	int _shot;               // DAT_004bca9c: the shot 0x4e flies
	int _set46, _set4b, _set50, _set23, _set47, _setF8, _setF7, _set48, _set4e, _set49, _set4a, _set4c; // DAT_004bcaa0..DAT_004bcacc
	int _nedHit;             // DAT_004bcad0: the 0xf7 attack caught Ned

	// .data (0x49e0b0..0x49e0e0)
	int _chose;              // DAT_0049e0b0
	int _sound46, _sound4a, _sound50, _sound23, _sound17c, _sound4b, _sound47, _sound48; // DAT_0049e0b4..d0
	int _shotNew;            // DAT_0049e0d4
	int _sound4e;            // DAT_0049e0d8
	int _sound4c;            // DAT_0049e0dc
	int _vehicleNew;         // DAT_0049e0e0
};

} // End of namespace NightmareNed

#endif // NIGHTMARENED_SCENE6_ROOM0_H
