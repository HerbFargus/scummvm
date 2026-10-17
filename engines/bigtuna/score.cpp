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

#include "common/debug.h"
#include "common/textconsole.h"

#include "bigtuna/bigtuna.h"
#include "bigtuna/bitmap.h"
#include "bigtuna/detection.h"
#include "bigtuna/movie.h"
#include "bigtuna/score.h"
#include "bigtuna/sound.h"

namespace BigTuna {

uint32 Score::readVarint(Common::SeekableReadStream &stream) {
	uint32 value = 0;
	byte b;
	do {
		b = stream.readByte();
		value = (value << 7) | (b & 0x7F);
	} while ((b & 0x80) && !stream.eos());
	return value;
}

int32 Score::readSignedVarint(Common::SeekableReadStream &stream) {
	uint32 value = readVarint(stream);
	return (value & 1) ? -(int32)(value >> 1) : (int32)(value >> 1);
}

Common::String Score::readPString(Common::SeekableReadStream &stream) {
	byte len = stream.readByte();
	return stream.readString(0, len);
}

bool Score::load(Common::SeekableReadStream &stream) {
	Step step;
	step.offset = 0;
	step.frame = 1;
	step.length = 1;

	while (stream.pos() < stream.size()) {
		FilmCommand cmd;
		cmd.op = stream.readByte();
		cmd.a = cmd.b = cmd.c = cmd.d = 0;

		switch (cmd.op) {
		case kFilmSpriteName:
			cmd.a = readVarint(stream);
			cmd.s1 = readPString(stream);
			break;
		case kFilmCursor:
		case kFilmTempo:
		case kFilmWaitSound:
			cmd.a = stream.readByte();
			break;
		case kFilmClear:
		case kFilmPalette:
			cmd.a = readVarint(stream);
			break;
		case kFilmGoto:
		case kFilmComment:
			cmd.s1 = readPString(stream);
			break;
		case kFilmGoMovie:
			cmd.s1 = readPString(stream);
			cmd.s2 = readPString(stream);
			break;
		case kFilmSprite:
			cmd.a = readVarint(stream);
			cmd.b = readVarint(stream);
			cmd.c = readSignedVarint(stream);
			cmd.d = readSignedVarint(stream);
			break;
		case kFilmScript:
		case kFilmStage:
		case kFilmOrigin:
			cmd.a = readVarint(stream);
			cmd.b = readVarint(stream);
			break;
		case kFilmSound1:
		case kFilmSound2:
		case kFilmChannelProp:
			cmd.a = stream.readByte();
			cmd.b = readVarint(stream);
			break;
		case kFilmVolume:
			cmd.a = stream.readByte();
			cmd.b = stream.readByte();
			cmd.c = readVarint(stream);
			break;
		case kFilmVideo:
			cmd.a = readSignedVarint(stream);
			cmd.b = readSignedVarint(stream);
			cmd.c = readVarint(stream);
			cmd.d = readVarint(stream);
			cmd.s1 = readPString(stream);
			break;
		case kFilmWait:
		case kFilmNext:
			step.length = cmd.op == kFilmWait ? MAX<uint32>(readVarint(stream), 1) : 1;
			_steps.push_back(step);
			step.commands.clear();
			step.offset = stream.pos();
			step.frame += step.length;
			step.length = 1;
			continue;
		default:
			warning("Score::load(): unknown film op %02x at %d", cmd.op, (int)stream.pos() - 1);
			return false;
		}
		if (stream.eos() || stream.err())
			return false;
		step.commands.push_back(cmd);
	}
	if (!step.commands.empty())
		_steps.push_back(step);
	return true;
}

uint Score::getFrameCount() const {
	if (_steps.empty())
		return 0;
	return _steps.back().frame + _steps.back().length - 1;
}

int Score::findStep(uint frame) const {
	for (uint i = 0; i < _steps.size(); i++) {
		if (frame < _steps[i].frame + _steps[i].length)
			return i;
	}
	return -1;
}

uint Score::findFrameByOffset(uint32 offset) const {
	for (auto &s : _steps) {
		if (s.offset >= offset)
			return s.frame;
	}
	return 0;
}

ScorePlayer::ScorePlayer(BigTunaEngine *vm, Movie *movie, Score *score, bool isLoop) :
	_vm(vm), _movie(movie), _score(score), _isLoop(isLoop), _current(-1), _next(0),
	_tempo(15), _frameScript(0), _originX(0), _originY(0), _seekCount(0) {
}

ScorePlayer::~ScorePlayer() {
	for (auto &s : _sprites)
		delete s.loop;
}

uint ScorePlayer::getFrame() const {
	if (_current < 0)
		return 0;
	const Step &s = _score->getStep(_current);
	return s.frame + s.length - 1;
}

bool ScorePlayer::hasLoops() const {
	for (auto &s : _sprites) {
		if (s.visible && s.loop)
			return true;
	}
	return false;
}

uint ScorePlayer::step() {
	if (_next >= _score->getStepCount())
		return 0;
	_current = _next++;
	const Step &s = _score->getStep(_current);
	for (auto &cmd : s.commands)
		apply(cmd, false);
	return s.length;
}

// FUN_004033a0 / FUN_00403a00: shown sprites go, hidden ones keep their state minus the member
void ScorePlayer::rewind() {
	for (auto &s : _sprites) {
		if (!s.exists || s.puppet)
			continue;
		if (s.visible)
			destroySprite(s);
		else
			setMember(s, 0);
	}
	_current = -1;
	_next = 0;
	_frameScript = 0;
}

// FUN_00402640: rebuild the sprites from the start of the score, then let the
// next step be the target. Puppet sprites keep their state.
void ScorePlayer::seek(uint frame) {
	int target = _score->findStep(frame);
	if (target < 0) {
		warning("ScorePlayer::seek(): no frame %d in %s", frame, _movie->getName().c_str());
		return;
	}
	_seekCount++;
	rewind();
	for (int i = 0; i < target; i++) {
		_current = i;
		for (auto &cmd : _score->getStep(i).commands)
			apply(cmd, true);
	}
	_next = target;
}

bool ScorePlayer::seekLabel(const Common::String &label) {
	int frame = findLabelFrame(label);
	if (frame <= 0) {
		warning("Cannot find frame %s", label.c_str());
		return false;
	}
	seek(frame);
	return true;
}

static bool labelMatches(const Common::String &name, const Common::String &label) {
	Common::String n = name;
	while (n.hasSuffix("\r"))
		n.deleteLastChar();
	return n.equalsIgnoreCase(label);
}

int ScorePlayer::findLabelFrame(const Common::String &label) const {
	for (auto &l : _movie->getLabels()) {
		if (labelMatches(l.name, label))
			return _movie->isV1() ? _score->findFrameByOffset(l.value) : l.value;
	}
	return 0;
}

// FUN_00402e30: marker(0) is the label at or before the current frame
int ScorePlayer::markerFrame(int offset) const {
	const Common::Array<Label> &labels = _movie->getLabels();
	if (labels.empty())
		return 0;
	Common::Array<uint> frames;
	for (auto &l : labels)
		frames.push_back(_movie->isV1() ? _score->findFrameByOffset(l.value) : l.value);
	uint current = getFrame();
	int i = 0;
	while (i < (int)frames.size() && frames[i] < current)
		i++;
	if (i == (int)frames.size() || frames[i] != current)
		i--;
	i = CLIP<int>(i + offset, 0, frames.size() - 1);
	return frames[i];
}

Sprite *ScorePlayer::findSprite(uint channel) {
	if (channel >= _sprites.size() || !_sprites[channel].exists)
		return nullptr;
	return &_sprites[channel];
}

// FUN_00403a70: referring to a channel creates its sprite, shown
Sprite &ScorePlayer::getSprite(uint channel) {
	if (channel >= _sprites.size())
		_sprites.resize(channel + 1);
	Sprite &s = _sprites[channel];
	if (!s.exists) {
		s = Sprite();
		s.exists = true;
		s.visible = true;
	}
	return s;
}

void ScorePlayer::destroySprite(Sprite &sprite) {
	delete sprite.loop;
	sprite = Sprite();
}

void ScorePlayer::setMember(Sprite &sprite, uint member) {
	if (sprite.member == member)
		return;
	delete sprite.loop;
	sprite.loop = nullptr;
	sprite.member = member;
	if (_movie->getType(member) == kResScore && sprite.visible) {
		Score *score = _movie->getScore(member);
		if (score)
			sprite.loop = new ScorePlayer(_vm, _movie, score, true);
	}
}

void ScorePlayer::setVisible(Sprite &sprite, bool visible) {
	if (sprite.visible == visible)
		return;
	sprite.visible = visible;
	if (!visible) {
		delete sprite.loop;
		sprite.loop = nullptr;
	} else if (_movie->getType(sprite.member) == kResScore) {
		Score *score = _movie->getScore(sprite.member);
		if (score)
			sprite.loop = new ScorePlayer(_vm, _movie, score, true);
	}
}

void ScorePlayer::setLoc(Sprite &sprite, int x, int y, bool constrain) {
	if (constrain) {
		x = CLIP<int>(x, sprite.constraint.left, sprite.constraint.right);
		y = CLIP<int>(y, sprite.constraint.top, sprite.constraint.bottom);
	}
	sprite.x = x;
	sprite.y = y;
}

Common::Rect ScorePlayer::getSpriteRect(const Sprite &sprite) const {
	if (sprite.loop) {
		Common::Rect r;
		bool first = true;
		for (auto &s : sprite.loop->_sprites) {
			if (!s.visible || !s.member)
				continue;
			Common::Rect c = sprite.loop->getSpriteRect(s);
			c.translate(sprite.x - sprite.loop->_originX, sprite.y - sprite.loop->_originY);
			if (first)
				r = c;
			else
				r.extend(c);
			first = false;
		}
		return r;
	}
	Bitmap *bitmap = _movie->getBitmap(sprite.member);
	if (!bitmap)
		return Common::Rect(sprite.x, sprite.y, sprite.x, sprite.y);
	int left = sprite.x - bitmap->getRegX();
	int top = sprite.y - bitmap->getRegY();
	return Common::Rect(left, top, left + bitmap->getSurface().w, top + bitmap->getSurface().h);
}

bool ScorePlayer::hitTest(const Sprite &sprite, int x, int y) const {
	if (!sprite.visible || !sprite.member)
		return false;
	if (sprite.loop) {
		int lx = x - sprite.x + sprite.loop->_originX;
		int ly = y - sprite.y + sprite.loop->_originY;
		for (auto &s : sprite.loop->_sprites) {
			if (sprite.loop->hitTest(s, lx, ly))
				return true;
		}
		return false;
	}
	Common::Rect r = getSpriteRect(sprite);
	if (!r.contains(x, y))
		return false;
	Bitmap *bitmap = _movie->getBitmap(sprite.member);
	const byte *mask = bitmap->getMask();
	return !mask || mask[(y - r.top) * r.width() + (x - r.left)];
}

void ScorePlayer::stepLoops() {
	for (auto &s : _sprites) {
		if (!s.visible || !s.loop)
			continue;
		if (!s.loop->step()) {
			s.loop->rewind();
			s.loop->step();
		}
	}
}

void ScorePlayer::apply(const FilmCommand &cmd, bool seeking) {
	switch (cmd.op) {
	case kFilmClear:
		if ((uint)cmd.a < _sprites.size() && _sprites[cmd.a].exists && !_sprites[cmd.a].puppet) {
			Sprite &s = _sprites[cmd.a];
			if (s.visible)
				destroySprite(s);
			else
				setMember(s, 0);
		}
		break;
	case kFilmSprite: {
		Sprite &sprite = getSprite(cmd.a);
		if (sprite.puppet)
			break;
		setMember(sprite, cmd.b);
		sprite.x = cmd.c;
		sprite.y = cmd.d;
		break;
	}
	case kFilmScript:
		if (cmd.a == 0)
			_frameScript = cmd.b;
		else if (cmd.b != 0 || findSprite(cmd.a))
			getSprite(cmd.a).script = cmd.b;
		break;
	case kFilmPalette:
		_vm->setPalette(_movie, cmd.a);
		break;
	case kFilmTempo:
		if (cmd.a > 0)
			_tempo = cmd.a;
		break;
	case kFilmOrigin:
		_originX = cmd.a;
		_originY = cmd.b;
		break;
	case kFilmSound1:
	case kFilmSound2:
		if (!seeking)
			_vm->_sound->play(cmd.a, _movie, cmd.b);
		break;
	case kFilmVolume:
		_vm->_sound->setVolume(cmd.a, cmd.b, seeking ? 0 : cmd.c);
		break;
	case kFilmWaitSound:
		if (!seeking)
			_vm->waitForSound(cmd.a);
		break;
	case kFilmCursor:
		if (!seeking && !_isLoop)
			_vm->setCursor(cmd.a);
		break;
	case kFilmGoto:
		if (!seeking && !_isLoop)
			seekLabel(cmd.s1);
		break;
	case kFilmGoMovie:
		if (!seeking && !_isLoop)
			_vm->requestMovie(cmd.s1, Value::string(cmd.s2));
		break;
	case kFilmComment:
	case kFilmSpriteName:
	case kFilmChannelProp:
	case kFilmStage:
		debugC(2, kDebugScore, "Film op %02x ignored", cmd.op);
		break;
	case kFilmVideo:
		warning("ScorePlayer: video '%s' not supported", cmd.s1.c_str());
		break;
	default:
		break;
	}
}

} // End of namespace BigTuna
