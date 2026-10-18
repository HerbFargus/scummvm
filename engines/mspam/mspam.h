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

#ifndef MSPAM_MSPAM_H
#define MSPAM_MSPAM_H

#include "common/array.h"
#include "common/str.h"
#include "engines/advancedDetector.h"
#include "engines/engine.h"
#include "graphics/surface.h"

#include "mspam/compound.h"

namespace GUI {
class Dialog;
}

namespace MSPAM {

class Layout;
class Runtime;
class TestInputScript;
class MediaManager;
class Title;

enum {
	kScreenWidth = 640,
	kScreenHeight = 480
};

class MSPAMEngine : public Engine {
public:
	MSPAMEngine(OSystem *syst, const ADGameDescription *gameDesc);
	~MSPAMEngine() override;

	Common::Error run() override;
	bool hasFeature(EngineFeature f) const override;
	int runDialog(GUI::Dialog &dialog) override;

private:
	bool loadTitle();
	bool showLayout(int index);
	void renderLayout(const Layout &layout, Graphics::Surface &dst, bool showHidden, bool showHotspots);
	void redraw();
	/** Logs which element, hotspot and action lists a click at (x, y) would trigger */
	void hitTest(int x, int y);
	bool saveShot(const Common::String &path);
	void saveAllShots(const Common::String &dir);
	void dumpLayouts(const Common::String &path);
	void viewer();
	/** Runs the title from a layout with the action runtime */
	void runGame(const Common::String &start);
	int playBusGame(const Common::String &planet, bool lizIntro, Runtime *runtime, TestInputScript *test);
	void pauseEngineIntern(bool pause) override;
	/** The game clock: milliseconds without the time spent paused */
	uint32 getGameTime() const { return getTotalPlayTime(); }
	/** A Windows message box with Yes and No (a test script answers it instead) */
	bool askYesNo(const Common::U32String &text);
	/** The title's About box (Ctrl+Shift+I) */
	void showAbout();
	/** A quit request that came from the global main menu, not the window: no question */
	bool isMenuQuit() const { return _menuQuitGrace > 0; }

	const ADGameDescription *_gameDescription;
	CompoundFile _tdf;
	MediaManager *_media;
	Title *_title;
	Layout *_layout;
	int _layoutIndex;
	Graphics::Surface _screen;
	byte _palette[256 * 3];
	bool _showHidden;
	bool _showHotspots;
	Runtime *_runtime;       ///< the running title, while runGame runs
	TestInputScript *_test;
	int _menuQuitGrace;      ///< frames after a dialog closed in which EVENT_QUIT came from it
};

} // End of namespace MSPAM

#endif // MSPAM_MSPAM_H
