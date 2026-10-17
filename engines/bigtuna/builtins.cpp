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

#include "common/system.h"
#include "common/textconsole.h"

#include "bigtuna/bigtuna.h"
#include "bigtuna/movie.h"
#include "bigtuna/score.h"
#include "bigtuna/script.h"
#include "bigtuna/sound.h"

namespace BigTuna {

// Builtin ids shared by func (get), proc (call) and set (FUN_004099f0, FUN_0040a8e0)
enum Builtin {
	kBLocH = 0x00,
	kBLocV = 0x01,
	kBVideoPlaying = 0x02,
	kBVolume = 0x04,
	kBColorDepth = 0x05,
	kBCursor = 0x07,
	kBAlert = 0x09,
	kBPuppetSprite = 0x0A,
	kBSoundFadeIn = 0x0B,
	kBSoundFadeOut = 0x0C,
	kBSoundStop = 0x0D,
	kBAbs = 0x0E,
	kBDoubleClick = 0x0F,
	kBFrame = 0x10,
	kBLabel = 0x11,
	kBMouseDown = 0x12,
	kBMouseH = 0x13,
	kBMouseV = 0x14,
	kBParam = 0x15,
	kBParamCount = 0x16,
	kBSoundBusy = 0x17,
	kBCursorReset = 0x18,
	kBSoundLevel = 0x19,
	kBTimer = 0x1A,
	kBStartTimer = 0x1B,
	kBIgnoreInt = 0x1C,
	kBVisible = 0x1D,
	kBCastNum = 0x1E,
	kBUpdateStage = 0x1F,
	kBRandom = 0x20,
	kBMemberNum = 0x21,
	kBRollover = 0x22,
	kBMemberNumOf = 0x25,
	kBKey = 0x26,
	kBKeyCode = 0x27,
	kBKey2 = 0x28,
	kBCharToNum = 0x2D,
	kBPuppetSound = 0x30,
	kBString = 0x34,
	kBRollover2 = 0x35,
	kBClickOn = 0x36,
	kBGetAt = 0x37,
	kBAdd = 0x38,
	kBClearGlobals = 0x39,
	kBSetUnknown3A = 0x3A,
	kBCount = 0x3B,
	kBDeleteAt = 0x3C,
	kBSetAt = 0x3D,
	kBAddOrdered = 0x3E,
	kBGetPos = 0x3F,
	kBGetOne = 0x40,
	kBMax = 0x41,
	kBAddAt = 0x42,
	kBPoint = 0x43,
	kBLoc = 0x44,
	kBConstraint = 0x45,
	kBHeight = 0x46,
	kBRect = 0x47,
	kBWidth = 0x48,
	kBInside = 0x49,
	kBMember = 0x4A,
	kBPropListP = 0x4B,
	kBVoidP = 0x4C,
	kBGetPref = 0x4D,
	kBSetPref = 0x4E,
	kBValue = 0x4F,
	kBMin = 0x50,
	kBLeft = 0x51,
	kBRight = 0x52,
	kBTop = 0x53,
	kBBottom = 0x54
};

Sprite &Script::spriteArg(int32 channel) {
	return _vm->getPlayer()->getSprite(MAX<int32>(channel, 0));
}

// Fade arguments as FUN_004099f0 reads them: (channel, ticks) or just (ticks)
static void fadeArgs(int32 a, int32 b, int &channel, int &ticks) {
	ticks = b;
	channel = a;
	if (ticks == 0) {
		ticks = 90;
		if (channel > 2) {
			ticks = channel;
			channel = 1;
		}
	}
	channel = CLIP(channel, 1, 2);
}

Value Script::callBuiltin(uint32 id) {
	ScorePlayer *player = _vm->getPlayer();

	switch (id) {
	case kBLocH:
	case kBLocV:
	case kBLeft:
	case kBTop:
	case kBRight:
	case kBBottom: {
		static const char *const keys[] = { "loch", "locv", "left", "top", "right", "bottom" };
		int k = id <= kBLocV ? id : id - kBLeft + 2;
		if (topType() == kObjPropList)
			return popPropList().getProp(keys[k]);
		Sprite &s = spriteArg(popInt());
		if (k == 0)
			return (int32)s.x;
		if (k == 1)
			return (int32)s.y;
		Common::Rect r = player->getSpriteRect(s);
		return (int32)(k == 2 ? r.left : k == 3 ? r.top : k == 4 ? r.right : r.bottom);
	}
	case kBVideoPlaying:
		popInt();
		return (int32)0;
	case kBVolume:
		return (int32)_vm->_sound->getVolume(CLIP<int32>(popInt(), 1, 2));
	case kBColorDepth:
		return 8;
	case kBCursor: {
		Value v = pop();
		if (v.isList()) {
			uint count = v.obj->items.size();
			if (count == 1 || count == 2) {
				uint res = v.obj->items[0].toInt() + kMemberBase;
				if (count == 1 && _vm->getMovie()->getType(res) == kResEmpty)
					res++;
				_vm->setCursorMember(res + 1);
			} else {
				warning("Expected one or two images in cursor list");
			}
		} else if (v.isInt() || v.isVoid()) {
			_vm->setCursor(v.i);
		} else {
			warning("Cursor expected number or list");
		}
		return Value();
	}
	case kBAlert:
		popString();
		return Value();
	case kBPuppetSprite: {
		int32 flag = popInt();
		spriteArg(popInt()).puppet = flag != 0;
		return Value();
	}
	case kBSoundFadeIn:
	case kBSoundFadeOut: {
		int32 b = popInt();
		int32 a = popInt();
		int channel, ticks;
		fadeArgs(a, b, channel, ticks);
		if (id == kBSoundFadeIn) {
			int target = _vm->_sound->getVolume(channel);
			_vm->_sound->setVolume(channel, 0, 0);
			_vm->_sound->setVolume(channel, target, ticks);
		} else {
			_vm->_sound->setVolume(channel, 0, ticks);
		}
		return Value();
	}
	case kBSoundStop:
		_vm->_sound->stop(CLIP<int32>(popInt(), 1, 2));
		return Value();
	case kBAbs:
		return (int32)ABS(popInt());
	case kBDoubleClick:
		return (int32)_vm->_doubleClick;
	case kBFrame:
		return (int32)player->getFrame();
	case kBLabel: {
		Value v = pop();
		if (v.isString())
			return (int32)player->findLabelFrame(v.obj->str);
		return (int32)player->markerFrame(v.toInt());
	}
	case kBMouseDown:
		return (int32)_vm->_mouseDown;
	case kBMouseH:
		return (int32)_vm->_mousePos.x;
	case kBMouseV:
		return (int32)_vm->_mousePos.y;
	case kBParam: {
		int32 n = popInt();
		int32 argc = _base >= 4 ? _stack[_base - 4].toInt() : 0;
		int slot = (int)_base - argc - 4 + n;
		return (slot >= 0 && slot < (int)_stack.size()) ? _stack[slot] : Value();
	}
	case kBParamCount:
		return (int32)(_base >= 4 ? _stack[_base - 4].toInt() : 0);
	case kBSoundBusy:
		return (int32)_vm->_sound->isBusy(CLIP<int32>(popInt(), 1, 2));
	case kBCursorReset:
		_vm->setCursor(-1);
		return Value();
	case kBSoundLevel:
		return (int32)((_vm->_sound->getVolume(1) | 0x10) >> 4);
	case kBTimer:
		return (int32)((g_system->getMillis() - _vm->_timerStart) * 60 / 1000);
	case kBStartTimer:
		_vm->_timerStart = g_system->getMillis();
		return Value();
	case kBIgnoreInt:
		popInt();
		return Value();
	case kBVisible:
		return (int32)spriteArg(popInt()).visible;
	case kBCastNum:
	case kBMemberNum: {
		uint member = spriteArg(popInt()).member;
		return (int32)(member ? member - kMemberBase : 0);
	}
	case kBUpdateStage:
		_vm->updateStage();
		return Value();
	case kBRandom: {
		int32 n = popInt();
		return (int32)(n > 0 ? _vm->_rnd.getRandomNumber(n - 1) + 1 : 1);
	}
	case kBRollover:
	case kBRollover2: {
		const Sprite *s = _vm->spriteAt(_vm->_mousePos.x, _vm->_mousePos.y, false);
		if (!s || !s->member)
			return -1;
		return (int32)(s->member - kMemberBase);
	}
	case kBMemberNumOf:
	case kBMember: {
		uint res = popMember();
		return (int32)(res ? res - kMemberBase : 0);
	}
	case kBKey:
	case kBKey2:
		if (!_vm->_keyChar)
			return (int32)0;
		return Value::string(Common::String((char)_vm->_keyChar));
	case kBKeyCode:
		return (int32)_vm->_keyChar;
	case kBCharToNum: {
		Common::String s = popString();
		return (int32)(s.empty() ? 0 : (int8)s[0]);
	}
	case kBPuppetSound: {
		Value v = pop();
		int channel;
		uint res;
		if (v.isVoid()) {
			channel = 1;
			res = popMember();
		} else {
			push(v);
			res = popMember();
			channel = CLIP<int32>(popInt(), 0, 2);
		}
		if (channel) {
			if (res)
				_vm->_sound->play(channel, _vm->getMovie(), res);
			else
				_vm->_sound->stop(channel);
		}
		return Value();
	}
	case kBString:
		if (topType() == kObjList)
			return Value::string(popList().toString());
		return Value::string(popString());
	case kBClickOn:
		return (int32)_vm->_clickOn;
	case kBGetAt: {
		int32 i = popInt();
		if (topType() != kObjList) {
			pop();
			warning("getAt not an array");
			return (int32)0;
		}
		Value list = popList();
		if (i < 1 || i > (int32)list.obj->items.size()) {
			warning("getAt bad index");
			return (int32)0;
		}
		return list.obj->items[i - 1];
	}
	case kBAdd:
	case kBAddOrdered: {
		Value v = pop();
		Value list = popList();
		Common::Array<Value> &items = list.obj->items;
		if (id == kBAddOrdered) {
			// FUN_0040af50 inserts before the first plain value it compares just above
			for (uint i = 0; i < items.size(); i++) {
				if (!v.isObject() && !items[i].isObject()) {
					int32 rawA = v.isInt() ? v.i * 2 + 1 : 0;
					int32 rawB = items[i].isInt() ? items[i].i * 2 + 1 : 0;
					if ((rawA - rawB) / 20 != -1)
						continue;
				}
				items.insert_at(i, v);
				return Value();
			}
		}
		items.push_back(v);
		return Value();
	}
	case kBClearGlobals:
		clearGlobals();
		return Value();
	case kBSetUnknown3A:
		popInt();
		return Value();
	case kBCount:
		return (int32)popList().obj->items.size();
	case kBDeleteAt: {
		int32 i = popInt();
		Value list = popList();
		if (i < 1 || i > (int32)list.obj->items.size())
			warning("deleteAt bad index");
		else
			list.obj->items.remove_at(i - 1);
		return Value();
	}
	case kBSetAt: {
		Value v = pop();
		int32 i = popInt();
		Value list = popList();
		if (i < 1) {
			warning("setAt bad index");
			return Value();
		}
		while ((int32)list.obj->items.size() < i)
			list.obj->items.push_back(Value());
		list.obj->items[i - 1] = v;
		return Value();
	}
	case kBGetPos: {
		Value v = pop();
		Value list = popList();
		Common::Array<Value> &items = list.obj->items;
		if (!items.empty() && !(items.size() & 1) && items[0].isObject()) {
			for (uint i = 1; i < items.size(); i += 2) {
				if (items[i].identical(v))
					return items[i - 1];
			}
		}
		for (uint i = 0; i < items.size(); i++) {
			if (items[i].identical(v))
				return (int32)(i + 1);
		}
		return (int32)0;
	}
	case kBGetOne: {
		Common::String key = popString();
		Value list = popList();
		Common::Array<Value> &items = list.obj->items;
		for (uint i = 0; i + 1 < items.size(); i += 2) {
			if (items[i].isString() && items[i].obj->str.equalsIgnoreCase(key))
				return items[i + 1];
		}
		return Value();
	}
	case kBMax:
	case kBMin: {
		bool isMax = id == kBMax;
		if (topType() == kObjList) {
			Value list = popList();
			int32 best = isMax ? -0x3FFFFFFF : 0x3FFFFFFF;
			for (auto &v : list.obj->items) {
				if (v.isInt())
					best = isMax ? MAX(best, v.i) : MIN(best, v.i);
			}
			return best;
		}
		int32 b = popInt();
		int32 a = popInt();
		return isMax ? MAX(a, b) : MIN(a, b);
	}
	case kBAddAt: {
		Value v = pop();
		int32 i = popInt();
		Value list = popList();
		i = CLIP<int32>(i - 1, 0, list.obj->items.size());
		list.obj->items.insert_at(i, v);
		return Value();
	}
	case kBPoint: {
		int32 v = popInt();
		int32 h = popInt();
		return Value::point(h, v);
	}
	case kBLoc: {
		Sprite &s = spriteArg(popInt());
		return Value::point(s.x, s.y);
	}
	case kBRect:
	case kBWidth:
	case kBHeight: {
		Sprite &s = spriteArg(popInt());
		Common::Rect r = player->getSpriteRect(s);
		if (id == kBWidth)
			return (int32)r.width();
		if (id == kBHeight)
			return (int32)r.height();
		return Value::rect(r.left, r.top, r.right, r.bottom);
	}
	case kBInside: {
		Value rect = popPropList();
		Value pt = popPropList();
		int32 h = pt.prop("loch"), v = pt.prop("locv");
		return (int32)(h >= rect.prop("left") && h <= rect.prop("right") &&
			v >= rect.prop("top") && v <= rect.prop("bottom"));
	}
	case kBPropListP: {
		bool r = topType() == kObjPropList;
		pop();
		return (int32)r;
	}
	case kBVoidP:
		return (int32)pop().isVoid();
	case kBGetPref:
		return _vm->getPref(popString());
	case kBSetPref: {
		Common::String text = popString();
		Common::String name = popString();
		_vm->setPref(name, text);
		return Value();
	}
	case kBValue: {
		Value v = pop();
		if (!v.isString())
			return v;
		const char *p = v.obj->str.c_str();
		if (*p >= '0' && *p <= '9')
			return (int32)atoi(p);
		if (*p == '[') {
			p++;
			return parseList(p);
		}
		return v;
	}
	default:
		warning("Unknown function %02x", id);
		return (int32)0;
	}
}

void Script::setProperty(uint32 id) {
	ScorePlayer *player = _vm->getPlayer();

	switch (id) {
	case kBLocH: {
		int32 v = popInt();
		Sprite &s = spriteArg(popInt());
		player->setLoc(s, v, s.y, true);
		break;
	}
	case kBLocV: {
		int32 v = popInt();
		Sprite &s = spriteArg(popInt());
		player->setLoc(s, s.x, v, true);
		break;
	}
	case kBVolume: {
		int32 v = CLIP<int32>(popInt(), 0, 255);
		int32 channel = CLIP<int32>(popInt(), 1, 2);
		_vm->_sound->setVolume(channel, v, 0);
		break;
	}
	case kBColorDepth:
		popInt();
		break;
	case kBSoundLevel: {
		int32 level = MIN<int32>(popInt(), 7);
		_vm->_sound->setVolume(1, level << 5, 0);
		_vm->_sound->setVolume(2, level << 5, 0);
		break;
	}
	case kBTimer:
		_vm->_timerStart = g_system->getMillis() - popInt() * 1000 / 60;
		break;
	case kBVisible: {
		int32 v = popInt();
		Sprite &s = spriteArg(popInt());
		player->setVisible(s, v != 0);
		break;
	}
	case kBCastNum:
	case kBMemberNum:
	case kBMember: {
		int32 n = popInt();
		Sprite &s = spriteArg(popInt());
		uint res = n + kMemberBase;
		if (_vm->getMovie()->getType(res) != kResEmpty) {
			player->setMember(s, res);
		} else {
			player->setMember(s, 0);
			warning("Missing cast %d", n);
		}
		break;
	}
	case kBLoc: {
		if (topType() != kObjPropList) {
			warning("set loc expected point");
			pop();
			pop();
			break;
		}
		Value pt = popPropList();
		Sprite &s = spriteArg(popInt());
		player->setLoc(s, pt.prop("loch"), pt.prop("locv"), true);
		break;
	}
	case kBConstraint: {
		int32 other = popInt();
		int32 channel = popInt();
		Common::Rect bounds(-32767, -32767, 32767, 32767);
		if (other != 0)
			bounds = player->getSpriteRect(spriteArg(other));
		spriteArg(channel).constraint = bounds;
		break;
	}
	default:
		warning("Unknown property %02x", id);
		break;
	}
}

} // End of namespace BigTuna
