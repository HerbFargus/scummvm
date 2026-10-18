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
#include "common/system.h"

#include "nightmarened/display.h"
#include "nightmarened/media.h"
#include "nightmarened/menus.h"
#include "nightmarened/nightmarened.h"
#include "nightmarened/rdx.h"
#include "nightmarened/scene1.h"
#include "nightmarened/scene1_graves.h"

namespace NightmareNed {

int GraveRoom::_savedFacing = 0;

GraveRoom::GraveRoom(Game *game, int id, Scene1 *top) : GraveyardRoom(game, id, top), _grid(nullptr),
		_scrollX(0), _scrollY(0), _soundCbSlot(-1), _soundCb(0) {
	for (int i = 0; i < 3; i++)
		_wallX[i] = _wallY[i] = 0;
	_hooks.user = this;
	_hooks.use = nullptr;
	_hooks.region = nullptr;
	_hooks.findTarget = nullptr;
	_hooks.hitTarget = nullptr;
}

GraveRoom::~GraveRoom() {
	shaftFree();
}

// ---- Shared code ----

// 0x441790
void GraveRoom::dropVideo() {
	top()->ambientEnable(false);
	ned()->resetIdle();
	ned()->show(false);
	top()->backdropVisible(false);
	MenuVideo video;
	video.open(_game, 0x48);
	int frames = video.getFrameCount();
	if (wait(kVideoList) != 0)
		return;
	BitmapRec *rec = bmp(kBmpVideo);
	if (rec && rec->object) {
		rec->object->setDrawOrder(99);
		rec->object->setDestination(_game->display().getPrimary());
	}
	int fps = MAX<int>(_game->media()._transFps, 1);
	uint32 next = 0;
	uint16 frame = 0;
	while ((int)frame < frames && !_game->getEngine()->shouldQuit()) {
		uint32 now = _game->now();
		if (next < now) {
			top()->cloudsDrift(now);
			if (rec)
				video.decodeFrame(frame, rec->bitmap.surface);
			next = 1000 / fps + now;
			frame++;
			draw();
			uint32 after = _game->now();
			if (after < next)
				g_system->delayMillis(next - after);
		}
		_game->getEngine()->pumpEvents();
	}
	video.close();
}

// 0x441900
void GraveRoom::fallShaft() {
	top()->layersVisible(false);
	RdxObject *topBmp = bkg(kBmpTop);
	int topY = 0x1e0;
	if (topBmp) {
		topBmp->setPosition(0, 0x1e0);
		topBmp->setVisibility(true);
		topBmp->setDrawOrder(0x50);
	}
	int16 stage = 0;          // the wall creature passed
	int videoY = 0;
	_wallX[1] = 0x17c;
	int gridY = (int16)(bmpHeight(kBmpTop) + 0x1e0);
	_wallX[0] = 100;
	_wallY[0] = gridY + 0xdc;
	_wallX[2] = 100;
	_wallY[1] = gridY + 0x208;
	_wallY[2] = gridY + 0x370;
	for (int i = 0; i < 9; i++)
		setOrder(kArea, wallSlot(i), 0x4b);
	if (_grid) {
		_grid->setPosition(0, gridY);
		_grid->setVisibility(true);
	}
	switchSlotAt(kNedArea, kNedFall, -1);
	visible(wallSlot(1), true);
	visible(wallSlot(4), true);
	visible(wallSlot(7), true);
	setPos(wallSlot(1), _wallX[0], _wallY[0]);
	setPos(wallSlot(3), _wallX[1], _wallY[1]);
	// The original places slot 31 at the second creature's spot too
	setPos(wallSlot(6), _wallX[1], _wallY[1]);
	int nedX = _wallX[0];
	int noMirror = 1;
	int nedY = -100;
	ensureFlip(kNedArea, kNedFlip417);
	flipActive(kNedArea, kNedFlip417, false);
	flipActive(kNedArea, kNedFall, false);
	int tumbling = 0;         // the tumble slot plays to its end before the next creature counts
	int16 anim = 0;           // the creature that bit
	int16 nedSlot = kNedFall;
	int16 dur = 0;            // one step-duration local the original shares
	int frame = 0;
	Common::Point pos;
	int16 bottom = 0;
	BitmapRec *video = bmp(kBmpVideo);
	while (gridY > 0x1e0 - kShaftH && !_game->getEngine()->shouldQuit()) {
		uint32 now = _game->now();
		if (nedY < 0x48)
			nedY += 8;
		_wallY[1] -= 10;
		_wallY[0] -= 10;
		videoY -= 10;
		gridY -= 10;
		_wallY[2] -= 10;
		topY -= 10;
		if (topBmp)
			topBmp->setPosition(0, topY);
		if (video && video->object)
			video->object->setPosition(0, videoY);
		if (_grid)
			_grid->setPosition(0, gridY);
		setPos(wallSlot(1), _wallX[0], _wallY[0]);
		setPos(wallSlot(4), _wallX[1], _wallY[1]);
		setPos(wallSlot(7), _wallX[2], _wallY[2]);

		Slot &ns = slotAt(kNedArea, nedSlot);
		if (ns.deadline < now) {
			ns.step++;
			if (ns.step >= ns.sequenceLength) {
				ns.step = 0;
				if (tumbling && nedSlot == kNedTumble) {
					int x;
					if (stage == 1 || stage == 3) {
						noMirror = 0;
						x = nedX + 0x140;
					} else {
						noMirror = 1;
						x = _wallX[0];
					}
					nedY += 0x50;
					nedX = x;
					pos = Common::Point(x, nedY);
					slotAt(kNedArea, kNedBase).x = x;
					slotAt(kNedArea, kNedBase).y = nedY;
					nedSlot = switchSlotAt(kNedArea, kNedFall, kNedTumble);
					tumbling = 0;
				}
			}
			dur = (int16)nedPlace(nedSlot, noMirror, pos);
			slotAt(kNedArea, nedSlot).deadline = now + dur * kMsPerStep;
		}

		if (anim > 0) {
			visible(anim, true);
			Slot &as = slot(anim);
			if (as.deadline < now) {
				as.step++;
				if (as.step < as.sequenceLength) {
					as.deadline = now + dur * kMsPerStep;
				} else {
					as.step = 0;
					anim = 0;
				}
			}
			if (anim > 0) {
				int f = 0, d = 0;
				if (seqStep(kArea, anim, slot(anim).step, f, d)) {
					frame = f;
					dur = (int16)d;
				}
				setImage(kArea, anim, frame);
				int ox = 0, oy = 0;
				frameOffset(kArea, anim, frame, ox, oy);
				if (stage == 1)
					pos = Common::Point(_wallX[0] - ox, _wallY[0] - oy);
				if (stage == 2)
					pos = Common::Point(_wallX[1] - ox, _wallY[1] - oy);
				if (stage == 3)
					pos = Common::Point(_wallX[2] - ox, _wallY[2] - oy);
				setPos(anim, pos.x, pos.y);
			}
		}

		pos = Common::Point(nedX, nedY);
		slotAt(kNedArea, kNedBase).x = nedX;
		slotAt(kNedArea, kNedBase).y = nedY;
		if (noMirror == 0 && nedSlot == kNedFall) {
			pos.x = nedX + slotAt(kNedArea, kNedFall).offsets[0];
			pos.y = nedY - 0x14;
			slotAt(kNedArea, kNedBase).x = pos.x;
			slotAt(kNedArea, kNedBase).y = pos.y;
		}
		dur = (int16)nedPlace(nedSlot, noMirror, pos);
		bottom = (int16)(slotAt(kNedArea, nedSlot).h + pos.y);

		if (!tumbling && stage == 0 && _wallY[0] < bottom + 0x36) {
			flipActive(kNedArea, kNedFall, true);
			anim = wallSlot(1);
			tumbling = 1;
			nedSlot = switchSlotAt(kNedArea, kNedTumble, nedSlot);
			slotAt(kNedArea, nedSlot).step = 0;
			playSound(wallSlot(1));
			stage = 1;
			dur = (int16)nedPlace(nedSlot, noMirror, pos);
		}
		if (!tumbling && stage == 1 && _wallY[1] < bottom + 10) {
			flipActive(kNedArea, kNedFlip417, true);
			flipActive(kNedArea, kNedBase, true);
			flipActive(kNedArea, kNedFall, false);
			noMirror = 0;
			anim = wallSlot(4);
			playSound(wallSlot(4));
			tumbling = 1;
			nedSlot = switchSlotAt(kNedArea, kNedTumble, nedSlot);
			stage = 2;
			slotAt(kNedArea, nedSlot).step = 0;
			nedX += 0x78;
			pos = Common::Point(nedX, nedY);
			slotAt(kNedArea, kNedBase).x = nedX;
			slotAt(kNedArea, kNedBase).y = nedY;
			dur = (int16)nedPlace(nedSlot, false, pos);
		}
		if (!tumbling && stage == 2 && bottom > _wallY[2]) {
			flipActive(kNedArea, kNedFlip417, false);
			flipActive(kNedArea, kNedBase, true);
			flipActive(kNedArea, kNedFall, true);
			anim = wallSlot(7);
			playSound(wallSlot(7));
			tumbling = 1;
			nedSlot = switchSlotAt(kNedArea, kNedTumble, nedSlot);
			stage = 3;
			slotAt(kNedArea, nedSlot).step = 0;
			dur = (int16)nedPlace(nedSlot, noMirror, pos);
		}
		draw();
		frameWait(0x23, now);
	}
	stopAllSounds();
	release(kVideoList, 1);
	_fallEnd = Common::Point(slotAt(kNedArea, kNedBase).x, slotAt(kNedArea, kNedBase).y);
	if (nedSlot == kNedTumble) {
		switchSlotAt(kNedArea, kNedFall, kNedTumble);
		slotAt(kNedArea, kNedBase).x = nedX + 0x1b8;
		slotAt(kNedArea, kNedBase).y = nedY;
	}
	visible(wallSlot(1), false);
	visible(wallSlot(4), false);
	visible(wallSlot(7), false);
}

// 0x442a10
void GraveRoom::climbOut(int16 mode) {
	ned()->show(false);
	int gridX = 0, gridY = 0x1e0 - kShaftH;
	_wallY[0] = 0xdc;
	_wallX[0] = 100;
	_wallY[1] = 0x1f4;
	_wallX[1] = 0x17c;
	_wallY[2] = 0x30c;
	_wallX[2] = 100;
	if (_grid)
		_grid->setVisibility(true);
	visibleAt(kNedArea, kNedClimb, true);
	visible(wallSlot(0), true);
	visible(wallSlot(3), true);
	visible(wallSlot(6), true);
	setPos(wallSlot(0), _wallX[0], _wallY[0]);
	setPos(wallSlot(3), _wallX[1], _wallY[1]);
	setPos(wallSlot(6), _wallX[1], _wallY[1]);
	slotAt(kNedArea, kNedClimb).y = 0;
	slotAt(kNedArea, kNedClimb).x = top()->_nedPos.x;
	if (_grid) {
		Common::Point p = _grid->getPosition();
		gridX = p.x;
		gridY = p.y;
	}
	int rise = mode == 0 ? 6 : (mode == 1 ? 2 : 0);
	int16 dur = 0;            // shared by the three creatures, as in the original
	int frame = 0;
	while (gridY < 0 && !_game->getEngine()->shouldQuit()) {
		uint32 now = _game->now();
		gridY += 0x10;
		if (_grid)
			_grid->setPosition(gridX, gridY);
		for (int i = 0; i < 3; i++) {
			int16 idle = wallSlot(i * 3), snap = wallSlot(i * 3 + 2);
			int x = _wallX[i], y = gridY + _wallY[i];
			setPos(idle, x, y);
			if (y <= 0x118)
				continue;
			visible(idle, false);
			visible(snap, true);
			Slot &s = slot(snap);
			if (s.deadline < now) {
				s.step++;
				if (s.step >= s.sequenceLength)
					s.step = s.sequenceLength - 1;
				else
					s.deadline = now + dur * kMsPerStep;
			}
			int f = 0, d = 0;
			if (seqStep(kArea, snap, s.step, f, d)) {
				frame = f;
				dur = (int16)d;
			}
			setImage(kArea, snap, frame);
			int ox = 0, oy = 0;
			frameOffset(kArea, snap, frame, ox, oy);
			setPos(snap, x - ox, y - oy);
		}
		climbStep(now, rise);
		draw();
		frameWait(0x23, now);
	}
	if (_grid)
		_grid->setVisibility(false);
	visible(wallSlot(2), false);
	visible(wallSlot(5), false);
	visible(wallSlot(8), false);
}

// 0x442fb0
int GraveRoom::shaftCreate() {
	BitmapRec *tile = bmp(kBmpShaft);
	if (!tile)
		return 1;
	// The original leaks the previous grid; one is enough
	shaftFree();
	_grid = new RdxGrid(1, 12, 0x280, 0x94);
	for (int row = 0; row < 12; row++)
		_grid->setTile(0, row, &tile->bitmap.surface, -1);
	_grid->setDestination(_game->display().getPrimary());
	_grid->setDrawOrder(99);
	_grid->setPosition(0, 0);
	_grid->setVisibility(false);
	return 0;
}

void GraveRoom::shaftFree() {
	delete _grid;
	_grid = nullptr;
}

// 0x4449d0 / 0x4013e0
int GraveRoom::zapTarget(int16 *dir, int16 *found, int32 rect[4]) {
	*found = -1;
	int unused = 0;
	if (ned()->findTarget(ned()->getFacing(), ned()->getCurSlot(), _targets, *dir, unused, *found)) {
		Common::Point p = spritePos(kArea, *found);
		int frame = 0, duration = 0;
		seqStep(kArea, *found, slot(*found).step, frame, duration);
		boxRect(kArea, *found, frame, 0, rect);
		int dx = p.x - _scrollX, dy = p.y - _scrollY;
		rect[0] += dx;
		rect[1] += dy;
		rect[3] += dy;
		rect[2] += dx;
	}
	return 1;
}

// 0x444020 / 0x444d00 / 0x401020
void GraveRoom::xform(void *user, int &x, int &y) {
	GraveRoom *r = (GraveRoom *)user;
	y += r->_scrollY;
	x += r->_scrollX;
}

// 0x454920
void GraveRoom::nedTick(uint32 input, uint32 now) {
	int facing = 0;
	ned()->tick(_world, input, facing, top()->_nedPos, now, _scrollX, _scrollY, &_hooks);
}

void GraveRoom::climbStep(uint32 now, int rise) {
	Slot &s = slotAt(kNedArea, kNedClimb);
	if (!(s.deadline < now))
		return;
	int frame = 0, duration = 0;
	seqStep(kNedArea, kNedClimb, s.step, frame, duration);
	setImage(kNedArea, kNedClimb, frame);
	int ox = 0, oy = 0;
	frameOffset(kNedArea, kNedClimb, frame, ox, oy);
	int y = _climb.y - oy;
	_climb.y -= rise;
	setPosAt(kNedArea, kNedClimb, _climb.x - ox, y);
	s.step++;
	if (s.step >= s.sequenceLength)
		s.step = 0;
	s.deadline = now + (int16)duration * kMsPerStep;
}

void GraveRoom::loadPlacements(int n, Common::Array<Placement> &list) {
	loadSst(0, 999, n, list);
	for (uint i = 0; i < list.size(); i++)
		applyPlacement(list[i]);
}

void GraveRoom::applyPaletteVA(uint32 va, int mode) {
	const byte *pal = palette(va);
	if (pal)
		_game->display().applyPalette(pal, (PaletteMode)mode);
}

int GraveRoom::submitWithId(uint32 va, int count, int scene, uint32 entry, int id) {
	if (entry >= va && entry < va + (uint32)count * 0x24 && !_entries.contains(entry)) {
		NafArchive *naf = _game->getNaf(kNafGrave);
		if (naf) {
			Common::Array<Request> one = _game->readList(entry, 1);
			Request &req = one[0];
			req.scene = scene;
			req.id = id;
			if (_game->res().load(naf, req)) {
				if (req.bitmap)
					_bitmaps[req.destVA] = req.bitmap;
				if (req.collision)
					_maps[req.destVA] = req.collision;
				if (req.hasPalette && req.paletteVA) {
					Common::Array<byte> &pal = _palettes[req.paletteVA];
					pal.resize(256 * 3);
					memcpy(pal.data(), req.palette, 256 * 3);
				}
				_entries[entry] = req;
			}
		}
	}
	return submit(va, count, scene);
}

// 0x455800
void GraveRoom::playSoundCallback(int id, int callback) {
	if (_game->playSlotSound(slot(id), nullptr)) {
		_soundCbSlot = id;
		_soundCb = callback;
	}
}

int GraveRoom::pollSoundEnd() {
	if (!_soundCb || _game->isVoicePlaying(slot(_soundCbSlot).voice))
		return 0;
	int cb = _soundCb;
	_soundCb = 0;
	return cb;
}

// 0x4558e0
void GraveRoom::loopSound(int id) {
	Slot &s = slot(id);
	_game->stopSlotSound(s);
	if (s.sound.size() < 18)
		return;
	uint32 pcmOffset = 18 + READ_LE_UINT16(s.sound.data() + 16);
	if (pcmOffset >= s.sound.size())
		return;
	_game->playVoice(s.voice, s.sound.data(), s.sound.data() + pcmOffset, s.sound.size() - pcmOffset, 0, 0, true);
}

RdxObject *GraveRoom::bkg(uint32 va) {
	BitmapRec *rec = bmp(va);
	return rec ? rec->object : nullptr;
}

int16 GraveRoom::bmpHeight(uint32 va) {
	BitmapRec *rec = bmp(va);
	return rec ? (int16)rec->bitmap.surface.h : 0;
}

void GraveRoom::setPosAt(int area, int id, int x, int y) {
	Slot &s = slotAt(area, id);
	if (s.sprite)
		s.sprite->setPosition(x, y);
}

Common::Point GraveRoom::spritePos(int area, int id) {
	Slot &s = slotAt(area, id);
	return s.sprite ? s.sprite->getPosition() : Common::Point(0, 0);
}

void GraveRoom::setOrder(int area, int id, int z) {
	Slot &s = slotAt(area, id);
	if (s.sprite)
		s.sprite->setDrawOrder((uint32)z);
}

void GraveRoom::setImage(int area, int id, int frame) {
	Slot &s = slotAt(area, id);
	if (s.sprite)
		s.sprite->setCurrentImage(frame);
}

// 0x430530(1, id, 1, ...)
int GraveRoom::nedPlace(int id, bool noMirror, Common::Point &pos) {
	int duration = placeAt(kNedArea, id, kNedBase, noMirror, nullptr, nullptr);
	pos = spritePos(kNedArea, id);
	return (int16)duration;
}

bool GraveRoom::floorAt(uint32 mapVA, int x, int y0, int &outY) {
	for (int16 y = (int16)y0; y >= 0; y--) {
		if (solidAt(mapVA, (int16)x, y)) {
			outY = y;
			return true;
		}
	}
	return false;
}

} // End of namespace NightmareNed
