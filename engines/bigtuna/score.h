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

#ifndef BIGTUNA_SCORE_H
#define BIGTUNA_SCORE_H

#include "common/array.h"
#include "common/rect.h"
#include "common/str.h"
#include "common/stream.h"

namespace BigTuna {

class BigTunaEngine;
class Movie;

enum FilmOp {
	kFilmSpriteName = 0x00,
	kFilmCursor = 0x01,
	kFilmClear = 0x02,
	kFilmGoto = 0x03,
	kFilmGoMovie = 0x04,
	kFilmComment = 0x05,
	kFilmPalette = 0x06,
	kFilmSprite = 0x07,
	kFilmScript = 0x08,
	kFilmSound1 = 0x09,
	kFilmSound2 = 0x0A,
	kFilmTempo = 0x0B,
	kFilmVolume = 0x0C,
	kFilmWait = 0x0D,
	kFilmVideo = 0x0E,
	kFilmChannelProp = 0x0F,
	kFilmStage = 0x10,
	kFilmNext = 0x11,
	kFilmWaitSound = 0x14,
	kFilmOrigin = 0x16
};

struct FilmCommand {
	byte op;
	int32 a, b, c, d;
	Common::String s1, s2;
};

/**
 * One step of the score: the ops up to 0x11 (one frame) or 0x0D n (n frames).
 * Frame numbers count every frame of a hold, as labels do.
 */
struct Step {
	uint32 offset;
	uint frame;
	uint length;
	Common::Array<FilmCommand> commands;
};

class Score {
public:
	bool load(Common::SeekableReadStream &stream);

	uint getStepCount() const { return _steps.size(); }
	const Step &getStep(uint index) const { return _steps[index]; }
	uint getFrameCount() const;
	// The step that holds a frame number
	int findStep(uint frame) const;
	uint findFrameByOffset(uint32 offset) const;

private:
	static uint32 readVarint(Common::SeekableReadStream &stream);
	static int32 readSignedVarint(Common::SeekableReadStream &stream);
	static Common::String readPString(Common::SeekableReadStream &stream);

	Common::Array<Step> _steps;
};

class ScorePlayer;

struct Sprite {
	Sprite() : exists(false), visible(false), puppet(false), member(0), x(0), y(0), script(0),
		mouseInside(false), loop(nullptr), constraint(-32767, -32767, 32767, 32767) {}

	bool exists;
	bool visible;
	bool puppet;
	uint member; // resource number
	int x, y;
	uint script; // resource number
	bool mouseInside;
	ScorePlayer *loop;
	Common::Rect constraint;
};

class ScorePlayer {
public:
	ScorePlayer(BigTunaEngine *vm, Movie *movie, Score *score, bool isLoop);
	~ScorePlayer();

	// Applies the next step; returns its length in frames, 0 at the end of the score
	uint step();
	// The next step will be the one holding this frame; sprite state is rebuilt up to it
	void seek(uint frame);
	bool seekLabel(const Common::String &label);
	void rewind();

	// The player's frame counter: the last frame of the current step
	uint getFrame() const;
	uint getFrameCount() const { return _score->getFrameCount(); }
	uint getTempo() const { return _tempo; }
	uint getFrameScript() const { return _frameScript; }
	Movie *getMovie() const { return _movie; }
	int getOriginX() const { return _originX; }
	int getOriginY() const { return _originY; }
	bool hasLoops() const;
	// Generation counter, bumped by every seek: lets the event loop see a go
	uint getSeekCount() const { return _seekCount; }

	uint getSpriteCount() const { return _sprites.size(); }
	Sprite *findSprite(uint channel);
	Sprite &getSprite(uint channel);
	const Common::Array<Sprite> &getSprites() const { return _sprites; }
	void setMember(Sprite &sprite, uint member);
	void setVisible(Sprite &sprite, bool visible);
	void setLoc(Sprite &sprite, int x, int y, bool constrain);
	Common::Rect getSpriteRect(const Sprite &sprite) const;
	bool hitTest(const Sprite &sprite, int x, int y) const;

	int findLabelFrame(const Common::String &label) const;
	int markerFrame(int offset) const;

	void stepLoops();

private:
	void apply(const FilmCommand &cmd, bool seeking);
	void destroySprite(Sprite &sprite);

	BigTunaEngine *_vm;
	Movie *_movie;
	Score *_score;
	bool _isLoop;
	int _current;
	uint _next;
	uint _tempo;
	uint _frameScript;
	int _originX, _originY;
	uint _seekCount;
	Common::Array<Sprite> _sprites;
};

} // End of namespace BigTuna

#endif // BIGTUNA_SCORE_H
