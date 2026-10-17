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

#include "graphics/pixelformat.h"

#include "bigtuna/bitmap.h"
#include "bigtuna/movie.h"
#include "bigtuna/score.h"
#include "bigtuna/stage.h"

namespace BigTuna {

Stage::Stage() {
	_surface.create(kStageWidth, kStageHeight, Graphics::PixelFormat::createFormatCLUT8());
}

Stage::~Stage() {
	_surface.free();
}

void Stage::render(const ScorePlayer &player) {
	clear(0);
	drawPlayer(player, 0, 0);
}

void Stage::clear(byte color) {
	_surface.fillRect(Common::Rect(kStageWidth, kStageHeight), color);
}

void Stage::drawPlayer(const ScorePlayer &player, int offsetX, int offsetY) {
	const Common::Array<Sprite> &sprites = player.getSprites();
	for (uint i = 1; i < sprites.size(); i++) {
		const Sprite &sprite = sprites[i];
		if (!sprite.exists || !sprite.visible || !sprite.member)
			continue;
		int x = sprite.x + offsetX;
		int y = sprite.y + offsetY;
		if (sprite.loop) {
			drawPlayer(*sprite.loop, x - sprite.loop->getOriginX(), y - sprite.loop->getOriginY());
			continue;
		}
		const Bitmap *bitmap = player.getMovie()->getBitmap(sprite.member);
		if (bitmap)
			drawBitmap(*bitmap, x - bitmap->getRegX(), y - bitmap->getRegY(), Common::Rect(kStageWidth, kStageHeight));
	}
}

void Stage::drawBitmap(const Bitmap &bitmap, int x, int y, const Common::Rect &clip) {
	const Graphics::Surface &src = bitmap.getSurface();
	if (src.format.bytesPerPixel != 1)
		return;
	Common::Rect dst(x, y, x + src.w, y + src.h);
	dst.clip(clip);
	if (dst.isEmpty())
		return;

	const byte *mask = bitmap.getMask();
	for (int dy = dst.top; dy < dst.bottom; dy++) {
		int sy = dy - y;
		const byte *s = (const byte *)src.getBasePtr(dst.left - x, sy);
		const byte *m = mask + sy * src.w + (dst.left - x);
		byte *d = (byte *)_surface.getBasePtr(dst.left, dy);
		if (bitmap.isOpaque()) {
			memcpy(d, s, dst.width());
			continue;
		}
		for (int i = 0; i < dst.width(); i++) {
			if (m[i])
				d[i] = s[i];
		}
	}
}

} // End of namespace BigTuna
