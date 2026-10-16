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
#include "common/file.h"
#include "common/system.h"
#include "engines/util.h"
#include "image/png.h"

#include "yaga/display.h"

namespace Yaga {

Display *g_display = nullptr;

void Display::open(int width, int height) {
	if (_open && _surface.w == width && _surface.h == height)
		return;
	// Any true-colour mode will do; the game itself draws 16-bit
	Common::List<Graphics::PixelFormat> formats = g_system->getSupportedFormats();
	for (Common::List<Graphics::PixelFormat>::iterator f = formats.begin(); f != formats.end(); ) {
		if (f->bytesPerPixel < 2)
			f = formats.erase(f);
		else
			++f;
	}
	initGraphics(width, height, formats);
	_surface.create(width, height, g_system->getScreenFormat());
	_open = true;
	debug(1, "yaga: display %dx%d, %d bits", width, height, _surface.format.bytesPerPixel * 8);
}

void Display::clear() {
	if (_open)
		_surface.clear();
}

void Display::present() {
	if (!_open)
		return;
	g_system->copyRectToScreen(_surface.getPixels(), _surface.pitch, 0, 0, _surface.w, _surface.h);
	g_system->updateScreen();
}

bool Display::saveScreenshot(const Common::Path &path) {
	if (!_open)
		return false;
	Common::DumpFile out;
	if (!out.open(path, true))
		return false;
	return Image::writePNG(out, *_surface.surfacePtr());
}

} // End of namespace Yaga
