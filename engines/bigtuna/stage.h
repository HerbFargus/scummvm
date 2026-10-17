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

#ifndef BIGTUNA_STAGE_H
#define BIGTUNA_STAGE_H

#include "graphics/surface.h"

namespace BigTuna {

class Bitmap;
class ScorePlayer;

enum {
	kStageWidth = 640,
	kStageHeight = 480
};

class Stage {
public:
	Stage();
	~Stage();

	// Draws the sprite channels in order, higher channels on top
	void render(const ScorePlayer &player);
	const Graphics::Surface &getSurface() const { return _surface; }

private:
	void drawPlayer(const ScorePlayer &player, int offsetX, int offsetY);
	void drawBitmap(const Bitmap &bitmap, int x, int y);

	Graphics::Surface _surface;
};

} // End of namespace BigTuna

#endif // BIGTUNA_STAGE_H
