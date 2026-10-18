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


#include "common/ptr.h"
#include "common/textconsole.h"

#include "nightmarened/game.h"
#include "nightmarened/nightmarened.h"
#include "nightmarened/placements.h"
#include "nightmarened/resources.h"

namespace NightmareNed {

enum {
	kSstAreaPrefix = 0x4afcd8,  // i16 per area index: 99 - index
	kAreaIndexTable = 0x4afc60  // i16 per area number (0x45a440)
};

bool loadSceneSst(Game *game, int areaIndex, int scene, int n, Common::Array<Placement> &list) {
	list.clear();
	Common::String name = Common::String::format("%02d%03d0%02d.sst",
		(int16)game->exe().readUint16(kSstAreaPrefix + 2 * areaIndex), scene, n);
	Common::ScopedPtr<Common::SeekableReadStream> sst(game->getEngine()->openFile(name));
	if (!sst || !loadPlacements(*sst, list)) {
		warning("Can't read %s", name.c_str());
		return false;
	}
	return true;
}

void applyPlacement(Game *game, const Placement &e, PlacementBitmapLookup lookup, void *user) {
	if (e.kind == 2) {
		if (e.area <= 0 || e.area >= 5 || e.slot < 0 || e.slot >= kAreaSlots)
			return;
		Slot &s = game->res().getSlot(e.area, e.slot);
		s.x = e.x;
		s.y = e.y;
		s.z = e.z;
		s.offsets[0] = e.offX;
		s.offsets[1] = e.offY;
		s.key = e.transparentColor;
		if (e.flip)
			s.flags |= 4;
	} else if (e.kind == 0 || e.kind == 1) {
		if (e.area <= 0 || !lookup)
			return;
		int index = (int16)game->exe().readUint16(kAreaIndexTable + 2 * e.area);
		BitmapRec *rec = lookup(user, kPlacementBitmapTable + 4 * ((index * 15 + e.slot) * 100 + e.sub));
		if (!rec)
			return;
		rec->x = e.x;
		rec->y = e.y;
		rec->z = e.z;
	}
}

} // End of namespace NightmareNed
