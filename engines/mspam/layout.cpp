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
#include "common/memstream.h"
#include "common/textconsole.h"

#include "mspam/compound.h"
#include "mspam/detection.h"
#include "mspam/layout.h"

namespace MSPAM {

static const byte kClassIdTail[15] = {
	0x71, 0xE5, 0xCB, 0x1F, 0x03, 0x69, 0x10, 0x96, 0x03, 0x08, 0x00, 0x2B, 0x36, 0xA4, 0x46
};

enum {
	kPersistVersion = 0x10000,
	kSegmentSize = 0x38,
	kMaxCount = 10000
};

/** Reads a CLSID; returns its last byte, 0 for a null CLSID, -1 for a foreign one */
static int readClassId(Common::SeekableReadStream &s) {
	byte id[16];
	if (s.read(id, 16) != 16)
		return -1;
	if (!memcmp(id + 1, kClassIdTail, 15))
		return id[0];
	for (int i = 0; i < 16; i++) {
		if (id[i])
			return -1;
	}
	return kClassNone;
}

static Common::String readCString(Common::SeekableReadStream &s) {
	Common::String str;
	byte c;
	while (!s.eos() && (c = s.readByte()) != 0)
		str += (char)c;
	return str;
}

static void alignEven(Common::SeekableReadStream &s) {
	if (s.pos() & 1)
		s.skip(1);
}

Common::String ActionParam::toString() const {
	if (type == kTypeString)
		return Common::String::format("\"%s\"", str.c_str());
	if (type >= kTypeBlob) {
		if (blob.size() >= 16 && !memcmp(&blob[1], kClassIdTail, 15))
			return Common::String::format("{class %02X, %d bytes}", blob[0], blob.size() - 16);
		return Common::String::format("{%d bytes}", blob.size());
	}
	if (type == 5)
		return Common::String::format("media %x", value);
	return Common::String::format("%d", (int32)value);
}

bool ActionList::load(Common::SeekableReadStream &s) {
	actions.clear();
	uint32 version = s.readUint32LE();
	uint32 size = s.readUint32LE();
	if (version != kPersistVersion || s.pos() + size > s.size())
		return false;
	int64 end = s.pos() + size;
	flags = s.readUint16LE();
	flags2 = s.readUint16LE();
	uint16 count = s.readUint16LE();
	for (uint i = 0; i < count && s.pos() < end; i++) {
		Action a;
		a.id = s.readUint16LE();
		a.targetKind = s.readUint16LE();
		a.target = readCString(s);
		alignEven(s);
		uint16 nparams = s.readUint16LE();
		for (uint k = 0; k < nparams; k++) {
			ActionParam p;
			p.type = s.readUint16LE();
			p.value = 0;
			if (p.type == 0 || p.type == 2) {
				p.value = s.readUint16LE();
			} else if (p.type == 1 || p.type == 3 || (p.type >= 5 && p.type <= 7)) {
				p.value = s.readUint32LE();
			} else if (p.type == ActionParam::kTypeString) {
				// A stale far pointer from the authoring tool, then the string if it was set
				p.value = s.readUint32LE();
				if (p.value)
					p.str = readCString(s);
			} else if (p.type >= ActionParam::kTypeBlob) {
				uint32 n = s.readUint32LE();
				if (n > size)
					return false;
				p.blob.resize(n);
				s.read(p.blob.data(), n);
			} else {
				warning("Unknown action parameter type %x", p.type);
				return false;
			}
			alignEven(s);
			a.params.push_back(p);
		}
		actions.push_back(a);
	}
	if (s.pos() != end) {
		warning("Action list ends at %d, expected %d", (int)s.pos(), (int)end);
		s.seek(end);
		return false;
	}
	return !s.err();
}

bool Hotspot::contains(int px, int py) const {
	if (points.size() < 3 || px < bounds.left || px > bounds.right || py < bounds.top || py > bounds.bottom)
		return false;
	// Even-odd rule
	bool inside = false;
	for (uint i = 0, j = points.size() - 1; i < points.size(); j = i++) {
		const Common::Point &a = points[i], &b = points[j];
		if ((a.y > py) != (b.y > py) && px < (b.x - a.x) * (py - a.y) / (double)(b.y - a.y) + a.x)
			inside = !inside;
	}
	return inside;
}

Element::Element() : classId(0), x(0), y(0), z(0), visible(false), siteFlags(0), speed(0), iterations(0),
	initialSegment(0), spriteFlags(0), nextId(0), width(0), height(0), videoId(0), videoParam(0),
	videoParam2(0), videoFlags(0) {
	spriteWords[0] = spriteWords[1] = spriteWords[2] = 0;
	spriteLongs[0] = spriteLongs[1] = 0;
}

const PathSegment *Element::findSegment(uint32 id) const {
	for (const PathSegment &seg : segments) {
		if (seg.id == id)
			return &seg;
	}
	return nullptr;
}

const PathSegment *Element::getCurrentSegment() const {
	const PathSegment *seg = findSegment(initialSegment);
	return seg ? seg : (segments.empty() ? nullptr : &segments[0]);
}

bool readNameBlock(Common::SeekableReadStream &s, Common::Array<Common::String> &names) {
	int64 start = s.pos();
	s.readUint16LE(); // allocated size
	uint16 size = s.readUint16LE();
	uint16 count = s.readUint16LE();
	Common::Array<uint16> offsets;
	for (uint i = 0; i < count; i++)
		offsets.push_back(s.readUint16LE());
	names.clear();
	for (uint16 o : offsets) {
		s.seek(start + o);
		names.push_back(readCString(s));
	}
	s.seek(start + size);
	return !s.err() && count < kMaxCount;
}

Layout::Layout() : _flags(0), _priority(0), _paletteId(0), _transClass(0) {
	_transParams[0] = _transParams[1] = _transParams[2] = 0;
	_listHeader[0] = _listHeader[1] = 0;
}

// LyoElemInfo: u32 0x10000, u32 flags, u16 priority, u32 palette, u32 x3 palette
// transition parameters, CLSID of the palette transition and its 28 bytes, then
// at 0x46 the element name block and the element sites { i32 x, y, z; u8 visible;
// u8 flags; u16 element index } sorted by ascending z (front to back)
bool Layout::loadInfo(Common::SeekableReadStream &s) {
	if (s.readUint32LE() != kPersistVersion)
		return false;
	_flags = s.readUint32LE();
	_priority = s.readUint16LE();
	_paletteId = s.readUint32LE();
	for (int i = 0; i < 3; i++)
		_transParams[i] = s.readUint32LE();
	int cls = readClassId(s);
	_transClass = cls < 0 ? 0 : cls;
	_transData.resize(28);
	s.read(_transData.data(), 28);

	Common::Array<Common::String> names;
	if (!readNameBlock(s, names))
		return false;
	_elements.resize(names.size());
	for (uint i = 0; i < names.size(); i++)
		_elements[i].name = names[i];
	for (uint i = 0; i < names.size(); i++) {
		int32 x = s.readSint32LE(), y = s.readSint32LE(), z = s.readSint32LE();
		byte visible = s.readByte(), flags = s.readByte();
		uint16 index = s.readUint16LE();
		if (index >= _elements.size())
			return false;
		Element &e = _elements[index];
		e.x = x;
		e.y = y;
		e.z = z;
		e.visible = visible != 0;
		e.siteFlags = flags;
		_siteOrder.push_back(index);
	}
	return !s.err();
}

// Sprite element: u32 0x10000, u32 speed, u32 iterations, u16 x3, u32 initial
// segment, u32 x2, u16 flags, u32 next ID, u16 segments, the segments, then the
// triggers { u16 event, u16 kind, u32 segment, u32 hotspot, action list }
bool Layout::loadSprite(Common::SeekableReadStream &s, Element &e) {
	if (s.readUint32LE() != kPersistVersion)
		return false;
	e.speed = s.readUint32LE();
	e.iterations = s.readUint32LE();
	for (int i = 0; i < 3; i++)
		e.spriteWords[i] = s.readUint16LE();
	e.initialSegment = s.readUint32LE();
	e.spriteLongs[0] = s.readUint32LE();
	e.spriteLongs[1] = s.readUint32LE();
	e.spriteFlags = s.readUint16LE();
	e.nextId = s.readUint32LE();
	uint16 count = s.readUint16LE();
	if (count > kMaxCount)
		return false;

	// Path segment (0x38 bytes): u32 ID, PibList, sound, sound parameter; i32 end
	// point x, y, z; u32 flags, duration (ms), frames; u16 hotspots, u32 next
	// hotspot ID, i32 cursor, u16 has transition, u32 hotspot data size; then the
	// hotspots and the optional transition { u32 duration, i32 param, CLSID }
	for (uint i = 0; i < count; i++) {
		PathSegment seg;
		seg.id = s.readUint32LE();
		seg.pibList = s.readUint32LE();
		seg.sound = s.readUint32LE();
		seg.soundParam = s.readUint32LE();
		seg.x = s.readSint32LE();
		seg.y = s.readSint32LE();
		seg.z = s.readSint32LE();
		seg.flags = s.readUint32LE();
		seg.duration = s.readUint32LE();
		seg.frames = s.readUint32LE();
		uint16 nhot = s.readUint16LE();
		seg.nextHotspotId = s.readUint32LE();
		seg.cursor = s.readSint32LE();
		seg.hasTransition = s.readUint16LE() != 0;
		uint32 hotSize = s.readUint32LE();
		int64 hotEnd = s.pos() + hotSize;
		for (uint k = 0; k < nhot; k++) {
			Hotspot h;
			h.type = s.readUint16LE();
			h.index = s.readUint32LE();
			h.bounds.left = s.readSint32LE();
			h.bounds.top = s.readSint32LE();
			h.bounds.right = s.readSint32LE();
			h.bounds.bottom = s.readSint32LE();
			h.id = s.readUint32LE();
			h.cursor = s.readSint32LE();
			uint16 npts = s.readUint16LE();
			if (npts > kMaxCount)
				return false;
			for (uint n = 0; n < npts; n++) {
				int32 px = s.readSint32LE();
				int32 py = s.readSint32LE();
				h.points.push_back(Common::Point(px, py));
			}
			seg.hotspots.push_back(h);
		}
		if (s.pos() != hotEnd) {
			warning("Layout %s, %s: hotspot data size mismatch", _name.c_str(), e.name.c_str());
			s.seek(hotEnd);
		}
		seg.transDuration = 0;
		seg.transParam = 0;
		seg.transClass = 0;
		if (seg.hasTransition) {
			seg.transDuration = s.readUint32LE();
			seg.transParam = s.readSint32LE();
			int cls = readClassId(s);
			if (cls != kClassNone && cls != kClassPibTransition)
				return false;
			seg.transClass = cls;
		}
		e.segments.push_back(seg);
	}

	count = s.readUint16LE();
	for (uint i = 0; i < count && count < kMaxCount; i++) {
		Trigger t;
		t.event = s.readUint16LE();
		t.kind = s.readUint16LE();
		t.segmentId = s.readUint32LE();
		t.hotspotId = s.readUint32LE();
		if (!t.list.load(s))
			return false;
		e.triggers.push_back(t);
	}
	return !s.err();
}

// Video element: u32 0x10000, u32 width, u32 height, u32 AVI media ID, i32, u32,
// u16 flags, u16 triggers { u16 event, u16 kind, action list }
bool Layout::loadVideo(Common::SeekableReadStream &s, Element &e) {
	if (s.readUint32LE() != kPersistVersion)
		return false;
	e.width = s.readUint32LE();
	e.height = s.readUint32LE();
	e.videoId = s.readUint32LE();
	e.videoParam = s.readSint32LE();
	e.videoParam2 = s.readUint32LE();
	e.videoFlags = s.readUint16LE();
	uint16 count = s.readUint16LE();
	for (uint i = 0; i < count && count < kMaxCount; i++) {
		Trigger t;
		t.event = s.readUint16LE();
		t.kind = s.readUint16LE();
		t.segmentId = t.hotspotId = 0;
		if (!t.list.load(s))
			return false;
		e.triggers.push_back(t);
	}
	return !s.err();
}

// ElemData: the elements in index order, each a CLSID followed by its persisted data
bool Layout::loadElements(Common::SeekableReadStream &s) {
	for (uint i = 0; i < _elements.size(); i++) {
		Element &e = _elements[i];
		int cls = readClassId(s);
		e.classId = cls < 0 ? 0 : cls;
		bool ok;
		if (cls == kClassSprite)
			ok = loadSprite(s, e);
		else if (cls == kClassVideo)
			ok = loadVideo(s, e);
		else
			ok = false;
		if (!ok) {
			warning("Layout %s: element %d (%s, class %x) doesn't load", _name.c_str(), i, e.name.c_str(), cls);
			return false;
		}
	}
	if (s.pos() != s.size())
		warning("Layout %s: %d bytes left in ElemData", _name.c_str(), (int)(s.size() - s.pos()));
	return true;
}

// LyoActLists: u16 1, u16 1, then two action lists
bool Layout::loadLists(Common::SeekableReadStream &s) {
	_listHeader[0] = s.readUint16LE();
	_listHeader[1] = s.readUint16LE();
	while (s.pos() < s.size()) {
		ActionList list;
		if (!list.load(s))
			return false;
		_lists.push_back(list);
	}
	return true;
}

bool Layout::load(const CompoundFile &file, const Common::String &path, const Common::String &name) {
	_name = name;
	Common::ScopedPtr<Common::SeekableReadStream> info(file.openStream(path + "/LyoElemInfo"));
	if (!info || !loadInfo(*info)) {
		warning("Layout %s: bad LyoElemInfo", name.c_str());
		return false;
	}
	Common::ScopedPtr<Common::SeekableReadStream> data(file.openStream(path + "/ElemData"));
	if (data && !loadElements(*data))
		return false;
	if (!data && !_elements.empty())
		return false;
	Common::ScopedPtr<Common::SeekableReadStream> lists(file.openStream(path + "/LyoActLists"));
	if (lists && !loadLists(*lists)) {
		warning("Layout %s: bad LyoActLists", name.c_str());
		return false;
	}
	debugC(1, kDebugLayout, "Layout %s: %d elements, palette %x, %d layout lists",
		name.c_str(), _elements.size(), _paletteId, _lists.size());
	return true;
}

int Layout::findElement(const Common::String &name) const {
	for (uint i = 0; i < _elements.size(); i++) {
		if (_elements[i].name.equalsIgnoreCase(name))
			return i;
	}
	return -1;
}

// TITLE/ViewInfo: u32 0x10000, u16 x3, u16 name length, name, u32 width, u32 height
// TITLE/<view>/ArLyInfo: u32 0x10000, 8 bytes, then the layout name block
bool Title::load(const CompoundFile &file) {
	Common::ScopedPtr<Common::SeekableReadStream> view(file.openStream("TITLE/ViewInfo"));
	if (!view)
		return false;
	view->skip(10);
	uint16 len = view->readUint16LE();
	_viewName = readCString(*view);
	view->seek(12 + len + 1);
	_width = view->readUint32LE();
	_height = view->readUint32LE();

	Common::ScopedPtr<Common::SeekableReadStream> arena(file.openStream("TITLE/" + _viewName + "/ArLyInfo"));
	if (!arena)
		return false;
	arena->skip(12);
	if (!readNameBlock(*arena, _layouts))
		return false;
	debugC(1, kDebugLoading, "View %s %dx%d, %d layouts", _viewName.c_str(), _width, _height, _layouts.size());
	return true;
}

int Title::findLayout(const Common::String &name) const {
	for (uint i = 0; i < _layouts.size(); i++) {
		if (_layouts[i].equalsIgnoreCase(name))
			return i;
	}
	return -1;
}

Layout *Title::loadLayout(const CompoundFile &file, uint index) const {
	if (index >= _layouts.size())
		return nullptr;
	Layout *layout = new Layout();
	if (!layout->load(file, "TITLE/" + _viewName + "/" + _layouts[index], _layouts[index])) {
		delete layout;
		return nullptr;
	}
	return layout;
}

const char *getEventName(uint16 event, uint16 kind) {
	if (kind == 0) {
		switch (event) {
		case kEventMouseMove: return "Mouse Move";
		case kEventMouseDrag: return "Mouse Drag";
		case kEventLButtonDown: return "Left Button Down";
		case kEventLButtonUp: return "Left Button Up";
		case kEventLButtonDblClk: return "Left Button Double Click";
		case kEventRButtonDown: return "Right Button Down";
		case kEventRButtonUp: return "Right Button Up";
		case kEventRButtonDblClk: return "Right Button Double Click";
		default: break;
		}
	} else {
		switch (event) {
		case kEventShow: return "Show";
		case kEventHide: return "Hide";
		case kEventSpriteStart: return "Sprite Start";
		case kEventSpriteStop: return "Sprite Stop";
		case kEventPathSegBegin: return "Path Seg Begin";
		case kEventPathSegEnd: return "Path Seg End";
		case kEventVideoPlay: return "Video Play";
		case kEventVideoStop: return "Video Stop";
		case 18: return "Video Pause";
		case 19: return "Video Resume";
		case 20: return "Video Play Clip";
		case 21: return "Video Seek";
		default: break;
		}
	}
	return "mystery trigger";
}

// Action names from the ENUMACTIONDEFS tables of MSPAM.DLL and MSBSOLAR.DLL
static const char *const kTitleActions[] = {
	"Show View", "Hide View", "Quit Title", "FP: Open", "FP: Close", "FP: Set Gravity",
	"FP: Set Moon", "FP: Start/Stop", "FP: General params", "FP: Set Bus", "FP: Bus Thruster",
	"Eval: If Then Else", "Eval: Expr", "Eval: Start Sprite Expr", "Set Path Seg Mid", "Set View",
	"FP: Set Bonk Sprite", "FP: Set Shadow Sprite", "Eval: Rand number", "Eval: Free State Vars",
	"FP: Set Radians Sprite", "FP: Bus Settings", "Exec: Start Game", "Debug: Comment",
	"Debug: Level", "Eval: Case statement", "Eval: Enum Sprites", "Eval: Add Expr Action",
	"Idle Mode", "Start idle/transition", "Stop idle/transition", "Title GotoLayout",
	"Start transition layout", "End transition layout", "AVI Init", "AVI Skit Start",
	"AVI Add Bit", "AVI Add Stop", "AVI Close"
};

static const char *const kSoundActions[] = {
	"Play Sound", "Add Sound", "Play Group", "Stop Group", nullptr, "Pause Group",
	"Resume Group", "Kill Group", "Kill All"
};

static const char *const kViewActions[] = {
	"Goto Layout", "Goto Layout Relative", "Set Layout Priority", "Reset Layout", "Reset Layout Relative"
};

static const char *const kLayoutActions[] = {
	"Change Palette", "Set Palette Transition", "Set Bitwise Transition"
};

static const char *spriteAction(uint16 id) {
	static const char *const names[] = {
		"Show", "Hide", "Start", "Stop", "Set Speed", "Set Iterations", "Set Path Segment"
	};
	return id >= 7 && id <= 13 ? names[id - 7] : nullptr;
}

static const char *videoAction(uint16 id) {
	static const char *const names[] = {
		"Play", "Stop", "Pause", "Resume", "Play Clip", "Seek"
	};
	if (id == 7)
		return "Show";
	if (id == 8)
		return "Hide";
	return id >= 16 && id <= 21 ? names[id - 16] : nullptr;
}

Common::String getActionName(const Action &a, const Layout *layout) {
	const char *name = nullptr;
	if (a.targetKind == kTargetTitle) {
		if (a.id < ARRAYSIZE(kTitleActions))
			name = kTitleActions[a.id];
	} else if (a.targetKind == kTargetSound) {
		if (a.id >= 0x200 && a.id - 0x200 < (int)ARRAYSIZE(kSoundActions))
			name = kSoundActions[a.id - 0x200];
	} else {
		int el = layout ? layout->findElement(a.target) : -1;
		if (el >= 0) {
			name = layout->_elements[el].isVideo() ? videoAction(a.id) : spriteAction(a.id);
		} else if (layout && a.target.equalsIgnoreCase(layout->_name) && a.id < ARRAYSIZE(kLayoutActions)) {
			name = kLayoutActions[a.id];
		} else if (a.id < ARRAYSIZE(kViewActions) && a.targetKind >= 2) {
			name = kViewActions[a.id];
		} else {
			name = spriteAction(a.id);
			if (!name)
				name = videoAction(a.id);
		}
	}
	return name ? Common::String(name) : Common::String::format("action %d", a.id);
}

Common::String describeAction(const Action &a, const Layout *layout) {
	Common::String str = getActionName(a, layout);
	if (!a.target.empty())
		str += Common::String::format(" [%s]", a.target.c_str());
	if (a.targetKind < kTargetTitle)
		str += Common::String::format(" ^%d", a.targetKind);
	for (uint i = 0; i < a.params.size(); i++)
		str += (i ? ", " : " ") + a.params[i].toString();
	return str;
}

} // End of namespace MSPAM
