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

#ifndef NIGHTMARENED_SCENE6_ROOM3_H
#define NIGHTMARENED_SCENE6_ROOM3_H

#include "common/array.h"

#include "nightmarened/scene6.h"

namespace NightmareNed {

/**
 * Scene 6, sub-scene 3 (NAF scene 4, 96999004.SST). Its monster (anchored to slot 0x5e)
 * idles (0x5f) and attacks at random: 0x60 swoops along an arc at Ned, 0xf5 grabs (then
 * 0x61 carries Ned off if it caught him, else 0xf6), 0x62 sets off 0x181 on the prop
 * 0x180. Zapping the box 0x66 plays 0x67; from its step 5, 0x69 (on 0x68) plays with the
 * front bitmap shown and the monster opens up (0x63). Each zap on the open monster
 * knocks it back (0x64) and fills the meter 0x135; a full meter kills it (0x65).
 */
class Scene6Room3 : public Scene6Room {
public:
	explicit Scene6Room3(Game *game);

	int load(int fromScene, int fromSub) override;   // m2 0x44a240
	int enter(int fromScene, int fromSub) override;  // m3 0x44a270
	int leave(int toScene, int toSub) override;      // m4 0x44a920
	int unload(int toScene, int toSub) override;     // m5 0x44a970
	int tick(uint32 input, uint32 now) override;     // m7 0x44ab90

private:
	enum {
		kList = 0x4a5100,
		kCount = 0x1d,
		kNafScene = 4,
		kMapFloor = 0x4c4448,
		kBmpRoom = 0x4e281c,
		kBmpFront = 0x4e2820,
		kPalette = 0x4c4468,
		kDegToRad = 0x495250,   // double: 3.14159265359 / 180
		kHalf = 0x495258,       // double: 0.5
		kArcSize = 360,         // 0xb40 bytes of (x, y) int32 pairs
		kArcLast = 0xb2
	};

	uint32 entry(int id) const;
	bool loaded(int id) const;
	void swap(int16 id);
	/** The first-use set-up the animations below share */
	void attachOnce(int &flag, int id, int z);
	/** 0x441580: an elliptical arc in rect, 179 points (reverse 0: 360 down to 182 degrees) */
	void buildArc(const int32 rect[4], int reverse);

	void chooseAction();           // 0x44aa70
	void actionTick(uint32 now);   // 0x44ae90
	void introTick(uint32 now);    // 0x44ab90 (inlined copy of the dead 0x44bfa0)
	void idleTick(uint32 now);     // 0x44b590
	void knockTick(uint32 now);    // 0x44b680
	void swoopTick(uint32 now);    // 0x44b770
	void grabTick(uint32 now);     // 0x44baa0
	void grabMissTick(uint32 now); // 0x44bb80
	void carryTick(uint32 now);    // 0x44bc50
	void summonTick(uint32 now);   // 0x44bd80
	void spillTick(uint32 now);    // 0x44be60
	void openTick(uint32 now);     // 0x44c040
	void deathTick(uint32 now);    // 0x44c180
	void doorTick(uint32 now);     // 0x44c270
	void boxTick(uint32 now);      // 0x44c420
	void vehicleIn(uint32 now);    // 0x44ae90 case 10
	void vehicleOut(uint32 now);   // 0x44c590
	void hitChecks(uint32 now);    // 0x44c9d0

	static int hookUse(void *user, int x, int y, int16 *state);
	static int hookRegion(void *user, int32 id, uint32 flags, int x, int y, int16 *state);
	static void hookFindTarget(void *user, int x, int y, int16 *a, int16 *b, int32 rect[4]);
	static int hookHitTarget(void *user, int x, int y, int16 *a, int16 *b, const int32 rect[4]);

	int16 _monster;          // DAT_004c4c8c: the monster slot shown (-1 = none)
	int16 _action;           // DAT_004c4c78
	int _unused98;           // DAT_004c4c98
	int _ready;              // DAT_004c4c9c: the monster may pick its next action
	int _won;                // DAT_004c4ca4
	int _leaving;            // DAT_004c4ca8: the vehicle is back
	int _box;                // DAT_004c4cac: the box 0x67 plays
	int _open;               // DAT_004c4cb0: 0x69 plays, the monster stays open
	int _done;               // DAT_004c4cb4: switching back to the playroom
	int _spill;              // DAT_004c4cb8: 0x181 plays on 0x180
	int _set5f, _set64, _set60, _set61, _setF5, _setF6, _set181, _set69, _set67, _set62, _set63, _set65; // DAT_004c4cbc..DAT_004c4ce8
	int _nedHit;             // DAT_004c4cec: the 0xf5 grab caught Ned

	// The swoop 0x60 (moves the anchor 0x5e along _arc)
	int _swoopNedX;          // DAT_004c4c80
	int _swoopNedY;          // DAT_004c4c84 (unread)
	int16 _arcIndex;         // DAT_004c4c88
	int _anchorX, _anchorY;  // DAT_004c4450 / DAT_004c4454: slot 0x5e's position before
	int16 _arcStep;          // DAT_004c4458
	Common::Array<int32> _arc; // DAT_004c445c
	int _swoopBack;          // DAT_004c4cf0
	int _swoopTurned;        // DAT_004c4cf4 (unread)
	int _swoopReached;       // DAT_004c4cf8: the anchor is past Ned's old x

	// .data (0x4a5514..0x4a5530)
	int _intro;              // DAT_004a5514: 0x180 plays its sequence once
	int _chose;              // DAT_004a5518
	int _swoopNew;           // DAT_004a551c
	int _spillNew;           // DAT_004a5520
	int _sound69;            // DAT_004a5524
	int _doorOpens;          // DAT_004a5528: the next 0x69 opens the monster
	int _sound67;            // DAT_004a552c
	int _vehicleNew;         // DAT_004a5530
};

} // End of namespace NightmareNed

#endif // NIGHTMARENED_SCENE6_ROOM3_H
