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

#ifndef MSPAM_RUNTIME_H
#define MSPAM_RUNTIME_H

#include "audio/mixer.h"
#include "common/array.h"
#include "common/hashmap.h"
#include "common/hash-str.h"
#include "common/list.h"
#include "common/random.h"
#include "common/rect.h"
#include "common/str.h"
#include "graphics/surface.h"

#include "mspam/layout.h"
#include "mspam/script.h"

namespace MSPAM {

class CompoundFile;
class Evaluator;
class MediaManager;
class SoundManager;
struct Pib;
struct PibList;

/** Path segment flag bits (inferred from the data, see NOTES.md) */
enum SegmentFlags {
	kSegRelative = 0x01,       ///< the end point is relative to the start, else absolute
	kSegContinueFrames = 0x04, ///< the animation carries on from the previous segment
	kSegLoopFrames = 0x08,     ///< the animation loops, else it holds its last frame
	kSegContinueSound = 0x10,  ///< the previous segment's sound carries on
	kSegHotWhileMoving = 0x80  ///< clickable while it moves (FUN_1048_4aa2)
};

/** An element of a running layout */
struct ElementState {
	Element *def;
	bool visible;
	int32 x, y, z;          ///< where the path starts: the registration point (a video's top-left)
	uint rank;              ///< position in the original site order, to keep z ties stable

	// Sprite path playback
	int seg;                ///< current path segment index, -1 without segments
	bool running;
	uint32 elapsed;         ///< ms into the current segment
	uint32 animTicks;       ///< animation position in 1/1000 frames
	uint32 speed;
	uint32 iterations;
	uint32 iterationsLeft;
	uint32 soundId;
	Audio::SoundHandle sound;

	const PathSegment *segment() const;
	uint32 segmentDuration() const;
};

/** A layout's runtime state; kept while its priority isn't Low, until a Reset Layout */
struct LayoutState {
	Layout *layout;
	uint serial;
	Common::Array<ElementState> elements;
	Common::Array<uint> order; ///< element indices front to back
	bool lowPriority;          ///< Set Layout Priority Low: dropped when left
	bool reset;                ///< Reset Layout while it's showing: dropped when left

	LayoutState() : layout(nullptr), serial(0), lowPriority(false), reset(false) {}
	~LayoutState() { delete layout; }
	int find(const Common::String &name) const;
};

/**
 * The action runtime: the current layout and its elements, the action list
 * queue (lists run from the queue, like the original's PROCESSACTIONLIST from
 * the idle loop), sprite path playback, the title's actions and expressions,
 * the sound manager and palette transitions.
 */
class Runtime : public ScriptHost {
public:
	Runtime(MediaManager *media, const CompoundFile &tdf, const Title *title, Audio::Mixer *mixer);
	~Runtime() override;

	void start(const Common::String &layout);
	void update(uint32 now);
	void render(Graphics::Surface &dst);
	const byte *getPalette() const { return _screenPalette; }

	/** A Windows mouse message (kEventLButtonDown...) at screen coordinates */
	void mouseEvent(uint16 msg, int x, int y);
	/** Esc or space: runs the expression in GVarESC */
	void escape();

	bool quitRequested() const { return _quit; }
	Common::String getLayoutName() const;
	bool isIdle() const;
	/** The screen position of a visible element's picture centre, or a hotspot's */
	bool findElement(const Common::String &name, uint32 hotspotId, Common::Point &pt) const;
	void logStatistics() const;
	void listClickable() const;

	// ScriptHost
	bool hasElement(const Common::String &name) override;
	int32 getProperty(const Common::String &element, int prop) override;
	void setProperty(const Common::String &element, int prop, int32 value) override;

private:
	struct QueuedList {
		uint serial;
		int owner;             ///< element index, -1 for a layout list
		const ActionList *list;
		uint pos;
	};
	struct Skit {
		Common::Array<Common::String> exprs;
		Common::String stopExpr;
	};

	// Layouts
	LayoutState *loadLayoutState(const Common::String &name);
	void switchLayout();
	void requestLayout(const Common::String &name);

	// Action lists and events
	void queueList(int owner, const ActionList &list, bool front = false);
	void fireEvent(int el, uint16 event, uint32 segmentId = 0);
	void processQueue();
	void runList(QueuedList &item);
	void executeAction(int owner, const Action &a);
	void elementAction(int el, const Action &a);
	void layoutAction(const Action &a);
	void viewAction(const Action &a);
	void titleAction(const Action &a);
	void soundAction(const Action &a);
	void unimplemented(const Action &a, const char *what);

	// Sprites
	void setVisible(int el, bool visible);
	void startSprite(int el);
	void stopSprite(int el);
	void setPathSegment(int el, int index);
	void beginSegment(int el);
	void updateSprite(int el, uint32 dt);
	void getPathPoint(const ElementState &e, int32 &x, int32 &y, int32 &z) const;
	Common::Point getPosition(const ElementState &e) const;
	Common::Point getOrigin(const ElementState &e, const Pib *pib) const;
	int32 getZ(const ElementState &e) const;
	const Pib *getFrame(const ElementState &e) const;
	int hitTest(const ElementState &e, int x, int y, uint32 &hotspotId) const;
	void sortElements();
	int findElementState(const Common::String &name) const;

	// Palette
	void changePalette(uint32 id, const ActionParam *transition, bool block);
	void updatePalette(uint32 now);

	MediaManager *_media;
	const CompoundFile &_tdf;
	const Title *_title;
	SoundManager *_sound;
	Variables _vars;
	Evaluator *_eval;

	Common::HashMap<Common::String, LayoutState *, Common::IgnoreCase_Hash, Common::IgnoreCase_EqualTo> _cache;
	LayoutState *_layout;
	uint _nextSerial;
	Common::String _pendingLayout;
	bool _reenter;
	bool _quit;

	Common::List<QueuedList> _queue;
	uint32 _now;
	uint32 _lastUpdate;
	uint32 _blockedUntil;
	bool _leaving;

	// Palettes: the layout palette, scaled by the fade level (0-256) for the screen
	byte _palette[256 * 3];
	byte _screenPalette[256 * 3];
	int _fadeFrom, _fadeTo, _fadeLevel;
	uint32 _fadeStart, _fadeDuration;

	Common::Array<Skit> _skits;
	bool _skitsOpen;

	uint32 _lastDownTime;
	Common::Point _lastDownPos;

	Common::HashMap<Common::String, uint> _unimplemented;
	Common::HashMap<Common::String, uint> _visited;
	Common::HashMap<Common::String, bool> _warned;
	Common::RandomSource _random;
};

} // End of namespace MSPAM

#endif // MSPAM_RUNTIME_H
