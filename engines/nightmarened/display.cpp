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

#include "common/system.h"
#include "graphics/paletteman.h"

#include "nightmarened/data.h"
#include "nightmarened/display.h"
#include "nightmarened/rdx.h"
#include "nightmarened/resources.h"

namespace NightmareNed {

// The 20 Windows static colours the game leaves at 0..9 and 246..255
static const byte kStaticColors[20 * 3] = {
	0, 0, 0, 128, 0, 0, 0, 128, 0, 128, 128, 0, 0, 0, 128,
	128, 0, 128, 0, 128, 128, 192, 192, 192, 192, 220, 192, 166, 202, 240,
	255, 251, 240, 160, 160, 164, 128, 128, 128, 255, 0, 0, 0, 255, 0,
	255, 255, 0, 0, 0, 255, 255, 0, 255, 0, 255, 255, 255, 255, 255
};

enum {
	kOverlayRegion = 249, // static red
	kOverlaySolid = 250   // static green
};

Display::Display(Resources *res) : _res(res), _showCollision(false), _drawCount(0), _paletteDirty(true) {
	_primary = new RdxSurface(640, 480);
	_primary->setRenderCallback(drawOverlay, this);
	memset(_palette, 0, sizeof(_palette));
	memcpy(_palette, kStaticColors, 30);
	memcpy(_palette + 246 * 3, kStaticColors + 30, 30);
}

Display::~Display() {
	delete _primary;
}

void Display::applyPalette(const byte *pal, PaletteMode mode) {
	int first = 0, last = 255;
	switch (mode) {
	case kPalGame:
		first = 10;
		last = 245;
		break;
	case kPalUpper:
		first = 43;
		last = 245;
		break;
	case kPalStatic:
		memcpy(_palette, pal, 30);
		memcpy(_palette + 246 * 3, pal + 246 * 3, 30);
		_paletteDirty = true;
		return;
	default:
		break;
	}
	memcpy(_palette + first * 3, pal + first * 3, (last - first + 1) * 3);
	_paletteDirty = true;
}

// srfDraw, then the FX_DDRAW_RENDER_CALLBACK palette upload (0x446a40), then present
void Display::draw() {
	_primary->draw();
	if (_paletteDirty) {
		g_system->getPaletteManager()->setPalette(_palette, 0, 256);
		_paletteDirty = false;
	}
	Graphics::Surface &buf = _primary->getBuffer();
	g_system->copyRectToScreen(buf.getPixels(), buf.pitch, 0, 0, buf.w, buf.h);
	g_system->updateScreen();
	_drawCount++;
}

bool Display::slotPlace(int area, int slot, int ref, int *outFrame, int *outDuration, bool noMirror,
	const Common::Point &scroll, bool relative) {
	Slot &s = _res->getSlot(area, slot);
	Slot &r = _res->getSlot(area, ref);
	if (!s.loaded || !s.sequence || !s.sprite)
		return false;
	int frame = 0, duration = 0;
	if (!s.sequence->getStep(s.step, frame, duration))
		return false;
	s.sprite->setCurrentImage(frame);

	int sax = 0, say = 0, rax = 0, ray = 0, fox = 0, foy = 0;
	if (s.boxes) {
		sax = s.boxes->anchorX;
		say = s.boxes->anchorY;
		if (frame < (int)s.boxes->frames.size()) {
			fox = s.boxes->frames[frame].offX;
			foy = s.boxes->frames[frame].offY;
		}
	}
	if (r.boxes) {
		rax = r.boxes->anchorX;
		ray = r.boxes->anchorY;
	}
	int x = relative ? s.x + r.x : r.x;
	int y = relative ? s.y + r.y : r.y;
	int d = rax - sax + fox;
	x = noMirror ? x - d : x - s.w + d;
	y += say - ray - foy;
	s.sprite->setPosition(x + scroll.x, y + scroll.y);

	if (outFrame)
		*outFrame = frame;
	if (outDuration)
		*outDuration = duration;
	return true;
}

int Display::slotSwitch(int area, int newSlot, int oldSlot, bool preStart) {
	if (newSlot > 0) {
		Slot &s = _res->getSlot(area, newSlot);
		s.step = preStart ? -1 : 0;
		s.deadline = 0;
	}
	if (oldSlot >= 0 && _res->getSlot(area, oldSlot).sprite)
		_res->getSlot(area, oldSlot).sprite->setVisibility(false);
	if (newSlot > 0 && _res->getSlot(area, newSlot).sprite)
		_res->getSlot(area, newSlot).sprite->setVisibility(true);
	return newSlot;
}

void Display::setCollisionOverlay(const Common::Array<CollisionMap *> &maps, const Common::Point &scroll) {
	_overlayMaps = maps;
	_overlayScroll = scroll;
}

void Display::drawOverlay(Graphics::Surface &dst, const Common::Rect &area, void *user) {
	Display *d = (Display *)user;
	if (!d->_showCollision)
		return;
	for (const CollisionMap *map : d->_overlayMaps) {
		if (!map)
			continue;
		// Solid mask: every other pixel, so the art stays readable
		for (int y = area.top; y < area.bottom; y += 2) {
			for (int x = area.left + (y / 2) % 2; x < area.right; x += 2) {
				if (map->isSolid(x - d->_overlayScroll.x, y - d->_overlayScroll.y))
					*(byte *)dst.getBasePtr(x, y) = kOverlaySolid;
			}
		}
		for (const CollisionMap::Region &r : map->regions) {
			Common::Rect rect = r.rect;
			rect.translate(d->_overlayScroll.x, d->_overlayScroll.y);
			rect.clip(area);
			if (!rect.isEmpty())
				dst.frameRect(rect, kOverlayRegion);
		}
	}
}

} // End of namespace NightmareNed
