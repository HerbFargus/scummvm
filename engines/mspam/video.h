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

#ifndef MSPAM_VIDEO_H
#define MSPAM_VIDEO_H

#include "common/path.h"
#include "graphics/surface.h"

namespace Video {
class AVIDecoder;
}

namespace MSPAM {

/** VID element flags (VID::SETFLAGS) that change playback */
enum VideoFlags {
	kVideoFullScreen = 0x02,  ///< MCIAVI full screen play (the title's .pl property)
	kVideoNaturalSize = 0x08, ///< the AVI keeps its size in the window, else it's stretched to it
	kVideoFrames = 0x10       ///< positions are frames, else milliseconds
};

/**
 * An AVI as the VID element's MCI window plays it: the frames are mapped to
 * the 8-bit screen palette (Cinepak with the Video for Windows dither, other
 * true-colour codecs with an ordered dither, 8-bit ones through their palette).
 */
class VideoPlayer {
public:
	VideoPlayer();
	~VideoPlayer();

	bool open(const Common::Path &path, const byte *palette);
	void close();
	bool isOpen() const { return _decoder != nullptr; }

	/** Plays from a position to another (inclusive, -1 = the end) */
	void play(int32 from, int32 to, bool frames);
	void stop();
	void pause();
	void resume();
	/** Moves to a position and shows its picture */
	void seek(int32 pos, bool frames);

	/** The engine pauses (the global main menu, dialogs): the decoder's clock stops, nested */
	void pauseEngine(bool pause);

	bool isPlaying() const { return _state == kPlaying; }
	bool isPaused() const { return _state == kPaused; }
	/** Decodes the frames that are due; true when playback reached its end */
	bool update();
	/** The current picture in screen colours, the AVI's own size */
	const Graphics::Surface *getFrame() const { return _hasFrame ? &_frame : nullptr; }

private:
	enum State {
		kStopped,
		kPlaying,
		kPaused
	};

	void decodeFrame();
	void convertFrame(const Graphics::Surface &src);
	void buildColorTable();

	Video::AVIDecoder *_decoder;
	State _state;
	Graphics::Surface _frame;
	bool _hasFrame;
	byte _palette[256 * 3];  ///< the screen palette the frames are mapped to
	byte _videoPalette[256 * 3];
	byte _remap[256];        ///< 8-bit AVI colour to screen colour
	byte *_rgbTable;         ///< RGB555 to screen colour, for true-colour frames
};

} // End of namespace MSPAM

#endif // MSPAM_VIDEO_H
