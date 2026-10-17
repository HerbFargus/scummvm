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

#ifndef DREAMFORGE_ACTOR_H
#define DREAMFORGE_ACTOR_H

#include "common/array.h"
#include "common/rect.h"
#include "graphics/surface.h"

namespace DreamForge {

class DreamForgeEngine;
struct AnimFrames;

// Directions (0x647d4): 0 W, 1 NW, 2 N, 3 NE, 4 E, 5 SE, 6 S, 7 SW
enum {
	kDirKeep = 8
};

// Animation slots (frames per slot at C.PRG 0x16340)
enum ActorSlot {
	kSlotBark = 0x00,
	kSlotStartWalk = 0x01,
	kSlotTurnA = 0x02,
	kSlotTurnB = 0x04,
	kSlotSitIdle = 0x05,
	kSlotSitDown = 0x06,
	kSlotStandUp = 0x07,
	kSlotLieDown = 0x14,
	kSlotLieDownAlt = 0x0F,
	kSlotLieBark = 0x11,
	kSlotLieIdle = 0x13,
	kSlotStandIdle = 0x1B,
	kSlotGetUp = 0x1C,
	kSlotStopWalk = 0x1D,
	kSlotUTurn = 0x1F,
	kSlotWalk = 0x20
};

enum Posture {
	kStanding,
	kSitting,
	kLying
};

/**
 * One of the two puppies (guy.c). Positions are the feet: y is the screen
 * row, x the scene column. Puppy 1 is drawn in palette entries 32..47 and
 * puppy 2 in 16..31, which every scene keeps for them.
 */
class Actor {
public:
	Actor(DreamForgeEngine *vm, int index);

	int _index;
	int _y, _x;
	int _dir;
	Posture _posture;
	bool _hidden;
	int _slot, _frame;
	bool _busy;  // a one-shot animation is playing
	int _arrivalCode, _arrivalY, _arrivalX;  // the click to post again on arrival (0x88304)

	/** Set position and facing (0x1b8d0, 0x169d0); kDirKeep keeps the facing */
	void place(int y, int x, int dir);
	void setDirection(int dir);
	/** Back to the idle frame for the posture */
	void idle();
	int idleSlot() const;

	/** Plans a walk (0x1b1d0); false if the target can't be reached */
	bool walkTo(int y, int x);
	bool isWalking() const { return _walking; }
	void stopWalking();
	/** One game step (0x18d00) */
	void step();

	/** The frames of a slot in this puppy's view for a direction (PIG2) */
	const AnimFrames *frames(int slot, int dir) const;
	/** Screen rect for the current frame, or empty */
	Common::Rect drawRect(int scrollX, int scale, int refScale) const;
	void draw(Graphics::Surface &dst, int scrollX, int scale, int refScale) const;

	/** Direction from this puppy to a point (0x341e0) */
	int directionTo(int y, int x) const;

private:
	void startSegment();
	bool mirrored() const;

	DreamForgeEngine *_vm;
	bool _walking;
	Common::Array<Common::Point> _path;  // waypoints still to go (x, y), the next one last
	int _targetY, _targetX;
	bool _yMajor;
	int32 _slope;      // minor axis per major step, 16.16
	int32 _minorAcc;   // 16.16
	int _minorSign;
};

int directionFromDelta(int dy, int dx);

} // End of namespace DreamForge

#endif // DREAMFORGE_ACTOR_H
