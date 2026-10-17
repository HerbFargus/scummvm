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
#include "nightmarened/ned.h"
#include "nightmarened/nightmarened.h"
#include "nightmarened/rdx.h"

namespace NightmareNed {

enum {
	kAtticArea = 3,
	kAtticList = 0x497680,
	kAtticListCount = 0x13,
	kAtticPalette = 0x5259a0, // the scene palette 0x404760 merges Ned's colours into
	kNedListAttic1 = 7,       // 0x403f60
	kNedListAttic2 = 0x15,
	kAtticStartRoom = 2,      // InitialAtticScene's default (0x403f60)
	kFollowLeft = 200,
	kFollowRight = 440,
	kFollowTop = 140,
	kFollowBottom = 440
};

// The collision maps 0x404760 adds to the world by default (list 0x497680 destinations);
// 0x52594c and 0x52595c are alternates it adds only for other attic states
static const uint32 kAtticMaps[] = { 0x525940, 0x525944, 0x525948, 0x525950, 0x525954, 0x525958, 0x525960 };

PlacementDemo::PlacementDemo(NightmareNedEngine *vm, Game *game) : _vm(vm), _game(game), _res(&game->res()),
	_display(&game->display()), _naf(nullptr), _lastTick(0), _facing(1), _nedStarted(false) {
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
	Ned *ned = _game->ned();
	if (ned) {
		// 0x403f60 (m2): the attic's Ned lists
		ned->listLoad(kNedListAttic1);
		ned->listLoad(kNedListAttic2);
	}
	_list = Resources::readRequests(_vm->getExe(), kAtticList, kAtticListCount);
	_res->loadList(_naf, _list, 0);
	for (Request &r : _list) {
		if (r.collision) {
			_collision.push_back(r.collision);
			for (uint32 va : kAtticMaps) {
				if (r.destVA == va) // 0x404760: world_add_map(map, bc_bounds(map))
					_world.addMap(r.collision, r.collision->bounds.left, r.collision->bounds.top,
						r.collision->bounds.right, r.collision->bounds.bottom);
			}
		}
		if (r.hasPalette && r.paletteVA) {
			if (ned && r.paletteVA == kAtticPalette)
				ned->copyPalette(r.palette); // 0x450210(0x5259a0)
			_display->applyPalette(r.palette, kPalGame); // 0x404760: pal_apply(0x5259a0, 11)
		}
	}
	_world.setUser(&_userMap);

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
	if (ned)
		startNed();
	return true;
}

// 0x404760 (m3) for Ned: the room's scroll, a floor point found upwards from the room's
// bottom in map 0, facing right, idle, 33 ms steps through the attic xform
void PlacementDemo::startNed() {
	Ned *ned = _game->ned();
	ned->listShow(kNedListAttic1);
	ned->listShow(kNedListAttic2);
	const ExeImage &exe = _vm->getExe();
	uint32 room = 0x497938 + kAtticStartRoom * 0x54;
	_scroll.x = kAtticStartRoom == 2 ? -0xd8d : -0x189 - exe.readSint32(room + 0x10);
	_scroll.y = -exe.readSint32(room + 0x14);
	_nedPos.x = -0x48 - _scroll.x;
	_nedPos.y = 0;
	int top = exe.readSint32(room + 0x14);
	for (int y = (int16)exe.readUint16(room + 0x1c); y >= top; y--) {
		if (!_collision.empty() && _collision[0]->isSolid((int16)_nedPos.x, y)) {
			_nedPos.y = y;
			break;
		}
	}
	ned->setFacing(1);
	ned->resetIdle();
	ned->setWrap1(false, 0x1e58, xform, this);
	ned->show(true);
	ned->enable(true);
	debugC(1, kDebugGraphics, "Ned starts at %d,%d, scroll %d,%d", _nedPos.x, _nedPos.y, _scroll.x, _scroll.y);
}

// FUN_004039e0: world -> screen in the attic
void PlacementDemo::xform(void *user, int &x, int &y) {
	PlacementDemo *demo = (PlacementDemo *)user;
	x += demo->_scroll.x;
	y += demo->_scroll.y;
}

// Stands in for the attic camera (0x407c50): keeps Ned's screen position inside a band
void PlacementDemo::follow() {
	int sx = _nedPos.x + _scroll.x, sy = _nedPos.y + _scroll.y;
	int dx = 0, dy = 0;
	if (sx < kFollowLeft)
		dx = kFollowLeft - sx;
	else if (sx > kFollowRight)
		dx = kFollowRight - sx;
	if (sy < kFollowTop)
		dy = kFollowTop - sy;
	else if (sy > kFollowBottom)
		dy = kFollowBottom - sy;
	if (!dx && !dy)
		return;
	_scroll.x += dx;
	_scroll.y += dy;
	_game->ned()->adjPosition(dx, dy);
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

	// 0x404f10: ned_tick, then the camera
	Ned *ned = _game->ned();
	if (ned) {
		ned->tick(_world, input, _facing, _nedPos, now, _scroll.x, _scroll.y, nullptr);
		follow();
		debugC(3, kDebugGraphics, "Ned input %x state %d slot %d step %d at %d,%d facing %d", input, ned->getState(),
			ned->getCurSlot(), ned->getSlotStep(ned->getCurSlot()), _nedPos.x, _nedPos.y, ned->getFacing());
	}

	place(now, true);
	_display->setCollisionOverlay(_collision, _scroll);
	_display->draw();
	return true;
}

} // End of namespace NightmareNed
