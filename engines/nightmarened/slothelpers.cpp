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

#include "common/endian.h"

#include "nightmarened/game.h"
#include "nightmarened/rdx.h"
#include "nightmarened/resources.h"
#include "nightmarened/slothelpers.h"

namespace NightmareNed {

// 0x41e060
void slotShow(Game *game, int area, int id, int z, bool visible) {
	Slot &s = game->res().getSlot(area, id);
	if (!s.sprite)
		return;
	s.sprite->setVisibility(visible);
	s.sprite->setDrawOrder((uint32)z);
}

// 0x479fb0
void slotMoveBy(Game *game, int area, int id, int dx, int dy) {
	Slot &s = game->res().getSlot(area, id);
	s.x += dx;
	s.y += dy;
	if (s.sprite)
		s.sprite->adjPosition(dx, dy);
}

// 0x4558e0
bool slotPlayLoop(Game *game, int area, int id) {
	Slot &s = game->res().getSlot(area, id);
	game->stopSlotSound(s);
	if (s.sound.size() < 18)
		return false;
	uint32 pcm = 18 + READ_LE_UINT16(s.sound.data() + 16);
	if (pcm >= s.sound.size())
		return false;
	game->playVoice(s.voice, s.sound.data(), s.sound.data() + pcm, s.sound.size() - pcm, 0, 0, true);
	return true;
}

} // End of namespace NightmareNed
