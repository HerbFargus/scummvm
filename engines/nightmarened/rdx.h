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

#ifndef NIGHTMARENED_RDX_H
#define NIGHTMARENED_RDX_H

#include "common/array.h"
#include "common/list.h"
#include "common/rect.h"
#include "graphics/surface.h"

namespace NightmareNed {

struct SpriteFrame;

/*
 * The subset of Intel RDX (dmix.dll + Dino2d.dll) the game draws with: objects
 * attached to a surface, kept in descending draw order (larger = further back),
 * fully redrawn on every draw().
 */

enum RdxEffectType {
	kFxHorizFlip,
	kFxSmorph
};

struct RdxEffect {
	RdxEffectType type;
	bool active;
	// FX_SMORPH: mode 0 clips, anything else wraps; one 16.16 {u0, v0, du, dv} row per dest line
	int32 smorphMode;
	const int32 *smorphRows;
	uint smorphRowCount;
};

class RdxSurface;

class RdxObject {
public:
	enum Kind {
		kSprite,
		kBackground,
		kGrid,
		kSurface
	};

	explicit RdxObject(Kind kind);
	virtual ~RdxObject();

	Kind getKind() const { return _kind; }

	void setDestination(RdxSurface *parent);
	RdxSurface *getDestination() const { return _parent; }
	void setDrawOrder(uint32 order);
	uint32 getDrawOrder() const { return _order; }
	void setVisibility(bool visible) { _visible = visible; }
	bool getVisibility() const { return _visible; }
	void setPosition(int x, int y) { _x = x; _y = y; }
	void adjPosition(int dx, int dy) { _x += dx; _y += dy; }
	Common::Point getPosition() const { return Common::Point(_x, _y); }
	/** All zero resets to (0, 0, w, h) */
	void setDestRect(int x, int y, int w, int h);
	/** Background view; wraps modulo the object size (dmix 0x10001250) */
	void adjView(int dx, int dy, int dw, int dh);
	Common::Rect getView() const;
	/** Clamps to the image count; true if it clamped */
	bool setCurrentImage(int image);
	int getCurrentImage() const { return _current; }

	RdxEffect *applyEffect(RdxEffectType type, int32 smorphMode = 0, const int32 *rows = nullptr, uint rowCount = 0);
	void removeEffect(RdxEffect *fx);
	const RdxEffect *activeEffect(RdxEffectType type) const;

	/** Draws into dst; clip is the surface area in dst coordinates */
	virtual void render(Graphics::Surface &dst, const Common::Rect &clip, const Common::Point &origin) = 0;

protected:
	friend class RdxSurface;

	/** Sets the object size and resets the view and dest extents (dmix spiSetWidth/Height) */
	void setSize(int w, int h);

	Kind _kind;
	RdxSurface *_parent;
	bool _visible;
	uint32 _order;
	int _x, _y, _extW, _extH;
	int _viewX, _viewY, _viewW, _viewH;
	int _w, _h;
	int _current, _count;
	Common::List<RdxEffect *> _effects;
};

/** spr + hseq: frames drawn at the top-left of the object, mask-transparent */
class RdxSprite : public RdxObject {
public:
	RdxSprite();
	/** Frames are not owned; w/h are the sequence size */
	void setFrames(const Common::Array<SpriteFrame *> &frames, int w, int h);
	void render(Graphics::Surface &dst, const Common::Rect &clip, const Common::Point &origin) override;

private:
	Common::Array<SpriteFrame *> _frames;
};

/** bkg: an 8-bit bitmap drawn through its view, wrapping like a torus, optionally colour-keyed */
class RdxBackground : public RdxObject {
public:
	RdxBackground();
	/** The surface is not owned and is read on every draw (hbmpSetPointer) */
	void setBitmap(const Graphics::Surface *bitmap, int key);
	void render(Graphics::Surface &dst, const Common::Rect &clip, const Common::Point &origin) override;

private:
	byte sampleView(int u, int v) const;

	const Graphics::Surface *_bitmap;
	int _key;
};

/** grd: cols x rows cells of one-bitmap tiles, positioned in world pixels, no wrap */
class RdxGrid : public RdxObject {
public:
	RdxGrid(int cols, int rows, int cellW, int cellH);
	void setTile(int col, int row, const Graphics::Surface *tile, int key);
	void render(Graphics::Surface &dst, const Common::Rect &clip, const Common::Point &origin) override;

private:
	int _cols, _rows, _cellW, _cellH;
	struct Cell {
		const Graphics::Surface *tile;
		int key;
	};
	Common::Array<Cell> _cells;
};

/** srf: owns a 640x480 buffer when it is the main surface, or draws into its parent's */
class RdxSurface : public RdxObject {
public:
	typedef void (*RenderCallback)(Graphics::Surface &dst, const Common::Rect &area, void *user);

	RdxSurface(int w, int h);
	~RdxSurface() override;

	/** srfSetColor / srfClearColor */
	void setClearColor(int color) { _clearColor = color; }
	void setRenderCallback(RenderCallback cb, void *user) { _callback = cb; _callbackUser = user; }

	/** srfDraw on the main surface: composite into the buffer */
	void draw();
	Graphics::Surface &getBuffer() { return _buffer; }

	void render(Graphics::Surface &dst, const Common::Rect &clip, const Common::Point &origin) override;

private:
	friend class RdxObject;

	void removeChild(RdxObject *obj);
	void insertChild(RdxObject *obj);
	void renderChildren(Graphics::Surface &dst, const Common::Rect &clip, const Common::Point &origin);

	Common::List<RdxObject *> _children; // head = largest order = drawn first
	Graphics::Surface _buffer;
	int _clearColor;
	RenderCallback _callback;
	void *_callbackUser;
};

/** grp: broadcasts setters to its members in insertion order */
class RdxGroup {
public:
	void addObject(RdxObject *obj);
	void setDestination(RdxSurface *parent);
	void setDrawOrder(uint32 order);
	void setVisibility(bool visible);
	void setPosition(int x, int y);
	void adjPosition(int dx, int dy);
	void adjView(int dx, int dy, int dw, int dh);

private:
	Common::Array<RdxObject *> _members;
};

} // End of namespace NightmareNed

#endif // NIGHTMARENED_RDX_H
