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

#include "common/config-manager.h"
#include "common/debug.h"
#include "common/system.h"

#include "nightmarened/detection.h"
#include "nightmarened/display.h"
#include "nightmarened/game.h"
#include "nightmarened/hints.h"
#include "nightmarened/media.h"
#include "nightmarened/ned.h"
#include "nightmarened/nightmarened.h"
#include "nightmarened/placements.h"
#include "nightmarened/rdx.h"
#include "nightmarened/scene6.h"
#include "nightmarened/scene6_rooms.h"

namespace NightmareNed {

enum {
	kMapPlayroom = 0x4beef4,   // cbix 1
	kMapFinale = 0x4beef8,     // cbix 2, once every monster is beaten
	kMapVisited = 0x4bdd70,    // cbix 3, once every room is visited and none beaten
	kBmpRoom = 0x4e21dc,       // bitmap 1 (palette DAT_004befd0)
	kBmpFront = 0x4e21e0,      // bitmap 2, colour key 10
	kBmpPalette = 0x4e21fc,    // bitmap 9: palette only (DAT_004be6e8)
	kPalRoom = 0x4befd0,
	kPalMain = 0x4be6e8,
	kPalFinale = 0x4bde98,     // bitmaps 10 and 0xf
	kClipToAttic = 0xf,        // 8715tv01
	kClipToScene3 = 0x2e       // 8746tv01
};

Scene6::Scene6(Game *game) : Scene(game, 6) {
	const ExeImage &exe = _game->exe();
	_boxSound = exe.readSint32(0x4a1bf8);
	_sound133 = exe.readSint32(0x4a1bfc);
	_sound134 = exe.readSint32(0x4a1c00);
	_sound6b = exe.readSint32(0x4a1c04);
	_sound6d = exe.readSint32(0x4a1c08);
	_slideAX = exe.readSint32(0x4a1c10);
	_slideAY = exe.readSint32(0x4a1c14);
	_slideBX = exe.readSint32(0x4a1c18);
	_slideBY = exe.readSint32(0x4a1c1c);

	for (int i = 0; i < kSavedCount; i++)
		_saved[i] = 0;
	_vehicleHomeX = 0;
	_timeUp = _beaten = _effect133 = _effect134 = 0;
	_unportedExit = -1;
	_built.resize(0x32);
	_builtOk.resize(0x32);
	for (uint i = 0; i < _builtOk.size(); i++)
		_builtOk[i] = false;
	_builtCount = 0;
	_lastTick = 0;
	_preparing = false;
	_boxSlot = 0;
	_entry0 = _entry1 = _entry2 = _entry3 = 0;
	_selected = -1;
	memset(_palMain, 0, sizeof(_palMain));
	_holeOpen = 0;
	_entryIndex = 0;
	_state = 0;
	_sharedLoaded = 0;
	_finale = _allWon = _allVisited = 0;
	_toAttic = _toScene3 = 0;
	_sharedShown = _hintDone = 0;
	_timer1 = _timer2 = _timer3 = 0;

	_hooks.user = this;
	_hooks.use = hookUse;
	_hooks.region = hookRegion;
	_hooks.findTarget = hookFindTarget;
	_hooks.hitTarget = hookHitTarget;

	// 0x434830: the four rooms
	addSubScene(new Scene6Room0(game));
	addSubScene(new Scene6Room1(game));
	addSubScene(new Scene6Room2(game));
	addSubScene(new Scene6Room3(game));
}

Scene6::~Scene6() {
	freeAll();
}

// m0 0x430d60 / m1 0x430d90. Test hook: nightmarened_s6_progress=<9 digits> sets the
// saved block (won0 visited0 won1 visited1 won2 visited2 won3 visited3 boxOpen)
int Scene6::init() {
	for (int i = 0; i < kSavedCount; i++)
		_saved[i] = 0;
	if (ConfMan.hasKey("nightmarened_s6_progress")) {
		Common::String p = ConfMan.get("nightmarened_s6_progress");
		for (uint i = 0; i < p.size() && i < kSavedCount; i++)
			_saved[i] = p[i] == '1';
	}
	return 0;
}

void Scene6::shutdown() {
	for (int i = 0; i < kSavedCount; i++)
		_saved[i] = 0;
}

// m9 0x4347b0
void Scene6::saveState(int32 *out) {
	for (int i = 0; i < kSavedCount; i++)
		out[i] = _saved[i];
}

// m10 0x4347d0
void Scene6::restoreState(const int32 *in, int size) {
	for (int i = 0; i < kSavedCount; i++)
		_saved[i] = (size == 0x24 && in) ? in[i] : 0;
}

void Scene6::closeAll(bool stopMusic) {
	release(kSharedList, (int16)_game->exe().readUint16(kSharedCount));
	if (stopMusic)
		_game->media().stopMusic();
	_game->closeNaf(kNafPlayroom);
}

// m2 0x430f40: the list is built from the progress: a token per room visited (another
// once beaten), or the finale's pieces
int Scene6::load(int fromScene, int fromSub) {
	_selected = -1;
	_allWon = 0;
	_allVisited = 0;
	_holeOpen = 0;
	_toAttic = 0;
	_toScene3 = 0;
	if (fromScene != 6 || fromSub == -1) {
		if (!_game->getNaf(kNafPlayroom))
			return 5;
		ned()->listLoad(kNedList);
		_sharedLoaded = 0;
		_sharedShown = 0;
	}
	for (int i = 0; i < 0x32; i++) {
		if (!_builtOk[i])
			_built[i] = Request();
	}
	bool won0 = _saved[kWon0], won1 = _saved[kWon1], won2 = _saved[kWon2], won3 = _saved[kWon3];
	bool vis0 = _saved[kVisited0], vis1 = _saved[kVisited1], vis2 = _saved[kVisited2], vis3 = _saved[kVisited3];
	bool all = vis2 && vis1 && vis0 && vis3 && won2 && won1 && won0 && won3;
	bool visitedOnly = vis2 && vis1 && vis0 && vis3 && !won2 && !won1 && !won0 && !won3;
	if (all)
		_builtCount = 0x1a;
	else if (visitedOnly)
		_builtCount = 0x16;
	else
		_builtCount = 0x11 + (vis2 ? 1 : 0) + (vis1 ? 1 : 0) + (vis0 ? 1 : 0) + (vis3 ? 1 : 0);

	setEntry(0, -1, 1, Request::kCollision, kMapPlayroom, 0);
	setEntry(1, -1, 2, Request::kCollision, kMapFinale, 0);
	setEntry(2, -1, 3, Request::kCollision, kMapVisited, 0);
	setEntry(3, -1, 1, Request::kBitmap, kBmpRoom, kPalRoom);
	setEntry(4, 10, 2, Request::kBitmap, kBmpFront, 0);
	setEntry(5, -1, 9, Request::kBitmap, kBmpPalette, kPalMain);
	setEntry(6, kArea, 0x2c, Request::kSprite, 0, 0);
	setEntry(7, kArea, 0x2d, Request::kSprite, 0, 0);
	setEntry(8, kArea, 0x2d, Request::kSound, 0, 0);
	setEntry(9, kArea, 0x33, Request::kSprite, 0, 0);
	setEntry(10, kArea, 0x33, Request::kSound, 0, 0);
	setEntry(11, kArea, 0x36, Request::kSprite, 0, 0);
	setEntry(12, kArea, 0x37, Request::kSprite, 0, 0);
	setEntry(13, kArea, 0xf9, Request::kSprite, 0, 0);
	setEntry(14, kArea, 0x2e, Request::kSprite, 0, 0);
	setEntry(15, kArea, 0x2f, Request::kSprite, 0, 0);
	setEntry(16, kArea, 0x2f, Request::kSound, 0, 0);

	if (all) {
		_allWon = 1;
		_entryIndex = 0x11;
		setEntry(0x11, -1, 10, Request::kBitmap, 0x4e29ac, kPalFinale);
		setEntry(0x12, kArea, 0x6a, Request::kSprite, 0, 0);
		setEntry(0x13, kArea, 0x6b, Request::kSprite, 0, 0);
		setEntry(0x14, kArea, 0x6b, Request::kSound, 0, 0);
		setEntry(0x15, kArea, 0x6c, Request::kSprite, 0, 0);
		setEntry(0x16, kArea, 0x6d, Request::kSprite, 0, 0);
		setEntry(0x17, kArea, 0x6d, Request::kSound, 0, 0);
		setEntry(0x18, 10, 0xd, Request::kBitmap, 0x4e29b8, 0);
		setEntry(0x19, 10, 0x18, Request::kBitmap, 0x4e2238, 0);
	} else if (visitedOnly) {
		_allVisited = 1;
		_entryIndex = 0x11;
		setEntry(0x11, -1, 0xf, Request::kBitmap, 0x4e2b3c, kPalFinale);
		setEntry(0x12, 10, 0x14, Request::kBitmap, 0x4e2228, 0);
		setEntry(0x13, 10, 0x15, Request::kBitmap, 0x4e222c, 0);
		setEntry(0x14, 10, 0x16, Request::kBitmap, 0x4e2230, 0);
		setEntry(0x15, 10, 0x17, Request::kBitmap, 0x4e2234, 0);
	} else {
		int16 index = 0x10;
		if (vis2) {
			index = 0x11;
			_entry2 = index;
			if (won2)
				setEntry(index, 10, 0xc, Request::kBitmap, 0x4e29b4, 0);
			else
				setEntry(index, 10, 0x11, Request::kBitmap, 0x4e2b44, 0);
		}
		if (vis1) {
			_entry1 = ++index;
			if (won1)
				setEntry(index, 10, 0xb, Request::kBitmap, 0x4e29b0, 0);
			else
				setEntry(index, 10, 0x10, Request::kBitmap, 0x4e2b40, 0);
		}
		if (vis0) {
			_entry0 = ++index;
			if (won0)
				setEntry(index, 10, 5, Request::kBitmap, 0x4e21ec, 0);
			else
				setEntry(index, 10, 0x12, Request::kBitmap, 0x4e2b48, 0);
		}
		if (vis3) {
			_entry3 = ++index;
			if (won3)
				setEntry(index, 10, 0xe, Request::kBitmap, 0x4e29bc, 0);
			else
				setEntry(index, 10, 0x13, Request::kBitmap, 0x4e2b4c, 0);
		}
	}
	return submitBuilt();
}

// m3 0x431980
int Scene6::enter(int fromScene, int fromSub) {
	bool fromRoom = fromScene == 6 && fromSub != -1;
	if (!fromRoom)
		ned()->listShow(kNedList);
	ned()->setWrap1(false, 0, nullptr, nullptr);
	// 0x45a560(3, 999, 0): 96999000.SST
	if (!loadSceneSst(_game, 3, 999, 0, _sst))
		return 5;
	applyPlacements(_sst);
	if (!builtLoaded(2))
		return 5;
	_world = CollisionWorld();
	_userMap = CollisionMap();
	_world.setUser(&_userMap);
	CollisionMap *floor = map(_allWon ? kMapFinale : (_allVisited ? kMapVisited : kMapPlayroom));
	if (floor)
		_world.addMap(floor, 0, 0, 0x27f, 0x1df);
	ned()->show(false);
	ned()->resetIdle();
	_game->res().getSlot(1, 4).offsets[0] = 6; // DAT_004c7324
	_nedPos.x = 0xfa;
	for (int y = 0x1df; y >= 0; y--) {
		if (floor && floor->isSolid((int16)_nedPos.x, (int16)y)) {
			_nedPos.y = y;
			break;
		}
	}
	int f = 0;
	ned()->tick(_world, 0, f, _nedPos, 0, 0, 0, &_hooks);

	RdxSurface *primary = _game->display().getPrimary();
	if (!builtLoaded(3))
		return 5;
	BitmapRec *room = bmp(kBmpRoom);
	if (room && room->object) {
		room->object->setDrawOrder(200);
		room->object->setDestination(primary);
		room->object->setVisibility(true);
	}
	if (!builtLoaded(4))
		return 5;
	showBuilt(4, kBmpFront, kNedZ - 1, true);
	if (!builtLoaded(5) || !builtLoaded(6))
		return 5;
	attach(0x2c, kNedZ + 1);
	visible(0x2c, false);
	_vehicleHomeX = slot(0x2c).x;
	if (!builtLoaded(7))
		return 5;
	attach(0x2d, kNedZ + 1);
	slot(0x2d).step = -1;
	visible(0x2d, false);
	if (!builtLoaded(9))
		return 5;
	attach(0x33, kNedZ - 1);
	slot(0x33).step = -1;
	visible(0x33, false);
	if (!builtLoaded(11))
		return 5;
	attach(0x36, kNedZ + 1);
	visible(0x36, false);
	if (!builtLoaded(12))
		return 5;
	attach(0x37, kNedZ + 1);
	slot(0x37).step = -1;
	visible(0x37, false);
	place(0x37, 0x36);
	if (!builtLoaded(13))
		return 5;
	attach(0xf9, kNedZ + 1);
	slot(0xf9).step = 0;
	place(0xf9, 0xf9);
	if (!builtLoaded(14))
		return 5;
	attach(0x2e, kNedZ + 1);
	slot(0x2e).step = -1;
	visible(0x2e, false);
	if (!builtLoaded(15))
		return 5;
	attach(0x2f, kNedZ + 1);
	slot(0x2f).step = -1;
	visible(0x2f, false);

	_targets.clear();
	if (!_saved[kBoxOpen]) {
		_state = 1;
		switchSlot(0x2d, -1);
		place(0x2d, 0x2c);
		targetAdd(_targets, 0x2d);
		targetAdd(_targets, 0xf9);
	} else {
		_selected = -1;
		_state = fromScene != 6 ? 3 : 5;
		switchSlot(0x2e, -1);
		place(0x2e, 0x2c);
		targetAdd(_targets, 0x2d);
		targetAdd(_targets, 0xf9);
		targetRemove(_targets, 0x2d);
		targetAdd(_targets, 0x2f);
	}
	visible(0x33, true);
	visible(0x36, false);
	visible(0x37, true);

	// The tokens
	_finale = 0;
	bool won0 = _saved[kWon0], won1 = _saved[kWon1], won2 = _saved[kWon2], won3 = _saved[kWon3];
	bool vis0 = _saved[kVisited0], vis1 = _saved[kVisited1], vis2 = _saved[kVisited2], vis3 = _saved[kVisited3];
	if (vis2 && vis1 && vis0 && vis3 && won2 && won1 && won0 && won3) {
		// Every monster beaten: the finale pieces
		_finale = 1;
		if (!builtLoaded(_entryIndex))
			return 5;
		showBuilt(_entryIndex++, 0x4e29ac, kNedZ + 2, false);
		if (!builtLoaded(_entryIndex++))
			return 5;
		attach(0x6a, kNedZ + 1);
		slot(0x6a).step = -1;
		visible(0x6a, false);
		if (!builtLoaded(_entryIndex++))
			return 5;
		attach(0x6b, kNedZ + 1);
		place(0x6b, 0x6a);
		slot(0x6b).step = -1;
		visible(0x6b, true);
		_entryIndex++;
		if (!builtLoaded(_entryIndex++))
			return 5;
		attach(0x6c, kNedZ + 1);
		slot(0x6c).step = -1;
		visible(0x6c, false);
		if (!builtLoaded(_entryIndex++))
			return 5;
		attach(0x6d, kNedZ + 1);
		slot(0x6d).step = 0;
		place(0x6d, 0x6c);
		slot(0x6d).step = -1;
		visible(0x6d, true);
		_entryIndex++;
		if (!builtLoaded(_entryIndex++))
			return 5;
		showBuilt(_entryIndex - 1, 0x4e29b8, kNedZ - 1, false);
		if (!builtLoaded(_entryIndex))
			return 5;
		showBuilt(_entryIndex, 0x4e2238, kNedZ - 1, false);
	} else if (vis2 && vis1 && vis0 && vis3 && !won2 && !won1 && !won0 && !won3) {
		// Every room visited, no monster beaten: the floor gives way
		_finale = 1;
		if (!builtLoaded(_entryIndex))
			return 5;
		showBuilt(_entryIndex++, 0x4e2b3c, kNedZ + 2, false);
		if (!builtLoaded(_entryIndex))
			return 5;
		BitmapRec *a = bmp(0x4e2228);
		if (a && a->object) {
			a->object->setDrawOrder(kNedZ + 1);
			a->object->setDestination(primary);
			a->object->setPosition(0, 0xf5);
			a->object->setVisibility(true);
		}
		_entryIndex++;
		if (!builtLoaded(_entryIndex))
			return 5;
		BitmapRec *b = bmp(0x4e222c);
		if (b && b->object) {
			b->object->setDrawOrder(kNedZ - 1);
			b->object->setDestination(primary);
			b->object->setPosition(0, 0xc8);
			b->object->setVisibility(true);
		}
		_entryIndex++;
		if (!builtLoaded(_entryIndex))
			return 5;
		showBuilt(_entryIndex++, 0x4e2230, kNedZ, false);
		if (!builtLoaded(_entryIndex))
			return 5;
		showBuilt(_entryIndex, 0x4e2234, kNedZ + 1, false);
	} else {
		if (vis2) {
			if (!builtLoaded(_entry2))
				return 5;
			showBuilt(_entry2, won2 ? 0x4e29b4 : 0x4e2b44, kNedZ + 2, true);
		}
		if (vis1) {
			if (!builtLoaded(_entry1))
				return 5;
			showBuilt(_entry1, won1 ? 0x4e29b0 : 0x4e2b40, kNedZ + 2, true);
		}
		if (vis0) {
			if (!builtLoaded(_entry0))
				return 5;
			showBuilt(_entry0, won0 ? 0x4e21ec : 0x4e2b48, kNedZ + 2, true);
		}
		if (vis3) {
			if (!builtLoaded(_entry3))
				return 5;
			showBuilt(_entry3, won3 ? 0x4e29bc : 0x4e2b4c, kNedZ + 2, true);
		}
	}

	if (palette(kPalMain))
		memcpy(_palMain, palette(kPalMain), sizeof(_palMain));
	ned()->copyPalette(_palMain);
	_game->display().applyPalette(_palMain, kPalAll);
	const byte *upper = palette((_allWon || _allVisited) ? kPalFinale : kPalRoom);
	if (upper)
		_game->display().applyPalette(upper, kPalUpper);
	if (_finale) {
		BitmapRec *front = bmp(kBmpFront);
		if (front && front->object)
			front->object->setVisibility(false);
		visible(0x37, false);
		visible(0x33, false);
		visible(0x2e, false);
		visible(0xf9, false);
		_state = _allWon ? 6 : 9;
	}
	if (!_sharedLoaded && submit(kSharedList, (int16)_game->exe().readUint16(kSharedCount), 0))
		return 5;
	if (fromRoom) {
		openWipe();
		freeWipe();
	} else {
		_game->media().playMusic(kMusicPlayroom);
	}
	ned()->show(true);
	ned()->enable(true);
	_game->_optionsAllowed = true;
	return 0;
}

// m4 0x430eb0
int Scene6::leave(int toScene, int toSub) {
	// 0x455ca0 flushes the loader queues; 0x45a5f0 frees the SST list
	_sst.clear();
	if (toScene == 6) {
		if (toSub != -1)
			loadWipe();
		return 0;
	}
	ned()->listFree(kNedList);
	if (toScene == 4)
		_game->media().loadClip(kClipToAttic);
	else if (toScene == 3)
		_game->media().loadClip(kClipToScene3);
	return 0;
}

void Scene6::keepVehicleAnchor() {
	if (slot(0x2c).boxes && slot(0x2c).boxes != &_staleBoxes)
		_staleBoxes = *slot(0x2c).boxes;
}

// Test hooks only (nightmarened_scene=6:<room>): what the playroom has loaded when it
// opens a room, which the original never skips
void Scene6::prepareRoomDirect() {
	if (_sharedLoaded)
		return;
	if (load(-1, -1))
		return;
	ned()->listShow(kNedList);
	ned()->setWrap1(false, 0, nullptr, nullptr);
	_nedPos.x = 0xfa;
	if (loadSceneSst(_game, 3, 999, 0, _sst))
		applyPlacements(_sst);
	_sst.clear();
	_vehicleHomeX = slot(0x2c).x;
	if (palette(kPalMain))
		memcpy(_palMain, palette(kPalMain), sizeof(_palMain));
	ned()->copyPalette(_palMain);
	_game->display().applyPalette(_palMain, kPalAll);
	keepVehicleAnchor();
	releaseBuilt();
	slot(0x2c).boxes = &_staleBoxes;
	slot(0x2c).alias = true;
	submit(kSharedList, (int16)_game->exe().readUint16(kSharedCount), 0);
	_state = 0;
	_sharedShown = 0;
	_preparing = true;
	boxTick(0);
	_preparing = false;
	loadWipe();
	_game->media().playMusic(kMusicPlayroom);
}

// m5 0x430dc0
int Scene6::unload(int toScene, int toSub) {
	if (toScene == 6 && toSub != -1)
		closePlayroom();
	_world = CollisionWorld();
	ned()->show(false);
	ned()->enable(false);
	stopAllSounds();
	keepVehicleAnchor();
	releaseBuilt();
	if (toScene == 6) {
		// The rooms still place the vehicle against the freed slot 0x2c, whose hit boxes
		// the original reads after freeing them: they stay readable here
		Slot &home = slot(0x2c);
		home.boxes = &_staleBoxes;
		home.alias = true;
		return 0;
	}
	closeAll(false);
	if (toScene == 4)
		_game->media().playClipWithMusic(1, kClipToAttic);
	else if (toScene == 3)
		_game->media().playClipWithMusic(1, kClipToScene3);
	return 0;
}

// 0x432bc0: the shared slots, set up the first time the box is used
static const int16 kCounterSlots[] = { 0x132, 0x136, 0x137 };

int Scene6::boxTick(uint32 now) {
	bool holeDone = false;
	Slot &hole = slot(0xf9);
	if (_holeOpen && hole.deadline < now) {
		hole.step++;
		holeDone = hole.sequenceLength <= hole.step;
		if (holeDone)
			hole.step = hole.sequenceLength - 1;
		hole.deadline = now + (int16)place(0xf9, 0xf9) * kMsPerStep;
	}
	if (holeDone) {
		switchTo(3, -1);
		return 1;
	}

	switch (_state) {
	case 0:
		// 0x432bc0
		if (!_sharedShown) {
			attach(0x31, kNedZ);
			place(0x31, 0x2c);
			slot(0x31).step = -1;
			visible(0x31, false);
			attach(0x12e, kNedZ);
			slot(0x12e).step = -1;
			visible(0x12e, false);
			for (int16 id : kCounterSlots) {
				attach(id, kNedZ);
				visible(id, false);
			}
			attach(0x133, kNedZ);
			visible(0x133, false);
			if (slot(0x133).sprite)
				slot(0x133).sprite->setPosition(0x62, 0x7b);
			attach(0x134, kNedZ);
			visible(0x134, false);
			if (slot(0x134).sprite)
				slot(0x134).sprite->setPosition(0xc5, 0x2f);
			if (slot(0x12e).sprite)
				slot(0x12e).sprite->setPosition(5, 10);
			for (int16 id : kCounterSlots) {
				if (slot(id).sprite)
					slot(id).sprite->setPosition(0xfa, 10);
			}
			_sharedShown = 1;
		}
		if (_selected == -1 || _preparing)
			return 1;
		switchTo(6, _selected);
		return 1;
	case 1: {
		// The closed box
		_boxSlot = 0x2d;
		if (_boxSound) {
			playSound(0x2d);
			_boxSound = 0;
		}
		Slot &s = slot(0x2d);
		if (s.deadline < now) {
			s.step++;
			if (s.sequenceLength <= s.step) {
				s.step = 0;
				_boxSound = 1;
			}
			s.deadline = now + (int16)place(0x2d, 0x2c) * kMsPerStep;
			return 0;
		}
		break;
	}
	case 2:
		_boxSlot = 0x2f;
		spinnerTick(now);
		return 0;
	case 3: {
		// The box opens, then the spinner
		_boxSlot = 0x2e;
		Slot &s = slot(0x2e);
		if (s.deadline < now) {
			s.step++;
			if (s.sequenceLength <= s.step) {
				s.step = 0;
				_boxSlot = switchSlot(0x2f, _boxSlot);
				place(0x2f, 0x2c);
				_state = 2;
			}
			s.deadline = now + (int16)place(0x2e, 0x2c) * kMsPerStep;
			return 0;
		}
		break;
	}
	case 4:
		_boxSlot = 0x2d;
		_state = 0;
		return 0;
	case 5:
		_boxSlot = 0x2d;
		visible(0x31, false);
		_state = 3;
		return 0;
	case 6: {
		// The finale, first part
		_boxSlot = 0x6b;
		if (_sound6b) {
			playSound(0x6b);
			_sound6b = 0;
		}
		Slot &s = slot(0x6b);
		if (s.deadline < now) {
			s.step++;
			if (s.sequenceLength <= s.step) {
				s.step = 0;
				_state = 7;
				_sound6b = 1;
			}
			s.deadline = now + (int16)place(0x6b, 0x6a) * kMsPerStep;
			return 0;
		}
		break;
	}
	case 7: {
		_boxSlot = 0x6d;
		if (_sound6d) {
			playSound(0x6d);
			_sound6d = 0;
		}
		Slot &s = slot(0x6d);
		if (s.deadline < now) {
			s.step++;
			if (s.sequenceLength <= s.step) {
				s.step = s.sequenceLength - 1;
				_state = 10;
				_sound6d = 1;
			}
			s.deadline = now + (int16)place(0x6d, 0x6c) * kMsPerStep;
			return 0;
		}
		break;
	}
	case 8:
		_boxSlot = 0x14;
		switchTo(3, -1);
		return 1;
	case 9: {
		// Every room visited, none beaten: the two floor pieces slide up, then scene 3
		_boxSlot = 0x14;
		_slideAY -= 6;
		if (_slideAY < 1)
			_slideAY = 0;
		BitmapRec *a = bmp(0x4e2228);
		if (a && a->object)
			a->object->setPosition(_slideAX, _slideAY);
		_slideBY -= 3;
		if (_slideBY < 1)
			_slideBY = 0;
		BitmapRec *b = bmp(0x4e222c);
		if (b && b->object)
			b->object->setPosition(_slideBX, _slideBY);
		if (_slideAY == 0 && _slideBY == 0) {
			_state = 8;
			playSound(0x16);
			return 0;
		}
		break;
	}
	case 10:
		_boxSlot = 0x6d;
		break;
	default:
		break;
	}
	return 0;
}

// 0x4336a0: the spinner; the step it is on picks the room (two steps each)
void Scene6::spinnerTick(uint32 now) {
	Slot &s = slot(0x2f);
	if (s.deadline >= now)
		return;
	s.step++;
	if (s.sequenceLength <= s.step)
		s.step = 0;
	int duration = place(0x2f, 0x2c);
	switch (s.step) {
	case 0:
		_selected = 1;
		break;
	case 1:
	case 7:
	case 0xd:
	case 0x13:
		playSound(0x2f);
		break;
	case 6:
		_selected = 2;
		break;
	case 0xc:
		_selected = 0;
		break;
	case 0x12:
		_selected = 3;
		break;
	default:
		_selected = -1;
		break;
	}
	s.deadline = now + (int16)duration * kMsPerStep;
}

// m7 0x432e20. The original has no throttle (it runs and draws as fast as srfDraw
// allows); here a tick runs at most every 41 ms.
int Scene6::tick(uint32 input, uint32 now) {
	if (now < _lastTick + kMsPerStep) {
		g_system->delayMillis(MIN<uint32>(_lastTick + kMsPerStep - now, 10));
		return 0;
	}
	_lastTick = now;
	if (!_finale) {
		Slot &a = slot(0x33);
		if (a.deadline < now) {
			a.step++;
			if (a.step == 7)
				playSound(0x33);
			if (a.sequenceLength <= a.step)
				a.step = 0;
			a.deadline = now + (int16)place(0x33, 0x33) * kMsPerStep;
		}
		Slot &b = slot(0x37);
		if (b.deadline < now) {
			b.step++;
			if (b.sequenceLength <= b.step)
				b.step = 0;
			b.deadline = now + (int16)place(0x37, 0x36) * kMsPerStep;
		}
	}
	if (boxTick(now) == 0) {
		int f = 0;
		ned()->tick(_world, input, f, _nedPos, now, 0, 0, &_hooks);
		draw(overlayMaps());
		debugC(3, kDebugGraphics, "Scene 6: input %x Ned %d,%d state %d slot %d box %d selected %d", input,
			_nedPos.x, _nedPos.y, ned()->getState(), ned()->getCurSlot(), _state, _selected);
	}
	if (_allWon && !_hintDone) {
		_game->hints().trigger(2, ned()->getFacing() != 0, _nedPos.x, _nedPos.y, nullptr, nullptr);
		_hintDone = 1;
	}
	if (_toAttic) {
		if (!switchTo(4, -1))
			return 0;
		_toAttic = 0;
	}
	if (_toScene3) {
		if (!switchTo(3, -1))
			return 0;
		_toScene3 = 0;
	}
	return 0;
}

Common::Array<CollisionMap *> Scene6::overlayMaps() const {
	Common::Array<CollisionMap *> maps;
	for (int i = 0; i < _world.getMapCount(); i++)
		maps.push_back(_world.getMap(i));
	maps.push_back(const_cast<CollisionMap *>(&_userMap));
	return maps;
}

// ---- Hooks (DAT_004be6a0) ----

// 0x433000 (+0x00): button 2 aims the zap at the target in front of Ned (0x433980)
int Scene6::hookUse(void *user, int x, int y, int16 *state) {
	Scene6 *s = (Scene6 *)user;
	int16 dir = 0x2f, found = -1;
	int32 rect[4] = { 0, 0, 0, 0 };
	s->findTarget(s->_targets, dir, found, rect, false);
	s->ned()->setUseSlot(dir);
	return 1;
}

// 0x432fc0 (+0x04): region 3 leads to the attic, 4 to scene 3; any other takes state 0xe
int Scene6::hookRegion(void *user, int32 id, uint32 flags, int x, int y, int16 *state) {
	Scene6 *s = (Scene6 *)user;
	if (id == 3) {
		s->_toAttic = 1;
		return 0;
	}
	if (id == 4) {
		s->_toScene3 = 1;
		return 0;
	}
	*state = 0xe;
	return 1;
}

// 0x433140 (+0x08): area 2; 0x433980 writes its direction over this hook's own argument
void Scene6::hookFindTarget(void *user, int x, int y, int16 *a, int16 *b, int32 rect[4]) {
	Scene6 *s = (Scene6 *)user;
	*a = 2;
	int16 dir = 0;
	s->findTarget(s->_targets, dir, *b, rect, false);
}

// 0x433050 (+0x0c)
int Scene6::hookHitTarget(void *user, int x, int y, int16 *a, int16 *b, const int32 rect[4]) {
	Scene6 *s = (Scene6 *)user;
	switch (*b) {
	case 0x2d:
		// The box opens for good
		s->_saved[kBoxOpen] = 1;
		s->_selected = -1;
		s->_state = 3;
		s->_boxSlot = s->switchSlot(0x2e, s->_boxSlot);
		s->place(0x2e, 0x2c);
		targetRemove(s->_targets, 0x2d);
		targetAdd(s->_targets, 0x2f);
		break;
	case 0x2f:
		if (s->_selected != -1)
			s->_state = 4;
		break;
	case 0xf9:
		s->_holeOpen = 1;
		targetRemove(s->_targets, 0xf9);
		break;
	default:
		break;
	}
	return 1;
}

} // End of namespace NightmareNed
