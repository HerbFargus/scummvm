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

#include "nightmarened/display.h"
#include "nightmarened/game.h"
#include "nightmarened/puff.h"
#include "nightmarened/rdx.h"

namespace NightmareNed {

// 0x430850 with the caller's xform: returns the step's duration
int ZapPuff::place(NedXform xform, void *user) {
	int frame = 0, duration = 0;
	if (!_game->display().slotPlace(kArea, _slot, _slot, &frame, &duration, true, Common::Point(0, 0)))
		return 0;
	Slot &s = _game->res().getSlot(kArea, _slot);
	if (xform && s.sprite) {
		Common::Point p = s.sprite->getPosition();
		int x = p.x, y = p.y;
		xform(user, x, y);
		s.sprite->setPosition(x, y);
	}
	return duration;
}

void ZapPuff::start(int area, int slot, int z, uint32 now, NedXform xform, void *user) {
	Slot &target = _game->res().getSlot(area, slot);
	Slot &puff = _game->res().getSlot(kArea, kPuffSlot);
	puff.x = (target.w - puff.w) / 2 + target.x;
	puff.y = target.y + (target.h - puff.h) / 2;
	if (puff.sprite) {
		puff.sprite->setDrawOrder(z);
		puff.sprite->setDestination(_game->display().getPrimary());
	}
	_slot = (int16)_game->display().slotSwitch(kArea, kPuffSlot, _slot);
	int duration = place(xform, user);
	puff.deadline = now + (int16)duration * kMsPerStep;
	// 0x455950: every slot voice stops
	for (int a = 0; a < kAreaCount; a++) {
		for (int i = 0; i < kAreaSlots; i++)
			_game->stopSlotSound(_game->res().getSlot(a, i));
	}
	_game->playSlotSound(puff, nullptr);
}

void ZapPuff::tick(uint32 now, NedXform xform, void *user) {
	if (_slot == -1)
		return;
	Slot &s = _game->res().getSlot(kArea, _slot);
	if (s.deadline < now) {
		s.step++;
		if (s.sequenceLength <= s.step) {
			_slot = (int16)_game->display().slotSwitch(kArea, -1, _slot);
			return;
		}
		int duration = place(xform, user);
		s.deadline = now + (int16)duration * kMsPerStep;
		return;
	}
	place(xform, user);
}

void ZapPuff::nudge(int dx, int dy) {
	if (_slot == -1)
		return;
	Slot &s = _game->res().getSlot(kArea, _slot);
	if (s.sprite)
		s.sprite->adjPosition(dx, dy);
}

} // End of namespace NightmareNed
