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

#include "common/archive.h"
#include "common/debug.h"
#include "common/memstream.h"
#include "common/system.h"
#include "common/textconsole.h"
#include "graphics/surface.h"
#include "video/qt_decoder.h"

#include "bigtuna/bigtuna.h"
#include "bigtuna/bitmap.h"
#include "bigtuna/detection.h"
#include "bigtuna/imp.h"
#include "bigtuna/sound.h"
#include "bigtuna/stage.h"

namespace BigTuna {

ImpFile::~ImpFile() {
	for (auto &p : _pictures)
		delete p._value;
}

bool ImpFile::open(const Common::String &name) {
	_name = name;
	if (!_file.open(Common::Path(name + ".imp")))
		return false;
	if (_file.readUint16LE() != 0x0063 || _file.readUint16LE() != 0x7AB7) {
		warning("ImpFile: %s is not an IMP file", name.c_str());
		return false;
	}
	uint16 count = _file.readUint16LE();
	for (uint i = 0; i < count; i++) {
		byte buf[19];
		_file.read(buf, sizeof(buf));
		Common::String record((const char *)buf + 1, MIN<byte>(buf[0], 10));
		Entry e;
		e.size = READ_LE_UINT32(buf + 11);
		e.offset = READ_LE_UINT32(buf + 15);
		_records[record] = e;
	}
	debugC(1, kDebugLoading, "ImpFile %s: %d records", name.c_str(), count);
	return !_file.err();
}

bool ImpFile::read(const Common::String &record, Common::Array<byte> &data) {
	if (!_records.contains(record))
		return false;
	const Entry &e = _records[record];
	data.resize(e.size);
	_file.seek(e.offset);
	if (e.size)
		_file.read(&data[0], e.size);
	return !_file.err();
}

Bitmap *ImpFile::getPicture(const Common::String &name) {
	if (_pictures.contains(name))
		return _pictures[name];
	Bitmap *bitmap = nullptr;
	Common::Array<byte> data;
	if (read(name + ".P", data)) {
		Common::MemoryReadStream stream(&data[0], data.size());
		bitmap = new Bitmap();
		if (!bitmap->loadImp(stream)) {
			warning("ImpFile %s: bad picture %s", _name.c_str(), name.c_str());
			delete bitmap;
			bitmap = nullptr;
		}
	} else {
		warning("ImpFile %s: no picture %s", _name.c_str(), name.c_str());
	}
	_pictures[name] = bitmap;
	return bitmap;
}

Common::Array<Common::String> ImpFile::list() const {
	Common::Array<Common::String> names;
	for (auto &r : _records)
		names.push_back(r._key);
	return names;
}

ImpPlayer::ImpPlayer(BigTunaEngine *vm) : _vm(vm), _file(nullptr), _channels(0), _pos(0),
	_prevDuration(0), _clicks(true), _quit(false) {
}

ImpPlayer::~ImpPlayer() {
	delete _file;
}

const Common::String &ImpPlayer::getFilmName() const {
	static const Common::String none;
	return _file ? _file->getName() : none;
}

// FUN_1008_04bd / FUN_1008_030b
bool ImpPlayer::loadFilm(const Common::String &name) {
	ImpFile *file = new ImpFile();
	Common::Array<byte> data;
	if (!file->open(name) || !file->read(name + ".A", data) || data.size() < 19) {
		warning("gofile: Cannot find film %s", name.c_str());
		delete file;
		return false;
	}
	uint count = READ_LE_UINT16(&data[0]);
	if (data.size() < 19 + 19 * count) {
		warning("get: Invalid film format in %s", name.c_str());
		delete file;
		return false;
	}

	delete _file;
	_file = file;
	_channels = data[2];
	_records.resize(count);
	for (uint i = 0; i < count; i++) {
		const byte *p = &data[19 + 19 * i];
		ImpRecord &r = _records[i];
		r.type = p[0];
		r.channel = p[1];
		r.duration = p[2];
		r.transition = p[3];
		r.a = READ_LE_INT16(p + 4);
		r.b = READ_LE_INT16(p + 6);
		r.c = READ_LE_INT16(p + 8);
		r.name = Common::String((const char *)p + 11, MIN<byte>(p[10], 8));
	}
	_cells.clear();
	_cells.resize(_channels + 1);
	_buttons.clear();
	_buttons.resize(_channels + 1);
	_pos = 0;
	_prevDuration = 0;
	for (auto &s : _slots)
		s = SoundSlot();

	Common::Array<byte> pal;
	if (_file->read("macpal.C", pal) && pal.size() >= 769)
		_vm->setPaletteData(&pal[1]);
	debugC(1, kDebugLoading, "Loaded film %s: %d records, %d channels", name.c_str(), count, _channels);
	return true;
}

// FUN_1008_105e: at the end of the records the film starts over
void ImpPlayer::rewind() {
	for (auto &c : _cells)
		c = Cell();
	for (auto &b : _buttons)
		b = 0;
	_pos = 0;
}

Common::String ImpPlayer::variable(const Common::String &name) const {
	return _variables.contains(name) ? _variables[name] : Common::String();
}

// FUN_1008_1159
void ImpPlayer::apply(uint index, bool seeking) {
	const ImpRecord &r = _records[index];
	uint ch = r.channel;
	switch (r.type) {
	case kImpClear:
		if (ch < _cells.size())
			_cells[ch] = Cell();
		break;
	case kImpPicture:
		if (ch < _cells.size()) {
			Cell &c = _cells[ch];
			c.active = true;
			if (!c.bitmap || !c.picture.equalsIgnoreCase(r.name)) {
				c.picture = r.name;
				c.bitmap = _file->getPicture(r.name);
			}
			c.x = r.a;
			c.y = r.b;
			c.layer = r.c;
		}
		break;
	case kImpSound:
		if (!seeking && r.a >= 1 && r.a <= 2 && !r.name.empty()) {
			SoundSlot &s = _slots[r.a - 1];
			s.play = true;
			s.name = r.name;
			s.loop = r.b != 0;
		}
		break;
	case kImpVolume:
		if (!seeking && r.a >= 1 && r.a <= 2) {
			SoundSlot &s = _slots[r.a - 1];
			s.volume = true;
			s.level = CLIP<int>(r.b, 0, 255);
			s.fade = r.c;
		}
		break;
	case kImpButton:
		if (ch < _buttons.size())
			_buttons[ch] = r.name.empty() ? 0 : index + 1;
		break;
	case kImpClicks:
		_clicks = r.a != 0;
		updateCursor();
		break;
	case kImpGo:
		if (!seeking)
			_target = r.b ? variable(r.name) : r.name;
		break;
	case kImpMovie:
		if (!seeking)
			playMovie(r.name, r.a, r.b);
		break;
	case kImpSetVariable:
		if (index + 1 < _records.size() && _records[index + 1].type == kImpValue)
			_variables[r.name] = _records[index + 1].name;
		else
			_variables[r.name].clear();
		break;
	case kImpBox:
	case kImpText:
		warning("ImpPlayer: %s record %s not supported", r.type == kImpBox ? "box" : "text", r.name.c_str());
		break;
	default:
		break;
	}
}

void ImpPlayer::updateCursor() {
	_vm->setCursor(_clicks ? -1 : 200);
}

// Film y is measured up from the bottom of the stage to the bottom of the picture
int ImpPlayer::top(const Cell &c) const {
	return kStageH - c.y - c.bitmap->getSurface().h;
}

Common::Array<uint> ImpPlayer::drawOrder() const {
	Common::Array<uint> order;
	for (uint i = 1; i < _cells.size(); i++) {
		if (_cells[i].active && _cells[i].bitmap)
			order.push_back(i);
	}
	// Higher layer values are further back
	Common::sort(order.begin(), order.end(), [this](uint a, uint b) {
		if (_cells[a].layer != _cells[b].layer)
			return _cells[a].layer > _cells[b].layer;
		return a < b;
	});
	return order;
}

void ImpPlayer::draw(bool present) {
	Stage *stage = _vm->getStage();
	stage->clear(0);
	Common::Rect clip(kStageX, kStageY, kStageX + kStageW, kStageY + kStageH);
	for (uint ch : drawOrder()) {
		const Cell &c = _cells[ch];
		stage->drawBitmap(*c.bitmap, kStageX + c.x, kStageY + top(c), clip);
	}
	if (present)
		_vm->presentStage();
}

int ImpPlayer::channelAt(int x, int y) const {
	Common::Array<uint> order = drawOrder();
	for (int i = (int)order.size() - 1; i >= 0; i--) {
		const Cell &c = _cells[order[i]];
		int bx = x - kStageX - c.x, by = y - kStageY - top(c);
		const Graphics::Surface &s = c.bitmap->getSurface();
		if (bx < 0 || by < 0 || bx >= s.w || by >= s.h)
			continue;
		if (c.bitmap->getMask()[by * s.w + bx])
			return order[i];
	}
	return -1;
}

Common::Array<Common::Point> ImpPlayer::getButtonPoints() const {
	Common::Array<Common::Point> points;
	for (uint ch = 1; ch < _cells.size(); ch++) {
		const Cell &c = _cells[ch];
		if (!c.active || !c.bitmap || !_buttons[ch])
			continue;
		const Graphics::Surface &s = c.bitmap->getSurface();
		points.push_back(Common::Point(kStageX + c.x + s.w / 2, kStageY + top(c) + s.h / 2));
	}
	return points;
}

// FUN_1008_0a04: wait, taking clicks on buttons and the space bar
void ImpPlayer::waitFrame(uint32 duration) {
	uint32 start = g_system->getMillis();
	do {
		_vm->pumpEvents();
		if (_vm->shouldQuit())
			return;
		Common::Point p;
		if (_vm->takeClick(p) && _clicks) {
			int ch = channelAt(p.x, p.y);
			if (ch > 0 && _buttons[ch]) {
				const ImpRecord &b = _records[_buttons[ch] - 1];
				_target = b.a ? variable(b.name) : b.name;
				debugC(1, kDebugScript, "Button on channel %d -> %s", ch, _target.c_str());
				return;
			}
		}
		if (_vm->takeKey() == ' ') {
			_clicks = true;
			updateCursor();
			_target = "option1";
			return;
		}
		g_system->delayMillis(5);
	} while (!_vm->isFast() && g_system->getMillis() - start < duration);
}

void ImpPlayer::flushSounds() {
	for (int i = 0; i < 2; i++) {
		SoundSlot &s = _slots[i];
		if (s.play) {
			s.play = false;
			Common::Array<byte> data;
			if (_file->read(s.name + ".S", data) && data.size() >= 8 && !memcmp(&data[0], "snd8", 4))
				_vm->_sound->playRaw(i + 1, _file->getName() + "/" + s.name, &data[8], data.size() - 8, READ_LE_UINT16(&data[4]), s.loop);
			else
				warning("ImpPlayer: no sound %s", s.name.c_str());
		}
		if (s.volume) {
			s.volume = false;
			if (s.level)
				_vm->_sound->setVolume(i + 1, s.level, 0);
			else if (s.fade)
				_vm->_sound->setVolume(i + 1, 0, s.fade);
			else
				_vm->_sound->stop(i + 1);
		}
	}
}

// FUN_1008_05df: a label in this film, rebuilt from the last cell record per
// channel; otherwise the film of that name
bool ImpPlayer::goTo(const Common::String &target) {
	debugC(1, kDebugScript, "Go to %s", target.c_str());
	for (uint i = 0; i < _records.size(); i++) {
		if (_records[i].type != kImpLabel || !_records[i].name.equalsIgnoreCase(target))
			continue;
		uint end = i;
		while (end < _records.size() && !_records[end].duration)
			end++;
		if (end == _records.size())
			end--;
		rewind();
		for (uint j = 0; j <= end; j++) {
			byte t = _records[j].type;
			if (t == kImpClear || t == kImpPicture || t == kImpButton || t == kImpSetVariable)
				apply(j, true);
			else if (j >= i && (t == kImpSound || t == kImpVolume || t == kImpClicks))
				apply(j, false);
		}
		_pos = end + 1;
		endFrame(end);
		return true;
	}
	if (target.equalsIgnoreCase(getFilmName()) || loadFilm(target))
		return true;
	return false;
}

void ImpPlayer::endFrame(uint index) {
	draw();
	waitFrame(_prevDuration);
	flushSounds();
	_prevDuration = _records[index].duration * 1000 / 60;
}

void ImpPlayer::playMovie(const Common::String &name, int x, int y) {
	Video::QuickTimeDecoder video;
	if (!video.loadFile(Common::Path(name + ".mov"))) {
		warning("ImpPlayer: cannot open %s.mov", name.c_str());
		return;
	}
	video.setDitheringPalette(_vm->getPalette());
	video.start();
	draw();
	Stage *stage = _vm->getStage();
	while (!video.endOfVideo() && !_vm->shouldQuit()) {
		if (video.needsUpdate()) {
			const Graphics::Surface *frame = video.decodeNextFrame();
			if (frame && frame->format.bytesPerPixel == 1) {
				Common::Rect r(kStageX + x, kStageY + y, kStageX + x + frame->w, kStageY + y + frame->h);
				r.clip(Common::Rect(kStageWidth, kStageHeight));
				stage->getSurface().copyRectToSurface(*frame, r.left, r.top,
					Common::Rect(r.left - (kStageX + x), r.top - (kStageY + y), r.right - (kStageX + x), r.bottom - (kStageY + y)));
				_vm->presentStage();
			}
		}
		_vm->pumpEvents();
		Common::Point p;
		if (_vm->takeClick(p))
			break;
		g_system->delayMillis(5);
	}
}

// FUN_1008_0c24
void ImpPlayer::run(const Common::String &start) {
	if (!loadFilm(start))
		return;
	updateCursor();
	while (!_vm->shouldQuit() && !_quit) {
		if (_pos >= _records.size())
			rewind();
		uint index = _pos++;
		apply(index, false);
		if (!_records[index].duration)
			continue;
		endFrame(index);
		_vm->frameDone();
		if (!_target.empty()) {
			Common::String target = _target;
			_target.clear();
			if (target.equalsIgnoreCase("quit") && !_file->has("quit.A") && !ImpFile().open("quit")) {
				_vm->quitGame();
				break;
			}
			goTo(target);
		}
	}
}

void ImpPlayer::sweep() {
	Common::ArchiveMemberList files;
	SearchMan.listMatchingMembers(files, "*.imp");
	uint bad = 0;
	for (auto &f : files) {
		Common::String name = f->getFileName();
		name = name.substr(0, name.size() - 4);
		ImpFile file;
		if (!file.open(name)) {
			bad++;
			continue;
		}
		uint pictures = 0, failed = 0;
		for (auto &r : file.list()) {
			if (r.hasSuffixIgnoreCase(".P")) {
				pictures++;
				failed += !file.getPicture(r.substr(0, r.size() - 2));
			}
		}
		uint frames = 0;
		if (file.has(name + ".A") && loadFilm(name)) {
			for (uint i = 0; i < _records.size(); i++) {
				byte t = _records[i].type;
				if (t == kImpPicture || t == kImpClear || t == kImpButton || t == kImpClicks)
					apply(i, true);
				if (_records[i].duration) {
					frames++;
					draw(false);
				}
			}
		}
		debug("sweep %s: %d frames, %d pictures, %d failed", name.c_str(), frames, pictures, failed);
		bad += failed != 0;
		draw();
		_vm->pumpEvents();
	}
	debug("sweep done: %d files, %d with problems", files.size(), bad);
}

} // End of namespace BigTuna
