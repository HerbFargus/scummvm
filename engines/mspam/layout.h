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

#ifndef MSPAM_LAYOUT_H
#define MSPAM_LAYOUT_H

#include "common/array.h"
#include "common/rect.h"
#include "common/str.h"
#include "common/stream.h"

namespace MSPAM {

class CompoundFile;

/** The last byte of the {CBE571xx-031F-1069-9603-08002B36A446} class IDs */
enum ClassId {
	kClassNone = 0x00,
	kClassSprite = 0x61,
	kClassPibTransition = 0x68,
	kClassVideo = 0x69,
	kClassPalTransition = 0x6F
};

/** Trigger events: Windows mouse messages, or element events */
enum TriggerEvent {
	kEventShow = 7,
	kEventHide = 8,
	kEventSpriteStart = 9,
	kEventSpriteStop = 10,
	kEventPathSegBegin = 14,
	kEventPathSegEnd = 15,
	kEventVideoPlay = 16,
	kEventVideoStop = 17,
	kEventMouseMove = 0x200,
	kEventLButtonDown = 0x201,
	kEventLButtonUp = 0x202,
	kEventLButtonDblClk = 0x203,
	kEventRButtonDown = 0x204,
	kEventRButtonUp = 0x205,
	kEventRButtonDblClk = 0x206,
	kEventMouseDrag = 0x401
};

/** Action target kinds (the word after the action ID) */
enum TargetKind {
	kTargetTitle = 0x80,  ///< GENTITLE (MSBSOLAR.DLL) actions, no target name
	kTargetSound = 0x81   ///< SND actions, no target name
	// Below 0x80: a named element, layout or view; the value counts levels up
	// from the list's owner (0 = the layout's own elements from a layout list)
};

/** An action parameter (APRM) */
struct ActionParam {
	enum {
		kTypeWord = 0,       ///< also 2
		kTypeLong = 1,       ///< also 3, 5 (media ID), 6, 7
		kTypeString = 4,
		kTypeBlob = 0x8000   ///< and above: u32 size + data (a persisted object)
	};
	uint16 type;
	uint32 value;
	Common::String str;
	Common::Array<byte> blob;

	Common::String toString() const;
};

struct Action {
	uint16 id;
	uint16 targetKind;
	Common::String target;
	Common::Array<ActionParam> params;
};

/**
 * An action list: u32 0x10000, u32 size, then u16 flags, u16 flags2, u16 count
 * and the actions { u16 id, u16 target kind, target name (even-padded), u16
 * nparams, params { u16 type, value (even-padded) } }.
 */
struct ActionList {
	uint16 flags;
	uint16 flags2;
	Common::Array<Action> actions;

	bool load(Common::SeekableReadStream &s);
};

/** An element trigger: the event, a path segment and hotspot filter (0 = any), and its list */
struct Trigger {
	uint16 event;
	uint16 kind;     ///< 0 = mouse message, 1 = element event
	uint32 segmentId;
	uint32 hotspotId;
	ActionList list;
};

/** A polygon hotspot on a path segment, in element coordinates */
struct Hotspot {
	uint16 type;     ///< always 2 (polygon)
	uint32 index;
	Common::Rect bounds;
	uint32 id;
	int32 cursor;
	Common::Array<Common::Point> points;

	bool contains(int x, int y) const;
};

struct PathSegment {
	uint32 id;
	uint32 pibList;
	uint32 sound;
	uint32 soundParam;
	int32 x, y, z;   ///< end point, relative to the start
	uint32 flags;
	uint32 duration; ///< milliseconds
	uint32 frames;
	uint32 nextHotspotId;
	int32 cursor;    ///< -1 = default cursor
	bool hasTransition;
	uint32 transDuration;
	int32 transParam;
	byte transClass;
	Common::Array<Hotspot> hotspots;
};

struct Element {
	Common::String name;
	byte classId;
	// Element site (from LyoElemInfo)
	int32 x, y, z;
	bool visible;
	byte siteFlags;

	// Sprite (CLSID ...61)
	uint32 speed;
	uint32 iterations;
	uint16 spriteWords[3];
	uint32 initialSegment;
	uint32 spriteLongs[2];
	uint16 spriteFlags;
	uint32 nextId;
	Common::Array<PathSegment> segments;

	// Video (CLSID ...69)
	uint32 width, height;
	uint32 videoId;
	int32 videoParam;
	uint32 videoParam2;
	uint16 videoFlags;

	Common::Array<Trigger> triggers;

	Element();
	bool isSprite() const { return classId == kClassSprite; }
	bool isVideo() const { return classId == kClassVideo; }
	const PathSegment *findSegment(uint32 id) const;
	const PathSegment *getCurrentSegment() const;
};

/**
 * A layout storage: LyoElemInfo (header, element names, element sites sorted
 * front to back), ElemData (the persisted elements) and LyoActLists (the
 * layout's two action lists).
 */
class Layout {
public:
	Layout();

	bool load(const CompoundFile &file, const Common::String &path, const Common::String &name);
	int findElement(const Common::String &name) const;

	Common::String _name;
	uint32 _flags;
	uint16 _priority;
	uint32 _paletteId;
	uint32 _transParams[3];
	byte _transClass;
	Common::Array<byte> _transData;
	Common::Array<Element> _elements;
	/** Element indices front to back (ascending z) */
	Common::Array<uint> _siteOrder;
	uint16 _listHeader[2];
	/** [0] runs when the layout is entered, [1] when it is left (tentative) */
	Common::Array<ActionList> _lists;

private:
	bool loadInfo(Common::SeekableReadStream &s);
	bool loadElements(Common::SeekableReadStream &s);
	bool loadSprite(Common::SeekableReadStream &s, Element &e);
	bool loadVideo(Common::SeekableReadStream &s, Element &e);
	bool loadLists(Common::SeekableReadStream &s);
};

/** The title's TDF: the view and its layout names (ArLyInfo) */
class Title {
public:
	bool load(const CompoundFile &file);
	int findLayout(const Common::String &name) const;
	Layout *loadLayout(const CompoundFile &file, uint index) const;

	Common::String _viewName;
	int _width, _height;
	Common::Array<Common::String> _layouts;
};

/** Reads a name block: u16 alloc, u16 size, u16 count, u16 offsets[count], strings */
bool readNameBlock(Common::SeekableReadStream &s, Common::Array<Common::String> &names);

const char *getEventName(uint16 event, uint16 kind);
/** Names an action from its target kind and, for named targets, the target's class */
Common::String getActionName(const Action &action, const Layout *layout);
Common::String describeAction(const Action &action, const Layout *layout);

} // End of namespace MSPAM

#endif // MSPAM_LAYOUT_H
