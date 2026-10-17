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

#ifndef BIGTUNA_SOUND_H
#define BIGTUNA_SOUND_H

#include "audio/mixer.h"

namespace BigTuna {

class Movie;

enum {
	kSoundChannels = 2
};

/**
 * Two sound channels, numbered 1 and 2 as in the scripts and score.
 * A sound resource is u8 bits, u16 LE rate, then unsigned mono PCM.
 */
class Sound {
public:
	Sound(Audio::Mixer *mixer);
	~Sound();

	// Keeps playing if the channel already plays this sound (FUN_0040c030)
	void play(int channel, Movie *movie, uint res);
	void stop(int channel);
	void stopAll();
	// On a movie change: both channels stop and go back to full volume
	void reset();
	bool isBusy(int channel) const;
	// Volume 0-255; fade in ticks (1/60 s)
	void setVolume(int channel, int volume, uint fade);
	int getVolume(int channel) const;
	void update(uint32 time);

private:
	struct Channel {
		Audio::SoundHandle handle;
		Movie *movie;
		uint res;
		int volume;
		int fadeFrom, fadeTo;
		uint32 fadeStart, fadeLength;
	};

	bool valid(int channel) const { return channel >= 1 && channel <= kSoundChannels; }
	void applyVolume(Channel &ch);

	Audio::Mixer *_mixer;
	Channel _channels[kSoundChannels];
	uint32 _time;
};

} // End of namespace BigTuna

#endif // BIGTUNA_SOUND_H
