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

#ifndef NIGHTMARENED_DISPLAY_H
#define NIGHTMARENED_DISPLAY_H

#include "common/array.h"
#include "common/rect.h"
#include "graphics/surface.h"

namespace NightmareNed {

class Resources;
class RdxSurface;
struct CollisionMap;

/** Game input bits (NITENED.EXE 0x446700) */
enum InputBits {
	kInputLeft = 0x01,
	kInputRight = 0x02,
	kInputUp = 0x04,
	kInputDown = 0x20,
	kInputButton1 = 0x100, // Shift
	kInputButton2 = 0x200, // Space
	kInputZ = 0x400,
	kInputButton3 = 0x800, // Ctrl
	kInputC = 0x1000,
	kInputS = 0x2000,
	kInputA = 0x4000,
	kInputW = 0x8000,
	kInputOptions = 0x10000, // O / Escape
	kInputQuit = 0x20000     // Q
};

/** pal_apply modes (0x446c10) */
enum PaletteMode {
	kPalAll = 10,
	kPalGame = 11,   // 10..245, the Windows static colours stay
	kPalStatic = 12, // 0..9 and 246..255
	kPalUpper = 13   // 43..245
};

/**
 * The primary 640x480 surface, the pending palette, and the game's display helpers
 * built on the slot records (0x430710 slot_place and friends).
 */
class Display {
public:
	explicit Display(Resources *res);
	~Display();

	RdxSurface *getPrimary() { return _primary; }

	/** Copies (part of) a 256 x rgb palette into the pending one */
	void applyPalette(const byte *pal, PaletteMode mode);
	/** The palette as last applied (256 x rgb), for 0x446b20 */
	const byte *getPalette() const { return _palette; }
	/** srfDraw on the primary surface, then upload a pending palette and present */
	void draw();

	/**
	 * slot_place (0x430710): shows the slot's current step and positions it relative to
	 * ref's anchor. pos is ref's (or slot + ref's, when relative) world position.
	 */
	bool slotPlace(int area, int slot, int ref, int *outFrame, int *outDuration, bool noMirror,
		const Common::Point &scroll, bool relative = false);
	/** slot_switch (0x430890): hides oldSlot, shows newSlot from step 0 */
	int slotSwitch(int area, int newSlot, int oldSlot, bool preStart = false);

	/** Debug overlay (ned.ini ShowCollisionRects): collision regions and solid masks */
	void setCollisionOverlay(const Common::Array<CollisionMap *> &maps, const Common::Point &scroll);
	bool _showCollision;
	/** Frames presented so far (test hooks count them) */
	uint32 _drawCount;

private:
	static void drawOverlay(Graphics::Surface &dst, const Common::Rect &area, void *user);

	Resources *_res;
	RdxSurface *_primary;
	byte _palette[256 * 3];
	bool _paletteDirty;
	Common::Array<CollisionMap *> _overlayMaps;
	Common::Point _overlayScroll;
};

} // End of namespace NightmareNed

#endif // NIGHTMARENED_DISPLAY_H
