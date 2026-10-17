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

#ifndef DREAMFORGE_DFA_H
#define DREAMFORGE_DFA_H

#include "common/stream.h"
#include "graphics/surface.h"

namespace DreamForge {

/**
 * DreamForge's DFIA animation format, the one FFmpeg calls Chronomaster DFA.
 * The chunk decoders follow FFmpeg's libavcodec/dfa.c (Konstantin Shishkov,
 * LGPL 2.1+).
 *
 * Header (0x80 bytes): "DFIA", u16 version, u16 frames, u16 width, u16 height,
 * u32 milliseconds per frame. Each frame is a run of chunks with 12-byte
 * headers (tag, u32 size, u32 type), ended by an EOFR chunk (type 0).
 */
class DFADecoder {
public:
	DFADecoder();
	~DFADecoder();

	/** Takes ownership of the stream */
	bool load(Common::SeekableReadStream *stream);
	void close();
	bool isLoaded() const { return _stream != nullptr; }

	uint16 getWidth() const { return _width; }
	uint16 getHeight() const { return _height; }
	uint16 getFrameCount() const { return _frameCount; }
	uint32 getFrameTime() const { return _frameTime; }
	int getCurFrame() const { return _curFrame; }
	bool endOfVideo() const { return _curFrame + 1 >= _frameCount; }

	/** Back to before the first frame (the picture is kept, as in the original's loops) */
	void rewind();
	/** Decodes the next frame; returns nullptr at the end or on bad data */
	const Graphics::Surface *decodeNextFrame();
	const Graphics::Surface *getSurface() const { return &_surface; }

	/** 8-bit palette, valid once a PAL1 chunk has been seen */
	const byte *getPalette() const { return _palette; }
	bool paletteChanged() { bool c = _paletteDirty; _paletteDirty = false; return c; }

private:
	bool decodeChunk(uint32 type, Common::SeekableReadStream &chunk);

	Common::SeekableReadStream *_stream;
	uint16 _version, _frameCount, _width, _height;
	uint32 _frameTime;
	int _curFrame;
	Graphics::Surface _surface;
	byte _palette[256 * 3];
	bool _paletteDirty;
};

} // End of namespace DreamForge

#endif // DREAMFORGE_DFA_H
