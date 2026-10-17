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

#ifndef MSPAM_SOUND_H
#define MSPAM_SOUND_H

#include "audio/mixer.h"
#include "common/array.h"
#include "common/str.h"

namespace Audio {
class AudioStream;
}

namespace MSPAM {

class MediaManager;

/**
 * The SND sound manager: sounds from the CMF (22 kHz 8-bit WAVs) in named
 * groups, plus the sounds of sprite path segments, all mixed by the ScummVM
 * mixer in place of the original's WaveMix-style "sndmixer".
 */
class SoundManager {
public:
	enum {
		kFlagAutoKill = 1,
		kFlagAutoLoop = 2
	};

	SoundManager(Audio::Mixer *mixer, MediaManager *media);
	~SoundManager();

	/** Play Sound; "Don't Mix Audio" stops the group's other sounds first */
	void play(uint32 id, const Common::String &group, bool exclusive, uint32 flags);
	/** Add Sound: the sound waits in its group for Play Group */
	void add(uint32 id, const Common::String &group, bool exclusive);
	void playGroup(const Common::String &group);
	void stopGroup(const Common::String &group);
	void pauseGroup(const Common::String &group, bool pause);
	void killGroup(const Common::String &group);
	void killAll();

	/** A sprite's channel: the sound of its current path segment */
	void playChannel(Audio::SoundHandle &handle, uint32 id);
	void stopChannel(Audio::SoundHandle &handle);
	bool isPlaying(const Audio::SoundHandle &handle) const;

	void update();

private:
	struct Sound {
		uint32 id;
		Common::String group;
		uint32 flags;
		bool started;
		Audio::SoundHandle handle;
	};

	Audio::AudioStream *makeStream(uint32 id, bool loop);
	bool start(Sound &sound);

	Audio::Mixer *_mixer;
	MediaManager *_media;
	Common::Array<Sound> _sounds;
};

} // End of namespace MSPAM

#endif // MSPAM_SOUND_H
