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

#ifndef BIGTUNA_BIGTUNA_H
#define BIGTUNA_BIGTUNA_H

#include "common/array.h"
#include "common/str.h"
#include "engines/advancedDetector.h"
#include "engines/engine.h"

namespace BigTuna {

class Movie;
class ScorePlayer;
class Sound;
class Stage;

class BigTunaEngine : public Engine {
public:
	BigTunaEngine(OSystem *syst, const ADGameDescription *gameDesc);
	~BigTunaEngine() override;

	Common::Error run() override;
	bool hasFeature(EngineFeature f) const override;

	bool isV1() const;
	bool loadMovie(const Common::String &name);
	void setPalette(Movie *movie, uint res);
	// Empty movie: the current one. Empty label: its first frame.
	void requestJump(const Common::String &movie, const Common::String &label);

	Movie *getMovie() const { return _movie; }
	ScorePlayer *getPlayer() const { return _player; }
	void redraw();
	bool saveShot(const Common::String &path);

	Sound *_sound;

private:
	void handleJump();
	void sweepMovies();
	void checkShot();

	const ADGameDescription *_gameDescription;
	Movie *_movie;
	ScorePlayer *_player;
	Stage *_stage;
	byte _palette[256 * 3];

	bool _jumpPending;
	Common::String _jumpMovie, _jumpLabel;

	// Test hooks: bigtuna_frames, bigtuna_shots, bigtuna_shot_dir, bigtuna_fast
	uint _stopAfter;
	uint _framesPlayed;
	Common::Array<uint> _shots;
	Common::String _shotDir;
	bool _fast;
};

} // End of namespace BigTuna

#endif // BIGTUNA_BIGTUNA_H
