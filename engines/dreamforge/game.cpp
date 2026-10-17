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

#include "common/archive.h"
#include "common/config-manager.h"
#include "common/file.h"
#include "common/debug.h"
#include "common/events.h"
#include "common/system.h"
#include "common/textconsole.h"
#include "graphics/paletteman.h"
#include "image/png.h"

#include "dreamforge/actor.h"
#include "dreamforge/detection.h"
#include "dreamforge/dreamforge.h"
#include "dreamforge/resource.h"
#include "dreamforge/scene.h"
#include "dreamforge/sound.h"

namespace DreamForge {

// C.PRG 0x2ec50: title buttons as top, left, height, width; 1-3 at rest, 4-6 pressed
static const int16 kTitleRects[6][4] = {
	{ 369, 108, 92, 132 }, { 369, 241, 92, 154 }, { 369, 396, 92, 156 },
	{ 362, 105, 104, 130 }, { 362, 236, 104, 158 }, { 362, 395, 104, 164 }
};
// PIG5 sprites: saved games, new game, options; at rest and pressed (0x31f90)
static const uint16 kTitleSprites[3][2] = { { 0x11, 0x10 }, { 0x22, 0x21 }, { 0x17, 0x19 } };

enum {
	kTitleBackground = 0x23,
	kTitlePalette = 5,     // COLORS.REL palette for the menus
	kTitleMusic = 5,
	kIntroCutscenes = 3
};

bool DreamForgeEngine::loadMenuPalette(int n, byte *pal) const {
	// COLORS.REL: a count byte, then that many 6-bit palettes (0x25180)
	Common::SeekableReadStream *in = SearchMan.createReadStreamForMember("COLORS.REL");
	if (!in)
		return false;
	in->seek(1 + n * 768);
	for (uint i = 0; i < 768; i++)
		pal[i] = (in->readByte() & 0x3F) * 255 / 63;
	delete in;
	return true;
}

void DreamForgeEngine::drawSprite(uint archive, uint index, int x, int y) {
	Resource *res = _archives[archive]->getResource(index);
	Sprite spr;
	if (res && spr.load(*res)) {
		Common::Rect dst(x, y, x + spr.width, y + spr.height);
		Common::Rect clipped = dst.findIntersectingRect(Common::Rect(kScreenWidth, kScreenHeight));
		if (!clipped.isEmpty())
			_screen.copyRectToSurfaceWithKey(spr.surface, clipped.left, clipped.top,
				Common::Rect(clipped.left - x, clipped.top - y, clipped.right - x, clipped.bottom - y), 0);
	}
	delete res;
}

// 0x11960: three cutscenes; skipping one skips the rest
void DreamForgeEngine::playIntro() {
	static const int intro[kIntroCutscenes] = { 0x137, 0x139, 0x138 };
	for (int i = 0; i < kIntroCutscenes && !shouldQuit(); i++)
		if (playCutscene(intro[i]) != 0)
			break;
}

// 0x32490 / 0x31f90 / 0x33f60: returns 1 saved games, 2 new game, 3 options
int DreamForgeEngine::titleMenu() {
	byte pal[768];
	loadMenuPalette(kTitlePalette, pal);
	g_system->getPaletteManager()->setPalette(pal, 0, 256);
	_sound->playMusic(kTitleMusic, true);
	setCursor(0);
	int pressed = 0;
	auto draw = [&]() {
		_screen.fillRect(Common::Rect(kScreenWidth, kScreenHeight), 0);
		drawSprite(5, kTitleBackground, 0, 0);
		for (int b = 0; b < 3; b++) {
			const int16 *r = kTitleRects[pressed == b + 1 ? b + 3 : b];
			drawSprite(5, kTitleSprites[b][pressed == b + 1], r[1], r[0]);
		}
		g_system->copyRectToScreen(_screen.getPixels(), _screen.pitch, 0, 0, kScreenWidth, kScreenHeight);
		g_system->updateScreen();
	};
	draw();
	// Test hook: dreamforge_title_shot=<png> saves the menu and quits
	if (ConfMan.hasKey("dreamforge_title_shot")) {
		Common::DumpFile out;
		if (out.open(Common::Path(ConfMan.get("dreamforge_title_shot"), '/'), true))
			Image::writePNG(out, _screen, pal);
		quitGame();
		return 0;
	}
	while (!shouldQuit()) {
		Common::Event event;
		while (g_system->getEventManager()->pollEvent(event)) {
			if (event.type != Common::EVENT_LBUTTONDOWN)
				continue;
			for (int i = 0; i < 6; i++) {
				const int16 *r = kTitleRects[i];
				if (event.mouse.y >= r[0] && event.mouse.y < r[0] + r[2] &&
					event.mouse.x >= r[1] && event.mouse.x < r[1] + r[3]) {
					pressed = i % 3 + 1;
					draw();
					_sound->playSfx(14008, 0, false);  // 0x8721c + 8
					return pressed;
				}
			}
		}
		g_system->delayMillis(10);
	}
	return 0;
}

// 0x47360: each game hides things in different places
void DreamForgeEngine::randomizeFlags() {
	for (int g = 0; g < 4; g++)
		for (int f = 0x155 + g * 5; f < 0x15A + g * 5; f++)
			if (getRandom15() >> 14)
				setFlag(f, true);
	int u = (getRandom15() * 5) >> 15;
	for (int g = 0; g < 4; g++) {
		setFlag(0x155 + g * 5 + u, false);
		switch (u) {
		case 1:
		case 2:
		case 3:
			if ((getRandom15() >> 14) == 0) {
				u--;
				break;
			}
			// fall through
		case 0:
			u++;
			break;
		default:
			u--;
			break;
		}
	}
}

// 0x33f60, "new game": the title door, the opening cutscenes, then the first room
void DreamForgeEngine::newGame() {
	playCutscene(0x142);
	initLogicVars();
	randomizeFlags();
	if (playCutscene(0x10A) == 0)
		playCutscene(0xF6);
	_newGame = true;
	loadScene(1, 1, 1);
	_newGame = false;
}

} // End of namespace DreamForge
