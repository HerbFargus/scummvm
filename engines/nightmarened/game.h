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

#ifndef NIGHTMARENED_GAME_H
#define NIGHTMARENED_GAME_H

#include "audio/mixer.h"
#include "common/array.h"

#include "nightmarened/display.h"
#include "nightmarened/resources.h"
#include "nightmarened/world.h"

namespace NightmareNed {

class Game;
class Ned;
class NightmareNedEngine;

/**
 * A scene record (0x38 bytes, registered by 0x459360 / 0x459690). The methods are
 * the record's function pointers m0..m10; every default does nothing and returns 0.
 */
class Scene {
public:
	Scene(Game *game, int id) : _game(game), _id(id), _parent(nullptr) {}
	virtual ~Scene();

	int getId() const { return _id; }
	Scene *getParent() const { return _parent; }
	const Common::Array<Scene *> &getSubScenes() const { return _subs; }
	/** 0x459690: the sub-scene's parent link is set here */
	void addSubScene(Scene *sub);

	virtual int init() { return 0; }                                // m0, +0x0c, at start-up
	virtual void shutdown() {}                                      // m1, +0x10
	virtual int load(int fromScene, int fromSub) { return 0; }      // m2, +0x14
	virtual int enter(int fromScene, int fromSub) { return 0; }     // m3, +0x18
	virtual int leave(int toScene, int toSub) { return 0; }         // m4, +0x1c
	virtual int unload(int toScene, int toSub) { return 0; }        // m5, +0x20
	virtual void suspend() {}                                       // m6, +0x24
	virtual int tick(uint32 input, uint32 now) { return 0; }        // m7, +0x28
	// m8..m10 (+0x2c..+0x34) save and restore scene state; not ported yet

protected:
	Game *_game;
	int _id;
	Scene *_parent;
	Common::Array<Scene *> _subs;
};

/**
 * The game's global state and services, shared by every scene and actor module:
 * resources, the primary surface, NAF handles, the clock, the scene graph, sound.
 */
class Game {
public:
	Game(NightmareNedEngine *vm);
	~Game();

	NightmareNedEngine *getEngine() { return _vm; }
	Resources &res() { return _res; }
	Display &display() { return _display; }
	const ExeImage &exe() const;
	Ned *ned() { return _ned; }
	void setNed(Ned *ned) { _ned = ned; }

	/** 0x445ed0: milliseconds since start-up (the clock the main loop passes in) */
	uint32 now() const;

	/** NAF by file-table index (0 = ned00000 .. 11 = ned99000); opened on demand, kept open */
	NafArchive *getNaf(int fileIndex);
	/** 0x4560c0 */
	void closeNaf(int fileIndex);
	/** Request list helpers (0x4586e0 / 0x458dc0) */
	Common::Array<Request> readList(uint32 va, int count) const { return Resources::readRequests(exe(), va, count); }
	bool loadList(int fileIndex, Common::Array<Request> &list, int scene);
	void freeList(Common::Array<Request> &list) { _res.freeList(list); }

	/** Registers a top-level scene (0x459360). Owned by Game. */
	void addScene(Scene *scene) { _scenes.push_back(scene); }
	/** 0x4598b0: m0 on every scene and sub-scene */
	void initScenes();
	/** 0x45a0f0: switches to (scene, sub); sub -1 = the top-level scene itself */
	void switchScene(int scene, int sub);
	Scene *getCurrentScene() const { return _current; }
	/** 0x45a2e0: the current top-level scene id, or -1 */
	int getCurrentSceneId() const;

	/** 0x45a310: the global input hook (DAT_0051f760), then the current scene's tick */
	typedef void (*InputHook)(Game *game, uint32 input, uint32 now, uint32 &outInput);
	void setInputHook(InputHook hook) { _inputHook = hook; }
	int tick(uint32 input, uint32 now);

	/** DAT_004c3b70 (0x445f50): whether O/Escape may open the options screen */
	bool _optionsAllowed;

	/**
	 * Plays PCM described by a WAVEFORMATEX (0x44d320). volume and pan are DirectSound
	 * hundredths of a dB (volume -10000..0, pan -10000..10000).
	 */
	void playVoice(Voice &voice, const byte *waveFormat, const byte *pcm, uint32 size, int volume, int pan, bool loop);
	/** 0x44d100 */
	void stopVoice(Voice &voice);
	bool isVoicePlaying(const Voice &voice) const;
	/** 0x455880 snd_play: plays a slot's auix sound once, stopping its own and stop's */
	bool playSlotSound(Slot &slot, Slot *stop);
	/** 0x455930 snd_stop */
	void stopSlotSound(Slot &slot);

private:
	Scene *findScene(int scene, int sub) const;

	NightmareNedEngine *_vm;
	Resources _res;
	Display _display;
	Ned *_ned;
	NafArchive *_nafs[12];
	Common::Array<Scene *> _scenes;
	Scene *_current;
	InputHook _inputHook;
	uint32 _startMillis;
};

} // End of namespace NightmareNed

#endif // NIGHTMARENED_GAME_H
