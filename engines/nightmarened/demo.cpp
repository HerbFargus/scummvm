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

#include "common/debug.h"
#include "common/ptr.h"
#include "common/textconsole.h"

#include "nightmarened/demo.h"
#include "nightmarened/detection.h"
#include "nightmarened/display.h"
#include "nightmarened/game.h"
#include "nightmarened/nightmarened.h"
#include "nightmarened/rdx.h"

namespace NightmareNed {

enum {
	kAtticArea = 3,
	kAtticList = 0x497680,
	kAtticListCount = 0x13,
	kScrollStep = 16
};

PlacementDemo::PlacementDemo(NightmareNedEngine *vm, Game *game) : _vm(vm), _game(game), _res(&game->res()),
	_display(&game->display()), _naf(nullptr), _lastTick(0) {
}

PlacementDemo::~PlacementDemo() {
	_res->freeList(_extra);
	_res->freeList(_list);
	delete _naf;
}

// Until the scene code supplies the request lists, take the first NAF scene with this id
int PlacementDemo::findSequenceScene(int id) const {
	for (const ResKey &k : _naf->listResources(kResSequence)) {
		if (k.id == id)
			return k.scene;
	}
	return -1;
}

// An alias borrows the frames of the slot its sequence names, from the same NAF scene,
// so that one loads first (the real request lists are ordered that way)
bool PlacementDemo::loadSlot(int area, int id, int scene) {
	if (scene < 0)
		scene = findSequenceScene(id);
	if (scene < 0)
		return false;
	if (_slotScenes.contains(id) && _slotScenes[id] == scene)
		return true;
	Common::ScopedPtr<Common::SeekableReadStream> s(_naf->getResource(kResSequence, scene, id));
	Sequence seq;
	if (!s || !seq.load(*s))
		return false;
	if (seq.sprite != id && seq.sprite < kAreaSlots && _naf->hasResource(kResSprite, scene, seq.sprite))
		loadSlot(area, seq.sprite, scene);

	Request req;
	req.type = Request::kSprite;
	req.area = area;
	req.id = id;
	req.scene = scene;
	if (!_res->load(_naf, req))
		return false;
	_extra.push_back(req);
	_slotScenes[id] = scene;
	return true;
}

bool PlacementDemo::loadAttic() {
	_naf = _vm->openNaf(98000);
	if (!_naf)
		return false;
	_list = Resources::readRequests(_vm->getExe(), kAtticList, kAtticListCount);
	_res->loadList(_naf, _list, 0);
	for (Request &r : _list) {
		if (r.collision)
			_collision.push_back(r.collision);
		if (r.hasPalette && r.paletteVA)
			_display->applyPalette(r.palette, kPalGame); // 0x404760: pal_apply(0x5259a0, 11)
	}

	Common::ScopedPtr<Common::SeekableReadStream> sst(_vm->openFile("98999001.SST"));
	Common::Array<Placement> placements;
	if (!sst || !loadPlacements(*sst, placements)) {
		warning("Can't read 98999001.SST");
		return false;
	}

	for (const Placement &p : placements) {
		Placed placed = { p.area, p.slot, nullptr };
		if (p.kind == 2) {
			// A sprite slot: area 3 is NED98000
			if (p.area != kAtticArea || p.slot < 0 || p.slot >= kAreaSlots)
				continue;
			Slot &slot = _res->getSlot(p.area, p.slot);
			if (!slot.loaded && !loadSlot(p.area, p.slot, -1))
				continue;
			slot.x = p.x;
			slot.y = p.y;
			slot.z = p.z;
			if (p.flip)
				slot.flags |= 4;
			slot.sprite->setDestination(_display->getPrimary());
			slot.sprite->setDrawOrder(p.z);
			slot.sprite->setVisibility(true);
			if (slot.flags & 4)
				slot.flip = slot.sprite->applyEffect(kFxHorizFlip);
		} else if (p.area == 98) {
			// A bitmap record: (scene, id) = (slot, sub)
			BitmapRec *rec = nullptr;
			for (Request &r : _list) {
				if (r.bitmap && r.scene == p.slot && r.id == p.sub)
					rec = r.bitmap;
			}
			if (!rec) {
				Request req;
				req.type = Request::kBitmap;
				req.area = p.transparentColor;
				req.id = p.sub;
				req.scene = p.slot;
				if (!_res->load(_naf, req))
					continue;
				_extra.push_back(req);
				rec = req.bitmap;
			}
			if (!rec->object)
				continue;
			rec->x = p.x;
			rec->y = p.y;
			rec->z = p.z;
			rec->object->setDestination(_display->getPrimary());
			rec->object->setDrawOrder(p.z);
			rec->object->setVisibility(true);
			placed.bitmap = rec;
		} else {
			continue;
		}
		_placed.push_back(placed);
	}
	debugC(1, kDebugGraphics, "Attic: %d objects placed, %d collision maps", _placed.size(), _collision.size());
	return true;
}

// Each sprite loops its sequence: step, place, deadline = now + duration * 41 ms
void PlacementDemo::place(uint32 now, bool advance) {
	for (const Placed &p : _placed) {
		if (p.bitmap) {
			p.bitmap->object->setPosition(p.bitmap->x + _scroll.x, p.bitmap->y + _scroll.y);
			continue;
		}
		Slot &slot = _res->getSlot(p.area, p.slot);
		int duration = 0;
		if (advance && slot.deadline <= now && slot.sequenceLength > 0) {
			slot.step = slot.deadline ? (slot.step + 1) % slot.sequenceLength : 0;
			_display->slotPlace(p.area, p.slot, p.slot, nullptr, &duration, !(slot.flags & 4), _scroll);
			slot.deadline = now + MAX(duration, 1) * kMsPerStep;
			debugC(5, kDebugGraphics, "Slot %d:%d step %d/%d at %d,%d visible %d", p.area, p.slot, slot.step,
				slot.sequenceLength, slot.sprite->getPosition().x, slot.sprite->getPosition().y, slot.sprite->getVisibility());
		} else {
			_display->slotPlace(p.area, p.slot, p.slot, nullptr, nullptr, !(slot.flags & 4), _scroll);
		}
	}
}

bool PlacementDemo::tick(uint32 input, uint32 now) {
	// Scene ticks throttle themselves to 41 ms (S4 0x404f10)
	if (_lastTick && now < _lastTick + kMsPerStep)
		return false;
	_lastTick = now;

	if (input & kInputLeft)
		_scroll.x += kScrollStep;
	if (input & kInputRight)
		_scroll.x -= kScrollStep;
	if (input & kInputUp)
		_scroll.y += kScrollStep;
	if (input & kInputDown)
		_scroll.y -= kScrollStep;

	place(now, true);
	_display->setCollisionOverlay(_collision, _scroll);
	_display->draw();
	return true;
}

} // End of namespace NightmareNed
