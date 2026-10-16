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

#ifndef YAGA_DISPLAY_H
#define YAGA_DISPLAY_H

#include "common/path.h"
#include "graphics/managed_surface.h"

namespace Yaga {

// The game's render target: a back buffer the scripts draw into between
// RenderBegin and RenderEnd, then shown on the ScummVM screen
class Display {
public:
	Display() : _open(false) {}
	~Display() { _converted.free(); }

	bool isOpen() const { return _open; }
	// Sets the screen mode; the scripts ask for 640x480
	void open(int width, int height);
	Graphics::ManagedSurface &surface() { return _surface; }
	const Graphics::PixelFormat &format() const { return _surface.format; }

	void clear();
	void present();
	bool saveScreenshot(const Common::Path &path);

private:
	bool _open;
	Graphics::ManagedSurface _surface;
	Graphics::Surface _converted; // in the screen's format
};

extern Display *g_display;

// Alpha-blends srcRect of src into dstRect of dst (scaled if they differ),
// clipped to dst first: ManagedSurface::blendBlitFrom clips the right and
// bottom edges against the source's size, not its own, and so draws nothing
// of a sprite that runs a pixel off the edge
void blendClipped(Graphics::ManagedSurface &dst, const Graphics::Surface &src, Common::Rect srcRect, Common::Rect dstRect, uint alpha);

} // End of namespace Yaga

#endif // YAGA_DISPLAY_H
