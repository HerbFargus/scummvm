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

#ifndef NIGHTMARENED_MEDIA_H
#define NIGHTMARENED_MEDIA_H

#include "audio/mixer.h"
#include "common/array.h"
#include "common/str.h"

#include "nightmarened/resources.h"

namespace NightmareNed {

class Game;

/**
 * The area music voice (0x455960) and the TV clip player (0x479ff0 / 0x47a3e0): the
 * transitions between the attic and the dream scenes. A clip is a 640x480 Smacker file
 * decoded frame by frame into NED01000's frame bitmap, shown with the clip's own NAF
 * palette at trans_fps, while the scene waits.
 */
class Media {
public:
	explicit Media(Game *game);
	~Media();

	/** 0x455960: loops the WAV at file-table index (the area music); replaces the old one */
	void playMusic(int fileIndex);
	/** 0x455960(NULL) */
	void stopMusic();
	/** 0x455650: level 0..10 (ned.ini MusicVolume) is -30..0 dB; 0 stops the music, raising it restarts it */
	void setMusicVolume(int level);

	/** 0x479ff0: loads the frame bitmap (0x4b4a68) and clip's palette bitmap (0x4b4b94 + clip * 0x24) */
	int loadClip(int clip);
	typedef void (*FrameCallback)(void *user, int frame, int count, uint32 now);
	/**
	 * 0x47a3e0: plays clip (file 0x4b4af4[clip]) on the primary surface at draw order z,
	 * one frame per 1000 / trans_fps ms, then frees what loadClip loaded. Blocks, polling events.
	 */
	int playClip(int clip, int z, FrameCallback cb = nullptr, void *user = nullptr);
	/**
	 * 0x47aa00: plays clip at the video's own frame rate at draw order z, with no music;
	 * only quitting stops it. Frees what loadClip loaded.
	 */
	int playClipFull(int z, int clip, bool music = false);
	/** 0x47a6b0: the same, with the TV-room music (file 0x51) playing during the clip */
	int playClipWithMusic(int z, int clip) { return playClipFull(z, clip, true); }

	/** File-table entry name (0x4a05d0 + index * 0x14, +0x0c) */
	Common::String getFileName(int index) const;

	int16 _transFps;      // DAT_005238c4 (ned.ini trans_fps, default 15)
	int _installLevel;    // DAT_004bd738 (ned.ini Install_Level); 0 takes the still-picture path 0x47a110

private:
	void freeClip();

	Game *_game;
	Common::Array<Request> _frameList; // 0x4b4a68: DAT_00520200, the 640x480 frame bitmap
	Common::Array<Request> _clipList;  // 0x4b4b94 + clip * 0x24: palette into DAT_00520208
	int _loadedClip;
	Audio::SoundHandle _music;         // DAT_004c613c
	int _musicFile;                    // DAT_004c6038: the last music asked for
	int _musicLevel;                   // DAT_004c6144 as a 0..10 level
};

} // End of namespace NightmareNed

#endif // NIGHTMARENED_MEDIA_H
