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
	Frame frame;
	frame.offset = 0;

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
		case kFilmNext: {
			uint32 count = cmd.op == kFilmWait ? readVarint(stream) : 1;
			_frames.push_back(frame);
			frame.commands.clear();
			frame.offset = stream.pos();
			for (uint32 i = 1; i < count; i++)
				_frames.push_back(frame);
			continue;
		}
		default:
			warning("Score::load(): unknown film op %02x at %d", cmd.op, (int)stream.pos() - 1);
			return false;
		}
		if (stream.eos() || stream.err())
			return false;
		frame.commands.push_back(cmd);
	}
	if (!frame.commands.empty())
		_frames.push_back(frame);
	return true;
}

uint Score::findFrameByOffset(uint32 offset) const {
	for (uint i = 0; i < _frames.size(); i++) {
		if (_frames[i].offset >= offset)
			return i + 1;
	}
	return 0;
}

ScorePlayer::ScorePlayer(BigTunaEngine *vm, Movie *movie, Score *score, bool isLoop) :
	_vm(vm), _movie(movie), _score(score), _isLoop(isLoop) {
	reset();
}

ScorePlayer::~ScorePlayer() {
	for (auto &s : _sprites)
		delete s.loop;
}

void ScorePlayer::reset() {
	for (auto &s : _sprites)
		delete s.loop;
	_sprites.clear();
	_frame = 0;
	_tempo = 15;
	_frameScript = 0;
	_originX = _originY = 0;
	_waitSoundChannel = 0;
}

bool ScorePlayer::advance() {
	if (_waitSoundChannel) {
		if (_vm->_sound->isBusy(_waitSoundChannel))
			return true;
		_waitSoundChannel = 0;
	}

	for (auto &s : _sprites) {
		if (s.loop)
			s.loop->advance();
	}

	if (_frame >= _score->getFrameCount()) {
		if (!_isLoop || _score->getFrameCount() == 0)
			return false;
		reset();
	}
	applyFrame(_frame + 1, false);
	return true;
}

void ScorePlayer::gotoFrame(uint frame) {
	if (frame == 0 || frame > _score->getFrameCount())
		return;
	if (frame <= _frame)
		reset();
	while (_frame + 1 < frame)
		applyFrame(_frame + 1, true);
	applyFrame(frame, false);
}

bool ScorePlayer::gotoLabel(const Common::String &label) {
	for (auto &l : _movie->getLabels()) {
		if (!l.name.equalsIgnoreCase(label))
			continue;
		uint frame = _movie->isV1() ? _score->findFrameByOffset(l.value) : l.value;
		gotoFrame(frame);
		return true;
	}
	warning("ScorePlayer::gotoLabel(): no label '%s' in %s", label.c_str(), _movie->getName().c_str());
	return false;
}

void ScorePlayer::applyFrame(uint frame, bool seeking) {
	_frame = frame;
	for (auto &cmd : _score->getFrame(frame).commands)
		apply(cmd, seeking);
}

Sprite &ScorePlayer::getSprite(uint channel) {
	if (channel >= _sprites.size())
		_sprites.resize(channel + 1);
	return _sprites[channel];
}

void ScorePlayer::setMember(Sprite &sprite, uint member) {
	if (sprite.member == member)
		return;
	delete sprite.loop;
	sprite.loop = nullptr;
	sprite.member = member;
	if (_movie->getType(member) == kResScore) {
		Score *score = _movie->getScore(member);
		if (score)
			sprite.loop = new ScorePlayer(_vm, _movie, score, true);
	}
}

void ScorePlayer::clearSprite(Sprite &sprite) {
	delete sprite.loop;
	sprite = Sprite();
}

void ScorePlayer::apply(const FilmCommand &cmd, bool seeking) {
	switch (cmd.op) {
	case kFilmClear:
		if ((uint)cmd.a < _sprites.size())
			clearSprite(_sprites[cmd.a]);
		break;
	case kFilmSprite: {
		Sprite &sprite = getSprite(cmd.a);
		setMember(sprite, cmd.b);
		sprite.x = cmd.c;
		sprite.y = cmd.d;
		break;
	}
	case kFilmScript:
		if (cmd.a == 0)
			_frameScript = cmd.b;
		else if (cmd.b != 0 || (uint)cmd.a < _sprites.size())
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
			_waitSoundChannel = cmd.a;
		break;
	case kFilmGoto:
		if (!seeking && !_isLoop)
			_vm->requestJump("", cmd.s1);
		break;
	case kFilmGoMovie:
		if (!seeking && !_isLoop)
			_vm->requestJump(cmd.s1, cmd.s2);
		break;
	case kFilmCursor:
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
