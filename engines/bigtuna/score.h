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

struct Frame {
	uint32 offset;
	Common::Array<FilmCommand> commands;
};

/**
 * A score ("film"): a delta stream of film operations. Each frame ends with
 * 0x11 (one frame) or 0x0D n (n frames).
 */
class Score {
public:
	bool load(Common::SeekableReadStream &stream);

	uint getFrameCount() const { return _frames.size(); }
	const Frame &getFrame(uint frame) const { return _frames[frame - 1]; }
	uint findFrameByOffset(uint32 offset) const;

private:
	static uint32 readVarint(Common::SeekableReadStream &stream);
	static int32 readSignedVarint(Common::SeekableReadStream &stream);
	static Common::String readPString(Common::SeekableReadStream &stream);

	Common::Array<Frame> _frames;
};

class ScorePlayer;

struct Sprite {
	Sprite() : member(0), x(0), y(0), script(0), loop(nullptr) {}

	uint member; // resource number
	int x, y;
	uint script; // resource number
	ScorePlayer *loop;
};

class ScorePlayer {
public:
	ScorePlayer(BigTunaEngine *vm, Movie *movie, Score *score, bool isLoop);
	~ScorePlayer();

	void reset();
	// Moves to the next frame; false at the end of the score
	bool advance();
	void gotoFrame(uint frame);
	bool gotoLabel(const Common::String &label);

	uint getFrame() const { return _frame; }
	uint getFrameCount() const { return _score->getFrameCount(); }
	uint getTempo() const { return _tempo; }
	uint getFrameScript() const { return _frameScript; }
	Movie *getMovie() const { return _movie; }
	const Common::Array<Sprite> &getSprites() const { return _sprites; }
	int getOriginX() const { return _originX; }
	int getOriginY() const { return _originY; }

private:
	void applyFrame(uint frame, bool seeking);
	void apply(const FilmCommand &cmd, bool seeking);
	Sprite &getSprite(uint channel);
	void setMember(Sprite &sprite, uint member);
	void clearSprite(Sprite &sprite);

	BigTunaEngine *_vm;
	Movie *_movie;
	Score *_score;
	bool _isLoop;
	uint _frame;
	uint _tempo;
	uint _frameScript;
	int _originX, _originY;
	int _waitSoundChannel;
	Common::Array<Sprite> _sprites;
};

} // End of namespace BigTuna

#endif // BIGTUNA_SCORE_H
