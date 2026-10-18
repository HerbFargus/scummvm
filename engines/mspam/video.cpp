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

#include "common/textconsole.h"
#include "video/avi_decoder.h"

#include "mspam/video.h"

namespace MSPAM {

static byte findNearest(const byte *palette, int r, int g, int b) {
	uint best = 0, bestDist = 0xffffffff;
	for (uint i = 0; i < 256; i++) {
		int dr = palette[i * 3] - r, dg = palette[i * 3 + 1] - g, db = palette[i * 3 + 2] - b;
		uint dist = dr * dr * 3 + dg * dg * 4 + db * db * 2;
		if (dist < bestDist) {
			bestDist = dist;
			best = i;
			if (!dist)
				break;
		}
	}
	return best;
}

VideoPlayer::VideoPlayer() : _decoder(nullptr), _state(kStopped), _hasFrame(false), _rgbTable(nullptr) {
	memset(_palette, 0, sizeof(_palette));
	memset(_videoPalette, 0, sizeof(_videoPalette));
	for (uint i = 0; i < 256; i++)
		_remap[i] = i;
}

VideoPlayer::~VideoPlayer() {
	close();
}

bool VideoPlayer::open(const Common::Path &path, const byte *palette) {
	close();
	memcpy(_palette, palette, sizeof(_palette));
	_decoder = new Video::AVIDecoder();
	_decoder->setSoundType(Audio::Mixer::kSFXSoundType);
	if (!_decoder->loadFile(path)) {
		warning("Can't open video %s", path.toString().c_str());
		delete _decoder;
		_decoder = nullptr;
		return false;
	}
	// Cinepak draws 8-bit through the Video for Windows dither, like the original's DrawDib
	_decoder->setDitheringPalette(_palette);
	_frame.create(_decoder->getWidth(), _decoder->getHeight(), Graphics::PixelFormat::createFormatCLUT8());
	_state = kStopped;
	decodeFrame();
	return true;
}

void VideoPlayer::close() {
	delete _decoder;
	_decoder = nullptr;
	_frame.free();
	_hasFrame = false;
	_state = kStopped;
	delete[] _rgbTable;
	_rgbTable = nullptr;
}

void VideoPlayer::play(int32 from, int32 to, bool frames) {
	if (!_decoder)
		return;
	if (_decoder->isPlaying())
		_decoder->stop();
	int count = _decoder->getFrameCount();
	if (frames)
		_decoder->seekToFrame(CLIP<int32>(from, 0, MAX(count - 1, 0)));
	else
		_decoder->seek(Audio::Timestamp(MAX<int32>(from, 0), 1000));
	if (to < 0)
		_decoder->setEndFrame(MAX(count - 1, 0));
	else if (frames)
		_decoder->setEndFrame(CLIP<int32>(to, 0, MAX(count - 1, 0)));
	else
		_decoder->setEndTime(Audio::Timestamp(to, 1000));
	_decoder->start();
	_state = kPlaying;
}

void VideoPlayer::stop() {
	if (_decoder && _decoder->isPlaying())
		_decoder->stop();
	_state = kStopped;
}

void VideoPlayer::pause() {
	if (_state != kPlaying)
		return;
	_decoder->pauseVideo(true);
	_state = kPaused;
}

void VideoPlayer::resume() {
	if (_state != kPaused)
		return;
	_decoder->pauseVideo(false);
	_state = kPlaying;
}

void VideoPlayer::pauseEngine(bool pause) {
	if (_decoder)
		_decoder->pauseVideo(pause);
}

void VideoPlayer::seek(int32 pos, bool frames) {
	if (!_decoder)
		return;
	if (frames)
		_decoder->seekToFrame(CLIP<int32>(pos, 0, MAX<int32>(_decoder->getFrameCount() - 1, 0)));
	else
		_decoder->seek(Audio::Timestamp(MAX<int32>(pos, 0), 1000));
	if (_state == kStopped)
		decodeFrame();
}

bool VideoPlayer::update() {
	if (!_decoder || _state != kPlaying)
		return false;
	while (_decoder->needsUpdate() && !_decoder->endOfVideo())
		decodeFrame();
	if (!_decoder->endOfVideo())
		return false;
	_decoder->stop();
	_state = kStopped;
	return true;
}

void VideoPlayer::decodeFrame() {
	const Graphics::Surface *s = _decoder->decodeNextFrame();
	if (!s)
		return;
	if (_decoder->hasDirtyPalette()) {
		memcpy(_videoPalette, _decoder->getPalette(), sizeof(_videoPalette));
		for (uint i = 0; i < 256; i++) {
			const byte *c = _videoPalette + i * 3;
			bool same = !memcmp(c, _palette + i * 3, 3);
			_remap[i] = same ? i : findNearest(_palette, c[0], c[1], c[2]);
		}
	}
	convertFrame(*s);
}

// The 8-bit AVIs use the layouts' palettes, so their colours map to themselves
void VideoPlayer::convertFrame(const Graphics::Surface &src) {
	static const int kBayer[4][4] = {
		{ 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 }
	};
	int w = MIN<int>(src.w, _frame.w), h = MIN<int>(src.h, _frame.h);
	if (src.format.bytesPerPixel == 1) {
		for (int y = 0; y < h; y++) {
			const byte *in = (const byte *)src.getBasePtr(0, y);
			byte *out = (byte *)_frame.getBasePtr(0, y);
			for (int x = 0; x < w; x++)
				out[x] = _remap[in[x]];
		}
	} else {
		if (!_rgbTable)
			buildColorTable();
		for (int y = 0; y < h; y++) {
			byte *out = (byte *)_frame.getBasePtr(0, y);
			for (int x = 0; x < w; x++) {
				uint32 color = src.getPixel(x, y);
				byte r, g, b;
				src.format.colorToRGB(color, r, g, b);
				int d = (kBayer[y & 3][x & 3] - 8) * 2;
				int rr = CLIP(r + d, 0, 255), gg = CLIP(g + d, 0, 255), bb = CLIP(b + d, 0, 255);
				out[x] = _rgbTable[(rr >> 3) << 10 | (gg >> 3) << 5 | (bb >> 3)];
			}
		}
	}
	_hasFrame = true;
}

void VideoPlayer::buildColorTable() {
	_rgbTable = new byte[32768];
	for (uint i = 0; i < 32768; i++)
		_rgbTable[i] = findNearest(_palette, (i >> 10) * 8 + 4, ((i >> 5) & 31) * 8 + 4, (i & 31) * 8 + 4);
}

} // End of namespace MSPAM
