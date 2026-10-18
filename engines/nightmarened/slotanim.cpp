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

#include "nightmarened/game.h"
#include "nightmarened/rdx.h"
#include "nightmarened/slotanim.h"

namespace NightmareNed {

static bool validSlot(int area, int id) {
	return area >= 0 && area < kAreaCount && id >= 0 && id < kAreaSlots;
}

// 0x430530 slot_place_nx: the step's duration
static int placeNx(Game *game, int area, int id, int ref) {
	int frame = 0, duration = 0;
	if (!validSlot(area, id) || !validSlot(area, ref))
		return 0;
	game->display().slotPlace(area, id, ref, &frame, &duration, true, Common::Point(0, 0));
	return duration;
}

// 0x41d330 / 0x479e20
bool slotStep(Game *game, int area, int id, int ref, uint32 now, int16 duration) {
	if (!validSlot(area, id))
		return false;
	Slot &s = game->res().getSlot(area, id);
	if (now <= s.deadline) {
		placeNx(game, area, id, ref);
		return true;
	}
	s.step++;
	if (s.sequenceLength <= s.step)
		return false;
	int d = placeNx(game, area, id, ref);
	s.deadline = now + (int16)(duration ? duration : d) * kMsPerStep;
	return true;
}

// 0x41d400 / 0x479ef0
int16 slotStart(Game *game, int area, int newSlot, int oldSlot, int ref, uint32 now, int16 step) {
	game->display().slotSwitch(area, (int16)newSlot, (int16)oldSlot);
	if ((int16)newSlot > 0 && validSlot(area, newSlot)) {
		Slot &s = game->res().getSlot(area, newSlot);
		if (step < 0)
			step = s.sequenceLength > 0 ? (int16)((int)game->crtRandom() % s.sequenceLength) : 0;
		s.step = step;
		int d = placeNx(game, area, newSlot, ref);
		s.deadline = now + (int16)d * kMsPerStep;
	}
	return (int16)newSlot;
}

// 0x479fb0
void slotNudge(Game *game, int area, int id, int dx, int dy) {
	if (!validSlot(area, id))
		return;
	Slot &s = game->res().getSlot(area, id);
	s.x += dx;
	s.y += dy;
	if (s.sprite)
		s.sprite->adjPosition(dx, dy);
}

// 0x41e060
void slotShow(Game *game, int area, int id, int z, bool visible) {
	if (!validSlot(area, id))
		return;
	Slot &s = game->res().getSlot(area, id);
	if (s.sprite) {
		s.sprite->setVisibility(visible);
		s.sprite->setDrawOrder((int16)z);
	}
}

} // End of namespace NightmareNed
