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
#include "common/hashmap.h"
#include "common/random.h"
#include "common/rect.h"
#include "common/str.h"
#include "engines/advancedDetector.h"
#include "engines/engine.h"
#include "common/system.h"
#include "graphics/surface.h"

#include "dreamforge/walkmap.h"

namespace DreamForge {

class Actor;
struct AnimFrames;
struct Record;
struct SceneObject;
class ResourceArchive;
class Scene;
class Sound;
struct Sprite;

enum {
	kScreenWidth = 640,
	kScreenHeight = 480,
	kArchiveCount = 7,  // PIG0.USR .. PIG6.USR
	kFlagCount = 512,   // the flag bits at C.PRG 0x64834
	kStateCount = 1024, // saved state per object, hotspot and layer id (0x6a250, 0x6b250, 0x6c250)
	kStepMs = 100       // one game step (0x10840: two timer ticks)
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
	Sound *getSound() const { return _sound; }
	/** Plays CINE\<number>.DFA with its sound cues, then restores the scene; 1 if skipped */
	int playCutscene(int number);

	// The start of the game (game.cpp)
	void playIntro();
	int titleMenu();
	void newGame();
	void randomizeFlags();
	bool loadMenuPalette(int n, byte *pal) const;
	void drawSprite(uint archive, uint index, int x, int y);
	bool _newGame;     // 0xe0fec: set while a new game's first scene loads
	int _roomsSeen;    // 0x6d260

	// Act 1 room logic (act1.cpp)
	void act1Enter(int room, int area);
	void act1AfterLoad(int room, int area);
	void roomEventDone();
	void hideActors(int which);
	void showActors(int which);
	void setActorPos(int actor, int y, int x);

	/**
	 * Lip sync demo until actors exist: plays a voice line with a puppy's talking
	 * animation (PIG2, on black with its own palette) driven by its visemes.
	 * shotDir, if set, gets a PNG per step.
	 */
	bool sayDemo(int voiceId, int actor, int slot, int view, const Common::String &shotDir = Common::String());
	ResourceArchive *getArchive(uint n) const { return n < kArchiveCount ? _archives[n] : nullptr; }

	void setScroll(int x);
	int getScroll() const { return _scrollX; }
	bool _showLayers;
	Common::Array<bool> _layerVisible;

	void redraw(bool present = true);

	// Game state
	bool isFlagSet(uint flag) const;
	void setFlag(uint flag, bool on);
	int32 _hotspotState[kStateCount];  // a hotspot's state is its click count
	int32 _layerState[kStateCount];
	int32 _objectState[kStateCount];
	int _heldItem;      // 0x647c4[0]
	int _currentActor;  // 0x87320, the puppy being moved (1 or 2)
	int _counter3D;     // 0x6d264, bumped by script op 0x3d
	uint getRandom15() { return _rnd.getRandomNumber(0x7FFF); }

	// Puppies
	Actor *getActor(int n) const { return n == 1 || n == 2 ? _actors[n - 1] : nullptr; }
	const WalkMap &getWalkMap() const { return _walkMap; }
	int getRefScale() const;
	const AnimFrames *getActorFrames(uint pig2Index);
	/** A route over the scene's waypoint graph, not counting the ends (0x1aae0) */
	bool findWaypointPath(int y0, int x0, int y1, int x1, Common::Array<Common::Point> &via) const;
	/** Puts both puppies somewhere sensible after a debug jump */
	void placeActorsDefault();
	/** Runs one game step on time: events, the puppies, the screen */
	void runStep();
	/** Blocking puppy actions for scripts and room logic */
	void actorWalk(int actor, int y, int x, int dir);
	void actorTurn(Actor &a, int dir);
	void actorPlay(Actor &a, int slot);
	void actorSay(int actor, int voiceId, bool quick);

	/** Hotspot under a scene point, or nullptr (C.PRG 0x1f590) */
	const Record *hotspotAt(const Common::Point &p) const;
	/** Clicks a hotspot: counts the visit and runs its script (no walking yet) */
	void useHotspot(const Record &hotspot, bool walk = true);
	/** The walk-in region (mask 1) under a puppy's feet, or nullptr */
	const Record *regionAt(int y, int x) const;
	void checkRegions();
	/** The cursor for what's under the mouse (C.PRG 0x24120); kCursorWait while busy */
	int cursorFor(const Common::Point &screen) const;
	void setCursor(int id);
	void animateCursor();
	int _cursorId, _cursorFrame;
	int _currentRegion;
	uint32 _quitAt;
	/** The first hotspot with this id in the current scene, or nullptr */
	const Record *findHotspot(int id) const;
	/**
	 * An animation played in the room (script op 0x0f, C.PRG 0x23af0): CINE\<cine>.DFA
	 * drawn opaque at (left, top) in scene coordinates, with its .ACT cues.
	 */
	bool playSceneAnim(int cine, int top, int left, bool flip, int delay, int depth, bool noWait, int hideActor);
	/** Keeps the screen and events going until the voice line ends */
	void waitForVoice();
	bool saveShot(const Common::String &path);

private:
	bool mountData();
	void setupCursor();
	void mainLoop();
	void handleEvents();
	void dumpAllScenes(const Common::String &dir);
	void runCutsceneHook();

	const ADGameDescription *_gameDescription;
	ResourceArchive *_archives[kArchiveCount];
	Sound *_sound;
	Scene *_scene;
	Sprite *_cursors;
	Graphics::Surface _screen;
	int _scrollX;
	Common::Point _mousePos;
	bool _dirty;
	Actor *_actors[2];
	WalkMap _walkMap;
	Common::HashMap<uint, AnimFrames *> _actorFrames;
	uint32 _nextStep;
	uint32 _shotEvery, _nextShot, _shotCount;  // test hook: periodic screenshots
	Common::String _shotDir;
	bool _inScript;
	void drawScene();
	void startObject(SceneObject &o);
	void advanceObject(SceneObject &o);
	void updateObjects(bool force);
	uint32 ticks() const { return g_system->getMillis() * 182 / 10000; }  // the 18.2 Hz game timer
	void shadeActors();
	bool conditionsHold(const byte *conds, uint count) const;
	byte _flags[kFlagCount / 8];
	Common::RandomSource _rnd;
};

} // End of namespace DreamForge

#endif // DREAMFORGE_DREAMFORGE_H
