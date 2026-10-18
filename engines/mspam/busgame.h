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

#ifndef MSPAM_BUSGAME_H
#define MSPAM_BUSGAME_H

#include "audio/mixer.h"
#include "common/array.h"
#include "common/hashmap.h"
#include "common/hash-str.h"
#include "common/keyboard.h"
#include "common/str.h"
#include "graphics/surface.h"

namespace Audio {
class QueuingAudioStream;
class SeekableAudioStream;
}

namespace MSPAM {

/**
 * The planet-surface jumping game by Lobotomy Software (BUSGAME1.EX_ with its
 * sprite library BUSGAME2.DLL), which the title runs through "Exec: Start Game".
 * The game data is <PLANET>.BIN (or .4MB, the same without music), its sounds are
 * WAVE resources of BUSGAME1.EX_. A port of the original's code, see NOTES.md.
 */
class BusGame {
public:
	/** The arguments are those of the title's command line "busgame1 -p <planet>", -x and -l */
	BusGame(Audio::Mixer *mixer, const Common::String &planet, bool lizIntro, bool lowMemory);
	~BusGame();

	bool load();
	void keyDown(Common::KeyCode key);
	void keyUp(Common::KeyCode key);
	void charTyped(uint16 ascii);
	void mouseDown(int x, int y);
	void update(uint32 now);
	void render(Graphics::Surface &dst);
	const byte *getPalette() const { return _palette; }
	bool isDone() const { return _done; }
	/** The LobotomyGameResult value: 0 left without the coin, 1 won, 2 quit, 100+ sprite errors */
	int getResult() const;

private:
	struct Image {
		int w, h;
		Common::Array<byte> pixels;
		Common::Array<byte> opaque;
	};
	struct Frame {
		int16 dx, dy, dur;
		const Image *img;
	};
	struct Sequence {
		Common::String name;
		bool centered;
		Common::Array<Frame> frames;
	};
	struct SpriteType {
		Common::String name;
		const Sequence *seq;
		int16 pri;
		float vx, vy, ax, ay;
		int16 offMode, offRect[4];
		int16 trigFrame, trigTimer;
		int16 offAct, animAct;
	};
	enum Callback {
		kCbNone,
		kCbPlayer,        ///< 1000:418a
		kCbPlayerAnimEnd, ///< 1000:535a
		kCbDelete,        ///< 1000:536e
		kCbBounce,        ///< 1000:5386, stars and coins
		kCbArrow,         ///< 1000:545c
		kCbPlatform,      ///< 1000:54a8
		kCbBlock,         ///< 1000:5924
		kCbLava,          ///< 1000:5a82
		kCbCloud,         ///< 1000:5c0e
		kCbNumber,        ///< 1000:5c86
		kCbLiz,           ///< 1000:5cde
		kCbBalloon,       ///< 1000:6194
		kCbMartian        ///< 1000:62aa
	};
	/** A node of the sprite library's drawing and update list, ordered by priority */
	struct Node {
		bool isLayer;
		int16 pri;
		Node *prev, *next;
		bool linked;
	};
	struct Layer : Node {
		Common::String name;
		const Image *img;
		int16 x, y;
		bool visible;
	};
	struct Sprite : Node {
		const SpriteType *type;
		const Sequence *seq;
		float x, y, vx, vy, ax, ay;
		bool animating, visible;
		int16 frameNo, timer, trigFrame, trigTimer;
		int16 offMode, offRect[4], offAct, animAct;
		int16 ix, iy, w, h;
		const Frame *cur;
		int frameCount;
		Callback move, off, anim;
		// The game's word at +0x75: player state and direction, a counter, the
		// platform a block rests on, a number's digit
		int16 state, dir;
		int32 counter;
		Sprite *restOn;
		const Frame *digit;
	};
	enum {
		kObjWords = 18,
		kMaxObjects = 70,
		kGridW = 80,
		kGridH = 52
	};
	struct Object {
		int16 w[kObjWords]; ///< type, screen, left, top, right, bottom, -, -, flags, present, active, x1, y1, x2, y2, speed, count, reset
		Sprite *spr;
	};
	struct Level {
		byte grid[6][kGridH][kGridW];
		int16 links[6][4];  ///< up, down, left, right screens (1-based, 0 = none)
		int16 count;
		Object obj[kMaxObjects];
		int16 start[4];     ///< screen, x, y, level type (2: slippery)
	};

	// File access
	const byte *findRecord(const char *type, const char *name, uint32 *size = nullptr) const;
	const Image *getImage(const Common::String &name);
	bool parseAction();
	bool loadLevel();
	bool loadSounds();
	const SpriteType *findType(const Common::String &name) const;

	// Sprite library (BUSGAME2)
	Sprite *createSprite(const Common::String &type);
	void launchSprite(Sprite *s);
	void deleteSprite(Sprite *&s);
	void insertNode(Node *n);
	void unlinkNode(Node *n);
	void setFrame(Sprite *s);
	void updateRect(Sprite *s);
	bool isOffscreen(const Sprite *s) const;
	void updateSprite(Sprite *s);
	void call(Callback cb, Sprite *&s);
	void moveAction();
	void drawImage(Graphics::Surface &dst, const Image *img, int x, int y);

	// The game (BUSGAME1)
	void tick();
	void quit();
	Object *findObject(const Sprite *s);
	static bool isPlatformType(const Object &o);
	void setObjectRect(Object &o, int x, int y);
	void resetPlatformCounters();
	void setupMotion(Object &o);
	void createObject(Object &o);
	void spawnObjects();
	void deleteObjectSprite(Object &o);
	void deleteObjectSprites();
	void showLayers();
	void changeScreen();
	void requestScreen(int screen);
	void resetPhysics();
	void restartLevel();
	void setDigit(Sprite *s, int n);
	void showScore();
	void showAir();
	void stopPlatforms();
	void lizArrive();
	void jetFollow();
	void deleteJet();
	void playerState(bool replace);
	void ouch();
	int touch(Object &o);
	bool pushBlock(float dx);
	float centerX(const Sprite *s) const;
	float landOnPlatform(Sprite *s);
	float gridFloor(Sprite *s);
	int msRand();

	void cbPlayer(Sprite *&self);
	void cbBounce(Sprite *&s);
	void cbArrow(Sprite *&s);
	void cbPlatform(Sprite *&s);
	void cbBlock(Sprite *&s);
	void cbLava(Sprite *&s);
	void cbCloud(Sprite *&s);
	void cbLiz(Sprite *&s);
	void cbBalloon(Sprite *&s);
	void cbMartian(Sprite *&s);

	// Sound (WaveMix channels 0 effects, 1 music, 2 jet pack, 3 Liz and victory)
	void playSound(int n);
	void stopChannel(int channel);
	void lizTalk();
	void startSong();
	void updateMusic(uint32 now);
	Audio::SeekableAudioStream *makeWave(const byte *data, uint32 size);

	Audio::Mixer *_mixer;
	Common::String _planet;
	bool _lizIntro, _lowMemory;
	byte *_data;
	uint32 _dataSize;
	byte _palette[256 * 3];
	byte _bgColor;
	Common::HashMap<Common::String, Image *> _images;
	Common::Array<Sequence *> _seqs;
	Common::Array<SpriteType *> _types;
	Common::Array<Layer *> _layers;
	Common::Array<Sprite *> _garbage;
	Node *_head;
	Node *_nextUpdate;
	Common::Array<Common::Array<byte> > _effects;  ///< JUMPSOUND JETSOUND GRABSOUND STARSOUND VICTORY OUCH BALLOON LIZTALK
	Common::Array<const byte *> _songParts;
	Common::Array<uint32> _songSizes;
	Common::String _songOrder;
	Audio::SoundHandle _channels[4];
	Audio::QueuingAudioStream *_music;
	uint32 _musicRestart;
	uint32 _victoryEnd;
	uint32 _now;
	uint32 _nextTick;
	uint32 _seed;

	Level _level, _backup;
	int16 _heights[640];
	Sprite *_player, *_jet, *_riding, *_pushing, *_overlay, *_liz;
	Sprite *_score[6], *_airDigits[3], *_whatsit;
	uint32 _keys, _prevKeys;
	float _vx, _vy, _target, _grav, _planetGrav;
	float _air, _floorLimit, _lizTX, _lizTY, _lizVy;
	int32 _airShown, _score32;
	int _planetIndex, _screen, _pendingScreen;
	bool _screenChange, _screenSolid;
	int _lizState;
	bool _lizCarry, _running, _stateChanged, _pushFlag, _fellOff;
	uint32 _powerups;
	int _result;
	uint32 _ticks, _lastOuch;
	bool _done;
};

} // End of namespace MSPAM

#endif // MSPAM_BUSGAME_H
