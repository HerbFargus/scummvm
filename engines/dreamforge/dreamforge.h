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
#include "common/queue.h"
#include "common/random.h"
#include "common/serializer.h"
#include "common/rect.h"
#include "common/str.h"
#include "engines/advancedDetector.h"
#include "engines/engine.h"
#include "common/system.h"
#include "graphics/surface.h"

#include "dreamforge/walkmap.h"

namespace DreamForge {

class Actor;
struct Hotspot;
class Logic;
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

/** A click as the game queues it (C.PRG 0x13d50): where, and the cursor id there */
struct InputEvent {
	int y, x, code;
};

enum RecordKind {
	kKindLayer = 0,
	kKindHotspot = 1,
	kKindObject = 2
};

class DreamForgeEngine : public Engine {
	friend class Logic;

public:
	DreamForgeEngine(OSystem *syst, const ADGameDescription *gameDesc);
	~DreamForgeEngine() override;

	Common::Error run() override;
	bool hasFeature(EngineFeature f) const override;

	// Saves: room, flags, record states, held item, the puppies, the music
	bool canLoadGameStateCurrently(Common::U32String *msg = nullptr) override;
	bool canSaveGameStateCurrently(Common::U32String *msg = nullptr) override;
	Common::Error saveGameStream(Common::WriteStream *stream, bool isAutosave = false) override;
	Common::Error loadGameStream(Common::SeekableReadStream *stream) override;
	void syncGame(Common::Serializer &s);
	/** The in-game bone menu (C.PRG 0x32430, drawn by 0x31dc0, input 0x33c20) */
	void boneMenu();

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
	int32 _newGame;    // 0xe0fec: set while a new game's first scene loads
	bool _restoring;   // loading a save: the scene comes back without its handlers
	int32 _roomsSeen;  // 0x6d260

	// Act 1 room logic (act1.cpp)
	void act1Enter(int room, int area);
	void act1AfterLoad(int room, int area);
	/** Every act's room-code globals to their starting values */
	void initLogicVars();
	void roomEventDone();
	bool roomBanterAllowed() const;
	void hideActors(int which);
	void showActors(int which);
	void setActorPos(int actor, int y, int x);
	bool layerCursorAllowed(int layerId) const;

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
	/**
	 * Fades palette entries first..last between pal and a 6-bit colour in eight
	 * steps (C.PRG 0x51160 out, 0x51240 in). Out leaves the screen marked faded,
	 * which hides the cursor, until a fade in or setFaded(false) (0x4cbe0).
	 */
	void fadePalette(const byte *pal, int first, int last, int r, int g, int b, bool in);
	void setFaded(bool faded);
	bool _faded;  // bit 7 of 0xe15e0

	// Game state
	bool isFlagSet(uint flag) const;
	void setFlag(uint flag, bool on);
	int32 _hotspotState[kStateCount];  // a hotspot's state is its click count
	int32 _layerState[kStateCount];
	int32 _objectState[kStateCount];
	int _heldItem;      // 0x647c4[0]
	int _currentActor;  // 0x87320, the puppy being moved (1 or 2)
	int32 _counter3D;   // 0x6d264, bumped by script op 0x3d
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
	/** The puppies' random banter (C.PRG 0x1c720) */
	void banter(int set);
	void markDirty() { _dirty = true; }
	Logic *getLogic() const { return _logic; }

	/** Hotspot under a scene point, or nullptr (C.PRG 0x1f590) */
	const Record *hotspotAt(const Common::Point &p) const;
	/** Clicks a hotspot: counts the visit and runs its script (no walking yet) */
	void useHotspot(const Record &hotspot, bool walk = true);
	/** The walk-in region (mask 1) under a puppy's feet, or nullptr */
	const Record *regionAt(int y, int x) const;
	void checkRegions();
	/** The cursor for what's under the mouse (C.PRG 0x24120); kCursorWait while busy */
	int cursorFor(const Common::Point &screen) const;
	int maskCursor(uint16 mask, const Common::Point &screen, const Common::Point &p) const;
	int heldCursor() const;
	int puppyCursor() const;
	void setCursor(int id);
	void animateCursor();
	int cursorFrame() const;
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

	// Clicks (input.cpp)
	void postInput(int y, int x, int code);
	/** Test hooks: a click at a scene point, played out until the puppies are idle */
	void clickAndWait(int y, int x);
	void processInput();
	void dispatchInput(const InputEvent &e);
	/** Calls the current act's entry in a per-act handler table (C.PRG 0xdf020 ...) */
	int callActHandler(uint32 table, int a, int b, int c, int none);
	void dropHeldItem();
	int hotspotIndexAt(int y, int x, uint16 mask) const;
	int layerIndexAt(int y, int x, uint16 mask) const;
	int objectIndexAt(int y, int x, bool temporary, uint16 mask) const;
	Record *recordById(int kind, int id);
	int32 *recordState(int kind, int id);

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
	Logic *_logic;
	WalkMap _walkMap;
	Common::HashMap<uint, AnimFrames *> _actorFrames;
	uint32 _nextStep;
	uint32 _shotEvery, _nextShot, _shotCount;  // test hook: periodic screenshots
	Common::String _shotDir;
	bool _inScript;
	Common::Queue<InputEvent> _input;
	bool _checkPuppies;  // 0xdf044: clicks may hit the puppies
	void checkArrivals();
	int preFilterClick(int y, int x);
	int runVerb(int verb, int kind, int id, int y, int x, int code);
	int verbClick(int verb, int y, int x, int code);
	void lookClick(int y, int x, int code);
	void walkClick(int y, int x, bool arrow);
	void useClick(int y, int x, int code);
	int callAct2Handler(uint32 table, int a, int b, int c, int none);
	// Idle logic (idle.cpp, 0x10890)
	void idleStep();
	bool actorBusy(int g) const;
	bool actVeto(int g);
	void resetFidget(int g);
	void fidget(int g);
	void idleBanter();
	void faceGuy1();
	void wander();
	uint32 _fidgetAt[2];          // 0x8737c
	Common::Point _fidgetFrom[2];  // where the puppy stood when the fidget timer started
	int _fidgetDir[2];
	uint32 _idleBanterAt;         // 0xdf040
	Common::Array<int> _wanderSpots;  // 0x88330: the spots puppy 2 picks from
	void drawScene();
	void drawSurface(const Graphics::Surface &surf, const Common::Point &pos, bool mirror, bool keyed);
	void startObject(SceneObject &o);
	void advanceObject(SceneObject &o);
	void updateObjects(bool force);
	// Path objects (objects.cpp: 0x22e20 launch, 0x22590 step)
	bool launchPath(SceneObject &o);
	void stepPath(SceneObject &o);
	void sizePath(SceneObject &o);
	void shufflePathDice(bool reset);
	uint32 _pathTick;      // 0xdf058, updates so far
	byte _pathDice[100];   // 0xeb248: 1..100 shuffled
	int _pathDie;          // 0xeb2ac
	uint32 ticks() const { return g_system->getMillis() * 182 / 10000; }  // the 18.2 Hz game timer
	void shadeActors();
	bool conditionsHold(const byte *conds, uint count) const;
	byte _flags[kFlagCount / 8];
	Common::RandomSource _rnd;
};

} // End of namespace DreamForge

#endif // DREAMFORGE_DREAMFORGE_H
