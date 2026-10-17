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

#ifndef DREAMFORGE_DREAMFORGE_H
#define DREAMFORGE_DREAMFORGE_H

#include "common/array.h"
#include "common/rect.h"
#include "common/str.h"
#include "engines/advancedDetector.h"
#include "engines/engine.h"
#include "graphics/surface.h"

namespace DreamForge {

class ResourceArchive;
class Scene;
struct Sprite;

enum {
	kScreenWidth = 640,
	kScreenHeight = 480,
	kArchiveCount = 7  // PIG0.USR .. PIG6.USR
};

class DreamForgeEngine : public Engine {
public:
	DreamForgeEngine(OSystem *syst, const ADGameDescription *gameDesc);
	~DreamForgeEngine() override;

	Common::Error run() override;
	bool hasFeature(EngineFeature f) const override;

	/** Loads a scene by act, area and room (SCENES\<act>X<area>X<room>.PIG) */
	bool loadScene(int act, int area, int room);
	/** Parses "1X01X06" (or "1x01x06.pig") */
	static bool parseSceneName(const Common::String &name, int &act, int &area, int &room);
	/** Every scene file on the disc, sorted */
	Common::Array<Common::String> listScenes() const;

	Scene *getScene() const { return _scene; }
	ResourceArchive *getArchive(uint n) const { return n < kArchiveCount ? _archives[n] : nullptr; }

	void setScroll(int x);
	int getScroll() const { return _scrollX; }
	bool _showLayers;
	Common::Array<bool> _layerVisible;

	void redraw();
	bool saveShot(const Common::String &path);

private:
	bool mountData();
	void setupCursor();
	void mainLoop();
	void handleEvents();
	void dumpAllScenes(const Common::String &dir);

	const ADGameDescription *_gameDescription;
	ResourceArchive *_archives[kArchiveCount];
	Scene *_scene;
	Sprite *_cursors;
	Graphics::Surface _screen;
	int _scrollX;
	Common::Point _mousePos;
	bool _dirty;
};

} // End of namespace DreamForge

#endif // DREAMFORGE_DREAMFORGE_H
