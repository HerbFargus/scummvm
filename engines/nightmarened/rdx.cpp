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

#include "nightmarened/naf.h"
#include "nightmarened/rdx.h"

namespace NightmareNed {

static int wrapMod(int v, int m) {
	if (m <= 0)
		return v;
	v %= m;
	return v < 0 ? v + m : v;
}

// Clips (x, y, w, h) to clip; returns the offset into the source and the visible size
static bool clipRect(int x, int y, int w, int h, const Common::Rect &clip, Common::Rect &dest, Common::Point &srcOff) {
	dest = Common::Rect(x, y, x + w, y + h);
	dest.clip(clip);
	if (dest.isEmpty())
		return false;
	srcOff = Common::Point(dest.left - x, dest.top - y);
	return true;
}

RdxObject::RdxObject(Kind kind) : _kind(kind), _parent(nullptr), _visible(true), _order(1),
	_x(0), _y(0), _extW(0), _extH(0), _viewX(0), _viewY(0), _viewW(0), _viewH(0), _w(0), _h(0),
	_current(0), _count(1) {
}

RdxObject::~RdxObject() {
	if (_parent)
		_parent->removeChild(this);
	for (RdxEffect *fx : _effects)
		delete fx;
}

void RdxObject::setSize(int w, int h) {
	_w = w;
	_h = h;
	_viewX = _viewY = 0;
	_viewW = w;
	_viewH = h;
	_extW = w;
	_extH = h;
}

void RdxObject::setDestination(RdxSurface *parent) {
	if (_parent)
		_parent->removeChild(this);
	_parent = parent;
	if (_parent)
		_parent->insertChild(this);
}

// Always re-inserts, even with the same order: it then goes below its equals
void RdxObject::setDrawOrder(uint32 order) {
	_order = order;
	if (_parent) {
		_parent->removeChild(this);
		_parent->insertChild(this);
	}
}

void RdxObject::setDestRect(int x, int y, int w, int h) {
	if (!x && !y && !w && !h) {
		_x = _y = 0;
		_extW = _w;
		_extH = _h;
		return;
	}
	_x = x;
	_y = y;
	_extW = w;
	_extH = h;
}

void RdxObject::adjView(int dx, int dy, int dw, int dh) {
	_viewX = wrapMod(_viewX + dx, _w);
	_viewY = wrapMod(_viewY + dy, _h);
	_viewW = wrapMod(_viewW + dw, _w ? _w + 1 : 0);
	_viewH = wrapMod(_viewH + dh, _h ? _h + 1 : 0);
}

Common::Rect RdxObject::getView() const {
	return Common::Rect(_viewX, _viewY, _viewX + _viewW, _viewY + _viewH);
}

bool RdxObject::setCurrentImage(int image) {
	int clamped = CLIP(image, 0, MAX(_count - 1, 0));
	_current = clamped;
	return clamped != image;
}

RdxEffect *RdxObject::applyEffect(RdxEffectType type, int32 smorphMode, const int32 *rows, uint rowCount) {
	RdxEffect *fx = new RdxEffect();
	fx->type = type;
	fx->active = true;
	fx->smorphMode = smorphMode;
	fx->smorphRows = rows;
	fx->smorphRowCount = rowCount;
	_effects.push_front(fx);
	return fx;
}

void RdxObject::removeEffect(RdxEffect *fx) {
	_effects.remove(fx);
	delete fx;
}

const RdxEffect *RdxObject::activeEffect(RdxEffectType type) const {
	for (const RdxEffect *fx : _effects) {
		if (fx->type == type && fx->active)
			return fx;
	}
	return nullptr;
}

RdxSprite::RdxSprite() : RdxObject(kSprite) {
}

void RdxSprite::setFrames(const Common::Array<SpriteFrame *> &frames, int w, int h) {
	_frames = frames;
	_count = MAX<int>(frames.size(), 1);
	_current = 0;
	setSize(w, h);
}

// Sprites ignore the view and draw from (0, 0) of the frame. A flipped sprite mirrors about its
// frame: Dino2d's SprRenderHFlip (d2:100065b6) turns the clip offset sx into frameW - w - sx
// before rdxp5 ClipHFlipSprite8 skips that many source columns, so a cut shows the crop of the mirror.
void RdxSprite::render(Graphics::Surface &dst, const Common::Rect &clip, const Common::Point &origin) {
	if (_current >= (int)_frames.size() || !_frames[_current])
		return;
	const SpriteFrame *f = _frames[_current];
	int fw = MIN<int>(f->surface.w, _extW), fh = MIN<int>(f->surface.h, _extH);
	Common::Rect dest;
	Common::Point off;
	if (!clipRect(origin.x + _x, origin.y + _y, fw, fh, clip, dest, off))
		return;
	int w = MIN<int>(f->surface.w - off.x, dest.width()), h = MIN<int>(f->surface.h - off.y, dest.height());
	bool flip = activeEffect(kFxHorizFlip) != nullptr;
	for (int i = 0; i < h; i++) {
		int sy = off.y + i;
		byte *out = (byte *)dst.getBasePtr(dest.left, dest.top + i);
		for (int j = 0; j < w; j++) {
			int sx = flip ? f->surface.w - 1 - off.x - j : off.x + j;
			if (f->isOpaque(sx, sy))
				out[j] = *(const byte *)f->surface.getBasePtr(sx, sy);
		}
	}
}

RdxBackground::RdxBackground() : RdxObject(kBackground), _bitmap(nullptr), _key(-1) {
}

void RdxBackground::setBitmap(const Graphics::Surface *bitmap, int key) {
	_bitmap = bitmap;
	_key = key;
	_count = 1;
	_current = 0;
	setSize(bitmap ? bitmap->w : 0, bitmap ? bitmap->h : 0);
}

byte RdxBackground::sampleView(int u, int v) const {
	return *(const byte *)_bitmap->getBasePtr(wrapMod(_viewX + u, _bitmap->w), wrapMod(_viewY + v, _bitmap->h));
}

void RdxBackground::render(Graphics::Surface &dst, const Common::Rect &clip, const Common::Point &origin) {
	if (!_bitmap || !_bitmap->getPixels())
		return;
	Common::Rect dest;
	Common::Point off;
	if (!clipRect(origin.x + _x, origin.y + _y, _extW, _extH, clip, dest, off))
		return;

	const RdxEffect *smorph = activeEffect(kFxSmorph);
	if (smorph && smorph->smorphRows) {
		// One 16.16 {u0, v0, du, dv} row per destination line, sampling the view
		for (int i = 0; i < dest.height(); i++) {
			uint row = off.y + i;
			if (row >= smorph->smorphRowCount)
				break;
			const int32 *r = smorph->smorphRows + row * 4;
			int32 u = r[0] + off.x * r[2], v = r[1] + off.x * r[3];
			byte *out = (byte *)dst.getBasePtr(dest.left, dest.top + i);
			bool entered = false;
			for (int j = 0; j < dest.width(); j++, u += r[2], v += r[3]) {
				int su = u >> 16, sv = v >> 16;
				if (!smorph->smorphMode && (su < 0 || su >= _viewW || sv < 0 || sv >= _viewH)) {
					if (entered)
						break;
					continue;
				}
				entered = true;
				byte c = sampleView(su, sv);
				if (_key < 0 || c != _key)
					out[j] = c;
			}
		}
		return;
	}

	int w = MIN<int>(_viewW - off.x, dest.width()), h = MIN<int>(_viewH - off.y, dest.height());
	for (int i = 0; i < h; i++) {
		byte *out = (byte *)dst.getBasePtr(dest.left, dest.top + i);
		for (int j = 0; j < w; j++) {
			byte c = sampleView(off.x + j, off.y + i);
			if (_key < 0 || c != _key)
				out[j] = c;
		}
	}
}

RdxGrid::RdxGrid(int cols, int rows, int cellW, int cellH) : RdxObject(kGrid),
	_cols(cols), _rows(rows), _cellW(cellW), _cellH(cellH) {
	Cell empty = { nullptr, -1 };
	_cells.resize(cols * rows, empty);
	setSize(cols * cellW, rows * cellH);
}

void RdxGrid::setTile(int col, int row, const Graphics::Surface *tile, int key) {
	if (col >= 0 && col < _cols && row >= 0 && row < _rows) {
		_cells[row * _cols + col].tile = tile;
		_cells[row * _cols + col].key = key;
	}
}

void RdxGrid::render(Graphics::Surface &dst, const Common::Rect &clip, const Common::Point &origin) {
	Common::Rect dest;
	Common::Point off;
	if (!clipRect(origin.x + _x, origin.y + _y, _extW, _extH, clip, dest, off))
		return;
	for (int i = 0; i < dest.height(); i++) {
		int wy = _viewY + off.y + i;
		if (wy < 0 || wy >= _rows * _cellH)
			continue;
		byte *out = (byte *)dst.getBasePtr(dest.left, dest.top + i);
		for (int j = 0; j < dest.width(); j++) {
			int wx = _viewX + off.x + j;
			if (wx < 0 || wx >= _cols * _cellW)
				continue;
			const Cell &cell = _cells[(wy / _cellH) * _cols + wx / _cellW];
			if (!cell.tile || wx % _cellW >= cell.tile->w || wy % _cellH >= cell.tile->h)
				continue;
			byte c = *(const byte *)cell.tile->getBasePtr(wx % _cellW, wy % _cellH);
			if (cell.key < 0 || c != cell.key)
				out[j] = c;
		}
	}
}

RdxSurface::RdxSurface(int w, int h) : RdxObject(kSurface), _clearColor(-1), _callback(nullptr), _callbackUser(nullptr) {
	setSize(w, h);
}

RdxSurface::~RdxSurface() {
	for (RdxObject *obj : _children)
		obj->_parent = nullptr;
	_buffer.free();
}

void RdxSurface::removeChild(RdxObject *obj) {
	_children.remove(obj);
}

// Descending, unsigned: goes in front of the first child whose order is <= its own
void RdxSurface::insertChild(RdxObject *obj) {
	for (Common::List<RdxObject *>::iterator it = _children.begin(); it != _children.end(); ++it) {
		if ((*it)->_order <= obj->_order) {
			_children.insert(it, obj);
			return;
		}
	}
	_children.push_back(obj);
}

void RdxSurface::renderChildren(Graphics::Surface &dst, const Common::Rect &clip, const Common::Point &origin) {
	if (_clearColor >= 0)
		dst.fillRect(clip, _clearColor);
	for (RdxObject *obj : _children) {
		if (obj->_visible)
			obj->render(dst, clip, origin);
	}
	if (_callback)
		_callback(dst, clip, _callbackUser);
}

void RdxSurface::draw() {
	if (!_visible)
		return;
	if (!_buffer.getPixels())
		_buffer.create(_w, _h, Graphics::PixelFormat::createFormatCLUT8());
	renderChildren(_buffer, Common::Rect(_w, _h), Common::Point(0, 0));
}

// A child surface (the game's callback surface) draws into its parent at its position
void RdxSurface::render(Graphics::Surface &dst, const Common::Rect &clip, const Common::Point &origin) {
	Common::Rect area(origin.x + _x, origin.y + _y, origin.x + _x + _extW, origin.y + _y + _extH);
	area.clip(clip);
	if (!area.isEmpty())
		renderChildren(dst, area, Common::Point(origin.x + _x - _viewX, origin.y + _y - _viewY));
}

void RdxGroup::addObject(RdxObject *obj) {
	for (RdxObject *m : _members) {
		if (m == obj)
			return;
	}
	_members.push_back(obj);
}

void RdxGroup::setDestination(RdxSurface *parent) {
	for (RdxObject *m : _members)
		m->setDestination(parent);
}

void RdxGroup::setDrawOrder(uint32 order) {
	for (RdxObject *m : _members)
		m->setDrawOrder(order);
}

void RdxGroup::setVisibility(bool visible) {
	for (RdxObject *m : _members)
		m->setVisibility(visible);
}

void RdxGroup::setPosition(int x, int y) {
	for (RdxObject *m : _members)
		m->setPosition(x, y);
}

void RdxGroup::adjPosition(int dx, int dy) {
	for (RdxObject *m : _members)
		m->adjPosition(dx, dy);
}

void RdxGroup::adjView(int dx, int dy, int dw, int dh) {
	for (RdxObject *m : _members)
		m->adjView(dx, dy, dw, dh);
}

} // End of namespace NightmareNed
