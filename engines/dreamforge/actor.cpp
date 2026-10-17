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

#include "common/scummsys.h"

#include "dreamforge/actor.h"
#include "dreamforge/dreamforge.h"
#include "dreamforge/lipsync.h"
#include "dreamforge/walkmap.h"

namespace DreamForge {

// C.PRG 0x15e18: per slot and view, the frame's (x, y) offset from the feet
static const int8 kFrameOffsets[kActorAnimSlots][kActorViews][2] = {
	{ { -4, 0 }, { 0, 3 }, { 1, 1 }, { 0, 8 }, { -3, 6 } },  // 0x00
	{ { 2, 1 }, { -1, 6 }, { -2, 9 }, { -1, 8 }, { 1, 5 } },  // 0x01
	{ { 0, 7 }, { 0, 7 }, { 0, 7 }, { 0, 7 }, { 0, 6 } },  // 0x02
	{ { -1, 0 }, { 2, 3 }, { 4, 1 }, { 0, 0 }, { 0, 0 } },  // 0x03
	{ { 0, 7 }, { 0, 7 }, { 0, 7 }, { 0, 7 }, { 0, 6 } },  // 0x04
	{ { 31, 0 }, { 23, 5 }, { -2, 19 }, { -1, 9 }, { 31, 3 } },  // 0x05
	{ { 19, 0 }, { 17, 5 }, { -2, 19 }, { 0, 9 }, { 22, 5 } },  // 0x06
	{ { 19, 1 }, { 2, 5 }, { -1, 18 }, { -3, 10 }, { 19, 5 } },  // 0x07
	{ { -1, 0 }, { -4, 3 }, { -1, 1 }, { -2, 31 }, { -8, 5 } },  // 0x08
	{ { -2, 3 }, { -1, 7 }, { 0, 6 }, { 0, 12 }, { 0, 6 } },  // 0x09
	{ { 0, 0 }, { -1, 2 }, { -1, 1 }, { -2, 8 }, { 3, 7 } },  // 0x0a
	{ { 29, 0 }, { 23, 5 }, { -2, 19 }, { -1, 9 }, { 30, 3 } },  // 0x0b
	{ { -4, 1 }, { -6, 4 }, { 3, 3 }, { -3, 9 }, { -3, 5 } },  // 0x0c
	{ { -8, 1 }, { -9, 3 }, { -3, 1 }, { 0, 9 }, { -5, 6 } },  // 0x0d
	{ { 31, -1 }, { 23, 5 }, { -2, 19 }, { -1, 9 }, { 30, 3 } },  // 0x0e
	{ { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 35, 0 } },  // 0x0f
	{ { 11, 0 }, { 3, 3 }, { -2, 2 }, { -1, 10 }, { 3, 6 } },  // 0x10
	{ { 35, 2 }, { 23, 11 }, { -2, 19 }, { 1, -5 }, { 28, 1 } },  // 0x11
	{ { 35, 3 }, { 31, 4 }, { -1, 13 }, { 2, -4 }, { 32, 0 } },  // 0x12
	{ { 39, 2 }, { 31, 4 }, { -1, 14 }, { 2, -5 }, { 33, 0 } },  // 0x13
	{ { 23, 2 }, { 17, 6 }, { -1, 9 }, { 1, 5 }, { 16, 2 } },  // 0x14
	{ { 39, 3 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 } },  // 0x15
	{ { -6, 2 }, { -10, 3 }, { -2, 2 }, { 1, 14 }, { -5, 6 } },  // 0x16
	{ { -4, 1 }, { -9, 3 }, { -5, 3 }, { 0, 12 }, { -8, 7 } },  // 0x17
	{ { -1, 0 }, { -4, 3 }, { -1, 1 }, { -2, 31 }, { -8, 5 } },  // 0x18
	{ { 9, 6 }, { 0, 5 }, { -2, 3 }, { -3, 9 }, { 4, 10 } },  // 0x19
	{ { 37, 2 }, { 31, 4 }, { -1, 13 }, { 2, -5 }, { 32, 0 } },  // 0x1a
	{ { 0, 0 }, { 0, 2 }, { 0, 2 }, { 0, 8 }, { 0, 5 } },  // 0x1b
	{ { 23, 2 }, { 17, 6 }, { -1, 8 }, { 1, 5 }, { 16, 3 } },  // 0x1c
	{ { 3, 1 }, { 1, 3 }, { -2, 1 }, { 1, 8 }, { -1, 6 } },  // 0x1d
	{ { 0, 1 }, { -1, 3 }, { 0, 1 }, { 0, 9 }, { -1, 5 } },  // 0x1e
	{ { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 } },  // 0x1f
	{ { 9, 2 }, { 2, 2 }, { -3, 7 }, { 0, 18 }, { 2, 6 } },  // 0x20
};

// C.PRG 0x87384: x per walk frame, by view (negative = west)
static const int8 kStepX[kActorViews][8] = {
	{ -11, -19, -15, -25, -10, -16, -15, -19 },
	{ -14, -19, -7, -20, -7, -11, -14, -11 },
	{ 0, 0, 0, 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 0, 0, 0, 0 },
	{ -5, -6, -16, -20, -10, -17, -13, -13 },
};

// C.PRG 0x873ac: y per walk frame, by view
static const int8 kStepY[kActorViews][8] = {
	{ 0, 0, 0, 0, 0, 0, 0, 0 },
	{ 5, 1, 7, 3, 2, 1, 4, 6 },
	{ 4, 5, 9, 7, 5, 3, 9, 9 },
	{ 3, 7, 5, 7, 4, 4, 7, 5 },
	{ 1, 3, 1, 3, 2, 5, 3, 0 },
};


// C.PRG 0x88348: direction -> view (0 side, 1 three-quarter back, 2 back, 3 front, 4 three-quarter front)
static const int8 kDirView[8] = { 0, 1, 2, 1, 0, 4, 3, 4 };

int directionFromDelta(int dy, int dx) {
	if (dx == 0 && dy == 0)
		return -1;
	// 0x341e0: the sectors aren't equal, north and south get 90 degrees, east and west 20
	double a = atan2((double)-dy, (double)dx) * 180.0 / M_PI;
	if (a < 0)
		a += 360.0;
	if (a < 10 || a >= 350)
		return 4;
	if (a < 45)
		return 3;
	if (a < 135)
		return 2;
	if (a < 170)
		return 1;
	if (a < 190)
		return 0;
	if (a < 225)
		return 7;
	if (a < 315)
		return 6;
	return 5;
}

Actor::Actor(DreamForgeEngine *vm, int index) : _vm(vm), _index(index), _y(0), _x(0), _dir(6),
	_posture(kStanding), _hidden(false), _slot(kSlotStandIdle), _frame(0), _busy(false), _arrivalCode(-1), _arrivalY(0), _arrivalX(0), _walking(false),
	_targetY(0), _targetX(0), _yMajor(false), _slope(0), _minorAcc(0), _minorSign(1) {
}

bool Actor::mirrored() const {
	return _dir >= 3 && _dir <= 5;
}

int Actor::idleSlot() const {
	switch (_posture) {
	case kSitting: return kSlotSitIdle;
	case kLying: return kSlotLieIdle;
	default: return kSlotStandIdle;
	}
}

void Actor::idle() {
	_slot = idleSlot();
	_frame = 0;
}

void Actor::place(int y, int x, int dir) {
	_y = y;
	_x = x;
	stopWalking();
	if (dir >= 0 && dir < 8)
		_dir = dir;
	idle();
}

void Actor::setDirection(int dir) {
	if (dir >= 0 && dir < 8)
		_dir = dir;
}

const AnimFrames *Actor::frames(int slot, int dir) const {
	return _vm->getActorFrames(actorAnimIndex(_index, slot, kDirView[dir & 7]));
}

int Actor::directionTo(int y, int x) const {
	int d = directionFromDelta(y - _y, x - _x);
	return d < 0 ? _dir : d;
}

void Actor::stopWalking() {
	_walking = false;
	_path.clear();
}

bool Actor::walkTo(int y, int x) {
	const WalkMap &map = _vm->getWalkMap();
	if (y == _y && x == _x)
		return true;
	if (!map.isWalkable(_vm, y, x))
		return false;
	_path.clear();
	_path.push_back(Common::Point(x, y));
	if (!map.lineClear(_vm, _y, _x, y, x)) {
		Common::Array<Common::Point> via;
		if (!_vm->findWaypointPath(_y, _x, y, x, via))
			return false;
		for (int i = via.size() - 1; i >= 0; i--)
			_path.push_back(via[i]);
	}
	if (_posture != kStanding)
		_posture = kStanding;  // TODO: play the get up / stand up animation first
	startSegment();
	return true;
}

void Actor::startSegment() {
	while (!_path.empty()) {
		Common::Point p = _path.back();
		_path.pop_back();
		if (p.x == _x && p.y == _y)
			continue;
		_targetX = p.x;
		_targetY = p.y;
		_dir = directionTo(_targetY, _targetX);
		_yMajor = (_dir == 2 || _dir == 6);
		int major = _yMajor ? ABS(_targetY - _y) : ABS(_targetX - _x);
		int minor = _yMajor ? _targetX - _x : _targetY - _y;
		if (major == 0) {
			_yMajor = !_yMajor;
			major = _yMajor ? ABS(_targetY - _y) : ABS(_targetX - _x);
			minor = _yMajor ? _targetX - _x : _targetY - _y;
		}
		_minorSign = minor < 0 ? -1 : 1;
		_slope = major ? (int32)(((int64)ABS(minor) << 16) / major) : 0;
		_minorAcc = 0;
		_walking = true;
		_slot = kSlotWalk;
		return;
	}
	_walking = false;
	idle();
}

void Actor::step() {
	if (!_walking) {
		if (!_busy)
			idle();
		return;
	}
	const WalkMap &map = _vm->getWalkMap();
	int ref = _vm->getRefScale();
	int scale = map.scaleAt(_vm, _y, _x);
	if (scale <= 0 || ref <= 0)
		scale = ref = 1;
	int view = kDirView[_dir];
	int f = _frame & 7;
	int ny = _y, nx = _x;
	bool arrived = false;
	int len = _yMajor ? ABS(kStepY[view][f]) : ABS(kStepX[view][f]);
	int major = MAX(1, (len * scale + ref / 2) / ref);
	if (_yMajor) {
		int sign = _targetY > _y ? 1 : -1;
		major = MIN(major, ABS(_targetY - _y));
		ny = _y + sign * major;
		arrived = (ny == _targetY);
	} else {
		int sign = _targetX > _x ? 1 : -1;
		major = MIN(major, ABS(_targetX - _x));
		nx = _x + sign * major;
		arrived = (nx == _targetX);
	}
	_minorAcc += _slope * major;
	int moved = _minorAcc >> 16;
	_minorAcc &= 0xFFFF;
	if (_yMajor)
		nx = _x + _minorSign * moved;
	else
		ny = _y + _minorSign * moved;
	if (arrived) {
		ny = _targetY;
		nx = _targetX;
	}
	if (!map.isWalkable(_vm, ny, nx)) {
		stopWalking();
		idle();
		return;
	}
	_y = ny;
	_x = nx;
	_slot = kSlotWalk;
	_frame = (_frame + 1) & 7;
	if (arrived) {
		startSegment();
		if (!_walking)
			idle();
	}
}

Common::Rect Actor::drawRect(int scrollX, int scale, int refScale) const {
	const AnimFrames *anim = frames(_slot, _dir);
	if (!anim || anim->frames.empty() || refScale <= 0)
		return Common::Rect();
	const Graphics::Surface *s = anim->frames[_frame % anim->frames.size()];
	int view = kDirView[_dir];
	int offX = kFrameOffsets[_slot][view][0], offY = kFrameOffsets[_slot][view][1];
	if (mirrored())
		offX = -offX;
	int top = _y - (s->h - offY) * scale / refScale;
	int left = _x - (s->w / 2 - offX) * scale / refScale;
	return Common::Rect(left - scrollX, top, left - scrollX + s->w * scale / refScale, top + s->h * scale / refScale);
}

void Actor::draw(Graphics::Surface &dst, int scrollX, int scale, int refScale) const {
	if (_hidden || scale <= 0)
		return;
	const AnimFrames *anim = frames(_slot, _dir);
	if (!anim || anim->frames.empty())
		return;
	const Graphics::Surface *src = anim->frames[_frame % anim->frames.size()];
	Common::Rect r = drawRect(scrollX, scale, refScale);
	if (r.isEmpty())
		return;
	bool flip = mirrored();
	for (int y = MAX<int>(r.top, 0); y < MIN<int>(r.bottom, dst.h); y++) {
		int sy = (y - r.top) * src->h / r.height();
		const byte *srow = (const byte *)src->getBasePtr(0, sy);
		byte *drow = (byte *)dst.getBasePtr(0, y);
		for (int x = MAX<int>(r.left, 0); x < MIN<int>(r.right, dst.w); x++) {
			int sx = (x - r.left) * src->w / r.width();
			if (flip)
				sx = src->w - 1 - sx;
			byte c = srow[sx];
			if (c)
				drow[x] = c;
		}
	}
}

} // End of namespace DreamForge
