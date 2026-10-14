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

#include "common/config-manager.h"
#include "common/debug.h"
#include "common/events.h"
#include "common/file.h"
#include "common/fs.h"
#include "common/macresman.h"
#include "common/ptr.h"
#include "common/compression/stuffit.h"
#include "common/system.h"
#include "common/translation.h"
#include "common/formats/winexe.h"

#include "engines/util.h"

#include "gui/message.h"

#include "graphics/cursorman.h"
#include "graphics/maccursor.h"
#include "graphics/surface.h"
#include "graphics/pixelformat.h"
#include "graphics/wincursor.h"
#include "graphics/paletteman.h"

#include "image/png.h"

#include "mtropolis/mtropolis.h"

#include "mtropolis/actions.h"
#include "mtropolis/boot.h"
#include "mtropolis/debug.h"
#include "mtropolis/runtime.h"

#include "mtropolis/plugins.h"
#include "mtropolis/plugin/standard.h"
#include "mtropolis/plugin/obsidian.h"

namespace MTropolis {

// Development aid: when "mtropolis_test_input" names a file, its lines are
// run as a test script, injecting input through the event queue so a game
// can be tested without a real mouse or keyboard:
//   scene <name>     wait until the named scene is active
//   wait <ms>        wait
//   click <x> <y>    click at game coordinates
//   type <text>      type text
//   key <name>       press enter, escape, backspace or tab
//   shot <path>      save a PNG of the screen
//   probe <x> <y>    log the visual elements at a point
//   quit             quit the engine
class TestInputScript {
public:
	explicit TestInputScript(const Common::Path &path);

	void update(OSystem *system, Runtime *runtime, Engine *engine);

private:
	void pushKey(Common::EventManager *events, Common::KeyCode keycode, uint16 ascii);
	void saveScreenshot(OSystem *system, const Common::String &path);
	void probe(Structural *structural, const Common::Point &pt);

	Common::Array<Common::String> _lines;
	uint _lineIndex;
	uint32 _waitUntil;
};

TestInputScript::TestInputScript(const Common::Path &path) : _lineIndex(0), _waitUntil(0) {
	Common::File f;
	if (!f.open(Common::FSNode(path))) {
		warning("Test input: Couldn't open '%s'", path.toString().c_str());
		return;
	}

	while (!f.eos()) {
		Common::String line = f.readLine();
		line.trim();
		if (!line.empty() && !line.hasPrefix("#"))
			_lines.push_back(line);
	}
}

void TestInputScript::pushKey(Common::EventManager *events, Common::KeyCode keycode, uint16 ascii) {
	Common::Event evt;
	evt.type = Common::EVENT_KEYDOWN;
	evt.kbd = Common::KeyState(keycode, ascii);
	events->pushEvent(evt);
	evt.type = Common::EVENT_KEYUP;
	events->pushEvent(evt);
}

void TestInputScript::saveScreenshot(OSystem *system, const Common::String &path) {
	Graphics::Surface *screen = system->lockScreen();
	if (!screen)
		return;

	byte palette[256 * 3];
	if (screen->format.isCLUT8())
		system->getPaletteManager()->grabPalette(palette, 0, 256);

	// The screen's alpha bits aren't shown, so leave them out
	Graphics::Surface *opaque = screen->convertTo(Graphics::PixelFormat::createFormatRGB24(), screen->format.isCLUT8() ? palette : nullptr);
	system->unlockScreen();

	Common::DumpFile out;
	if (out.open(Common::FSNode(Common::Path(path, Common::Path::kNativeSeparator))))
		Image::writePNG(out, *opaque);
	else
		warning("Test input: Couldn't write '%s'", path.c_str());

	opaque->free();
	delete opaque;
}

void TestInputScript::probe(Structural *structural, const Common::Point &pt) {
	if (structural->isElement() && static_cast<Element *>(structural)->isVisual()) {
		VisualElement *visual = static_cast<VisualElement *>(structural);
		Common::Point pos = visual->getGlobalPosition();
		const Common::Rect &rect = visual->getRelativeRect();
		Common::Rect globalRect(pos.x, pos.y, pos.x + rect.width(), pos.y + rect.height());
		if (globalRect.contains(pt))
			debug(1, "Test input: Probe '%s' (%i,%i)-(%i,%i) visible %i ink %i", visual->getName().c_str(),
				  globalRect.left, globalRect.top, globalRect.right, globalRect.bottom, visual->isVisible() ? 1 : 0, static_cast<int>(visual->getRenderProperties().getInkMode()));
	}

	for (const Common::SharedPtr<Structural> &child : structural->getChildren())
		probe(child.get(), pt);
}

void TestInputScript::update(OSystem *system, Runtime *runtime, Engine *engine) {
	uint32 now = system->getMillis();

	while (_lineIndex < _lines.size() && now >= _waitUntil) {
		const Common::String &line = _lines[_lineIndex];
		Common::String cmd = line;
		Common::String arg;
		size_t space = line.findFirstOf(' ');
		if (space != Common::String::npos) {
			cmd = line.substr(0, space);
			arg = line.substr(space + 1);
		}

		Common::EventManager *events = system->getEventManager();

		if (cmd == "scene") {
			Structural *scene = runtime->getActiveMainScene().get();
			if (!scene || !scene->getName().equalsIgnoreCase(arg))
				return;
			debug(1, "Test input: Scene '%s' is active", arg.c_str());
		} else if (cmd == "wait") {
			_waitUntil = now + atoi(arg.c_str());
		} else if (cmd == "click") {
			int x = 0, y = 0;
			sscanf(arg.c_str(), "%d %d", &x, &y);
			Common::Event evt;
			evt.mouse = Common::Point(x, y);
			evt.type = Common::EVENT_MOUSEMOVE;
			events->pushEvent(evt);
			evt.type = Common::EVENT_LBUTTONDOWN;
			events->pushEvent(evt);
			evt.type = Common::EVENT_LBUTTONUP;
			events->pushEvent(evt);
			_waitUntil = now + 100;
		} else if (cmd == "type") {
			for (uint i = 0; i < arg.size(); i++) {
				char c = arg[i];
				Common::KeyCode keycode = static_cast<Common::KeyCode>((c >= 'A' && c <= 'Z') ? (c - 'A' + 'a') : c);
				pushKey(events, keycode, c);
			}
		} else if (cmd == "key") {
			if (arg == "enter")
				pushKey(events, Common::KEYCODE_RETURN, 13);
			else if (arg == "escape")
				pushKey(events, Common::KEYCODE_ESCAPE, 27);
			else if (arg == "backspace")
				pushKey(events, Common::KEYCODE_BACKSPACE, 8);
			else if (arg == "tab")
				pushKey(events, Common::KEYCODE_TAB, 9);
		} else if (cmd == "shot") {
			saveScreenshot(system, arg);
			debug(1, "Test input: Saved '%s'", arg.c_str());
		} else if (cmd == "probe") {
			int x = 0, y = 0;
			sscanf(arg.c_str(), "%d %d", &x, &y);
			if (runtime->getActiveSharedScene())
				probe(runtime->getActiveSharedScene().get(), Common::Point(x, y));
			if (runtime->getActiveMainScene())
				probe(runtime->getActiveMainScene().get(), Common::Point(x, y));
		} else if (cmd == "quit") {
			engine->quitGame();
		}

		_lineIndex++;
	}
}


MTropolisEngine::MTropolisEngine(OSystem *syst, const MTropolisGameDescription *gameDesc) : Engine(syst), _gameDescription(gameDesc), _saveWriter(nullptr), _isTriggeredAutosave(false) {
	const Common::FSNode gameDataDir(ConfMan.getPath("path"));
	SearchMan.addSubDirectoryMatching(gameDataDir, "Resource");

	bootAddSearchPaths(gameDataDir, *gameDesc);
}

MTropolisEngine::~MTropolisEngine() {
}

void MTropolisEngine::handleEvents() {
	Common::Event evt;
	Common::EventManager *eventMan = _system->getEventManager();

	while (eventMan->pollEvent(evt)) {
		switch (evt.type) {
		case Common::EVENT_LBUTTONDOWN:
			_runtime->onMouseDown(evt.mouse.x, evt.mouse.y, MTropolis::Actions::kMouseButtonLeft);
			break;
		case Common::EVENT_MBUTTONDOWN:
			_runtime->onMouseDown(evt.mouse.x, evt.mouse.y, MTropolis::Actions::kMouseButtonMiddle);
			break;
		case Common::EVENT_RBUTTONDOWN:
			_runtime->onMouseDown(evt.mouse.x, evt.mouse.y, MTropolis::Actions::kMouseButtonRight);
			break;
		case Common::EVENT_LBUTTONUP:
			_runtime->onMouseUp(evt.mouse.x, evt.mouse.y, MTropolis::Actions::kMouseButtonLeft);
			break;
		case Common::EVENT_MBUTTONUP:
			_runtime->onMouseUp(evt.mouse.x, evt.mouse.y, MTropolis::Actions::kMouseButtonMiddle);
			break;
		case Common::EVENT_RBUTTONUP:
			_runtime->onMouseUp(evt.mouse.x, evt.mouse.y, MTropolis::Actions::kMouseButtonRight);
			break;
		case Common::EVENT_MOUSEMOVE:
			_runtime->onMouseMove(evt.mouse.x, evt.mouse.y);
			break;
		case Common::EVENT_KEYDOWN:
		case Common::EVENT_KEYUP:
			_runtime->onKeyboardEvent(evt.type, evt.kbdRepeat, evt.kbd);
			break;
		case Common::EVENT_CUSTOM_ENGINE_ACTION_START:
			_runtime->onAction(static_cast<MTropolis::Actions::Action>(evt.customType));
			break;

		default:
			break;
		}
	}
}

Common::Error MTropolisEngine::run() {
#if !defined(USE_MPEG2)
	if (_gameDescription->desc.flags & MTGF_WANT_MPEG_VIDEO) {
		GUI::MessageDialog dialog(
			_("This game requires MPEG video support for some\n"
			  "content but MPEG video support was not compiled in.\n"
			  "The game will still play, but MPEG videos will not work."));
		dialog.runModal();
	}
#endif
#if !defined(USE_MAD)
	if (_gameDescription->desc.flags & MTGF_WANT_MPEG_AUDIO) {
		GUI::MessageDialog dialog(
			_("This game requires MPEG audio support for some\n"
			  "content but MPEG audio support was not compiled in.\n"
			  "The game will still play, but some audio will not work."));
		dialog.runModal();
	}
#endif

	int preferredWidth = 1024;
	int preferredHeight = 768;

	ColorDepthMode preferredColorDepthMode = kColorDepthMode8Bit;
	ColorDepthMode enhancedColorDepthMode = kColorDepthMode8Bit;

	Common::SharedPtr<SubtitleRenderer> subRenderer;

	if (ConfMan.getBool("subtitles"))
		subRenderer.reset(new SubtitleRenderer(ConfMan.getBool("mtropolis_mod_sound_gameplay_subtitles")));

	_runtime.reset(new Runtime(_system, _mixer, this, this, subRenderer));

	subRenderer.reset();

	// Get project boot configuration
	BootConfiguration bootConfig = bootProject(*_gameDescription);

	_runtime->queueProject(bootConfig._projectDesc);
	Common::String mainSegmentPath = bootConfig._mainSegmentPath;

	preferredWidth = bootConfig._width;
	preferredHeight = bootConfig._height;

	switch (bootConfig._bitDepth) {
	case 1:
		preferredColorDepthMode = kColorDepthMode1Bit;
		break;
	case 2:
		preferredColorDepthMode = kColorDepthMode2Bit;
		break;
	case 4:
		preferredColorDepthMode = kColorDepthMode4Bit;
		break;
	case 8:
		preferredColorDepthMode = kColorDepthMode8Bit;
		break;
	case 16:
		preferredColorDepthMode = kColorDepthMode16Bit;
		break;
	case 32:
		preferredColorDepthMode = kColorDepthMode32Bit;
		break;
	default:
		error("Unsupported color depth mode");
		break;
	}

	switch (bootConfig._enhancedBitDepth) {
	case 1:
		enhancedColorDepthMode = kColorDepthMode1Bit;
		break;
	case 2:
		enhancedColorDepthMode = kColorDepthMode2Bit;
		break;
	case 4:
		enhancedColorDepthMode = kColorDepthMode4Bit;
		break;
	case 8:
		enhancedColorDepthMode = kColorDepthMode8Bit;
		break;
	case 16:
		enhancedColorDepthMode = kColorDepthMode16Bit;
		break;
	case 32:
		enhancedColorDepthMode = kColorDepthMode32Bit;
		break;
	default:
		error("Unsupported color depth mode");
		break;
	}

	// Figure out pixel formats
	Graphics::PixelFormat modePixelFormats[kColorDepthModeCount];
	bool haveExactMode[kColorDepthModeCount];
	bool haveCloseMode[kColorDepthModeCount];

	for (int i = 0; i < kColorDepthModeCount; i++) {
		haveExactMode[i] = false;
		haveCloseMode[i] = false;
	}

	{
		Common::List<Graphics::PixelFormat> pixelFormats = _system->getSupportedFormats();

		Graphics::PixelFormat clut8Format = Graphics::PixelFormat::createFormatCLUT8();

		for (const Graphics::PixelFormat &candidateFormat : pixelFormats) {
			ColorDepthMode thisFormatMode = kColorDepthModeInvalid;
			bool isExactMatch = false;
			if (candidateFormat.rBits() == 8 && candidateFormat.gBits() == 8 && candidateFormat.bBits() == 8) {
				isExactMatch = (candidateFormat.aBits() == 8);
				thisFormatMode = kColorDepthMode32Bit;
			} else if (candidateFormat.rBits() == 5 && candidateFormat.bBits() == 5 && candidateFormat.bytesPerPixel == 2) {
				if (candidateFormat.gBits() == 5) {
					isExactMatch = true;
					thisFormatMode = kColorDepthMode16Bit;
				} else if (candidateFormat.gBits() == 6) {
					isExactMatch = false;
					thisFormatMode = kColorDepthMode16Bit;
				}
			} else if (candidateFormat == clut8Format) {
				isExactMatch = true;
				thisFormatMode = kColorDepthMode8Bit;
			}

			if (thisFormatMode != kColorDepthModeInvalid && !haveExactMode[thisFormatMode]) {
				if (isExactMatch) {
					haveExactMode[thisFormatMode] = true;
					haveCloseMode[thisFormatMode] = true;
					modePixelFormats[thisFormatMode] = candidateFormat;
				} else if (!haveCloseMode[thisFormatMode]) {
					haveCloseMode[thisFormatMode] = true;
					modePixelFormats[thisFormatMode] = candidateFormat;
				}
			}
		}
	}

	// Figure out a pixel format.  First try to find one that's at least as good or better than the enhanced mode
	ColorDepthMode selectedMode = kColorDepthModeInvalid;

	for (int i = enhancedColorDepthMode; i < kColorDepthModeCount; i++) {
		if (haveExactMode[i] || haveCloseMode[i]) {
			selectedMode = static_cast<ColorDepthMode>(i);
			break;
		}
	}

	// If that fails, find one that's at least as good as the preferred mode
	if (selectedMode == kColorDepthModeInvalid) {
		for (int i = preferredColorDepthMode; i < kColorDepthModeCount; i++) {
			if (haveExactMode[i] || haveCloseMode[i]) {
				selectedMode = static_cast<ColorDepthMode>(i);
				break;
			}
		}
	}

	// If that fails, then try to find the best one available
	if (selectedMode == kColorDepthModeInvalid) {
		for (int i = preferredColorDepthMode - 1; i >= 0; i--) {
			if (haveExactMode[i] || haveCloseMode[i]) {
				selectedMode = static_cast<ColorDepthMode>(i);
				break;
			}
		}
	}

	if (selectedMode == kColorDepthModeInvalid)
		error("Couldn't resolve a color depth mode");

	debug(1, "Color depth mode %i, %s", static_cast<int>(selectedMode), modePixelFormats[selectedMode].toString().c_str());

	// Set up supported pixel modes
	for (int i = 0; i < kColorDepthModeCount; i++) {
		if (haveExactMode[i] || haveCloseMode[i])
			_runtime->setupDisplayMode(static_cast<ColorDepthMode>(i), modePixelFormats[i]);
	}

	ColorDepthMode fakeMode = selectedMode;
	if (selectedMode == enhancedColorDepthMode)
		fakeMode = preferredColorDepthMode;

	if (_gameDescription->gameID == GID_OBSIDIAN && ConfMan.getBool("mtropolis_mod_obsidian_widescreen"))
		preferredHeight = 360;

	// Set active mode
	_runtime->switchDisplayMode(selectedMode, fakeMode);
	_runtime->setDisplayResolution(preferredWidth, preferredHeight);

	initGraphics(preferredWidth, preferredHeight, &modePixelFormats[selectedMode]);

#ifdef MTROPOLIS_DEBUG_ENABLE
	if (ConfMan.getBool("mtropolis_debug_at_start")) {
		_runtime->debugSetEnabled(true);
	}
	if (ConfMan.getBool("mtropolis_pause_at_start")) {
		_runtime->debugBreak();
	}
#endif

	// Done reading boot configuration
	bootConfig = BootConfiguration();

	// Apply mods
	if (ConfMan.getBool("mtropolis_mod_minimum_transition_duration"))
		_runtime->getHacks().minTransitionDuration = 75;

	// Apply game-specific mods and hacks
	if (_gameDescription->gameID == GID_OBSIDIAN) {
		HackSuites::addObsidianQuirks(*_gameDescription, _runtime->getHacks());
		HackSuites::addObsidianBugFixes(*_gameDescription, _runtime->getHacks());
		HackSuites::addObsidianSaveMechanism(*_gameDescription, _runtime->getHacks());

		if (ConfMan.getBool("mtropolis_mod_auto_save_at_checkpoints"))
			HackSuites::addObsidianAutoSaves(*_gameDescription, _runtime->getHacks(), this);

		if (ConfMan.getBool("mtropolis_mod_obsidian_widescreen")) {
			_runtime->getHacks().reportDisplaySize = Common::Point(640, 480);

			HackSuites::addObsidianImprovedWidescreen(*_gameDescription, _runtime->getHacks());
		}
	} else if (_gameDescription->gameID == GID_MTI) {
		HackSuites::addMTIQuirks(*_gameDescription, _runtime->getHacks());
	} else if (_gameDescription->gameID >= GID_MSB_ANIMAL && _gameDescription->gameID <= GID_MSB_WHALES) {
		// Dialog lines swap in sound assets from other scenes
		_runtime->getHacks().allowAssetsFromOtherScenes = true;
	} else if (_gameDescription->gameID == GID_UNIT) {
		Palette pal;
		pal.initDefaultPalette(2);
		_runtime->setGlobalPalette(pal);
	}


	Common::ScopedPtr<TestInputScript> testInput;
	if (ConfMan.hasKey("mtropolis_test_input"))
		testInput.reset(new TestInputScript(Common::Path(ConfMan.get("mtropolis_test_input"), Common::Path::kNativeSeparator)));

	while (!shouldQuit()) {
		if (testInput)
			testInput->update(_system, _runtime.get(), this);

		handleEvents();

		Common::String openTitlePath;
		if (_runtime->takeOpenTitleRequest(openTitlePath)) {
			// Titles are opened relative to the folder of the current main segment
			static const char *const kStartupSegmentFolder = "<Startup Segment Folder>";

			Common::String relPath = openTitlePath;
			if (relPath.hasPrefixIgnoreCase(kStartupSegmentFolder))
				relPath = relPath.substr(strlen(kStartupSegmentFolder));
			for (uint i = 0; i < relPath.size(); i++)
				if (relPath[i] == ':' || relPath[i] == '\\')
					relPath.setChar('/', i);
			while (relPath.hasPrefix("/"))
				relPath.deleteChar(0);

			size_t lastSlash = mainSegmentPath.findLastOf('/');
			Common::String newMainSegment = (lastSlash == Common::String::npos) ? relPath : mainSegmentPath.substr(0, lastSlash + 1) + relPath;

			debug(1, "Opening title '%s' as '%s'", openTitlePath.c_str(), newMainSegment.c_str());

			BootConfiguration titleConfig = bootProject(*_gameDescription, newMainSegment);
			mainSegmentPath = titleConfig._mainSegmentPath;
			_runtime->queueProject(titleConfig._projectDesc);
		}

		if (!_runtime->runFrame())
			break;

		_runtime->drawFrame();
		_system->delayMillis(10);
	}

	_runtime.reset();

	return Common::kNoError;
}


void MTropolisEngine::pauseEngineIntern(bool pause) {
	Engine::pauseEngineIntern(pause);
}



bool MTropolisEngine::hasFeature(EngineFeature f) const {
	switch (f) {
	case kSupportsReturnToLauncher:
	case kSupportsSavingDuringRuntime:
		return true;
	default:
		return false;
	};
}

} // End of namespace MTropolis
