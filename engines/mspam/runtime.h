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
#include "mspam/video.h"

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
	kSegContinueSound = 0x10   ///< the previous segment's sound carries on
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

	// Video playback
	enum VideoState {
		kVideoStopped,
		kVideoPlaying,
		kVideoPaused
	};
	VideoPlayer *video;     ///< the open AVI while the video shows
	VideoState videoState;
	uint16 videoFlags;      ///< VideoFlags (the skits set kVideoNaturalSize and kVideoFrames)
	int32 w, h;             ///< the video window size
	int32 playFrom;         ///< where Play starts: the last Seek

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
	~LayoutState();
	int find(const Common::String &name) const;
};

/**
 * A moon of the title's flight path (MSBSOLAR.DLL CMOON): a sprite that a time
 * event moves around an ellipse about the gravity point, carrying its shadow
 * sprites and running expressions as it passes given angles.
 */
struct FlightMoon {
	struct RadiansExpr {
		double radians;
		Common::String expr;
	};
	struct Shadow {
		Common::String name;
		int32 dx, dy, dz;
	};

	Common::String name;
	int32 centerX, centerY; ///< the gravity point when the moon was made
	double radiusX, radiusY; ///< 'o'; a zero Y radius means a circle
	double angle;           ///< 'i', radians
	double step;            ///< radians per tick: 2 pi / 'p'
	bool forward;           ///< 'p' isn't negative
	double skipFrom, skipTo; ///< 's' and 'r': angles that are jumped over
	uint32 delay;           ///< 'd': ms per tick
	Common::Array<RadiansExpr> radiansExprs;
	Common::Array<Shadow> shadows;
	bool ticking;
	uint32 lastTick;
};

/** The title's flight path object (MSBSOLAR.DLL, FP: Open to FP: Close) */
struct Flight {
	Common::String layout;  ///< where it was opened: its moons are that layout's sprites
	int32 gravityX, gravityY;
	Common::Array<FlightMoon> moons; ///< newest first, as the original's list
	int current;            ///< the moon that Set Moon picked, -1 for none
	bool started;           ///< toggled by FP: Start/Stop

	Flight() : gravityX(0), gravityY(0), current(-1), started(false) {}
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

	/** Exec: Start Game: the command line for the arcade game (see BusGame), which the engine loop runs */
	void startBusGame(const Common::String &command);
	/** The pending game's planet and whether Liz introduces it (-x, the first game only) */
	bool takeBusGame(Common::String &planet, bool &lizIntro);
	/** LobotomyGameResult (MSBSOLAR.DLL FUN_1008_4574) */
	void busGameEnded(int result);
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
	/** AVI Add Bit and AVI Add Stop entries of the title's skit table */
	struct SkitEntry {
		bool stop;
		int32 x, y, w, h;
		int32 from, to;     ///< frames
		int expr;           ///< the AVIExpr<n> variable to run first, -1 for none
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
	/** The element that took the mouse message, -1 for none */
	int dispatchMouse(uint16 msg, int x, int y);

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

	// Videos
	bool openVideo(int el);
	void closeVideo(int el);
	void videoAction(int el, const Action &a);
	void playVideo(int el, int32 from, int32 to, bool clip);
	void stopVideo(int el);
	void videoEnded(int el);
	void updateVideos();
	void renderVideo(const ElementState &e, Graphics::Surface &dst) const;
	Common::Rect getVideoRect(const ElementState &e) const;
	bool isVideoPlaying() const;

	// AVI skits (MSBSOLAR.DLL)
	void skitInit();
	void skitAdd(const Action &a, bool stop);
	void skitStart(uint n);
	void skitNext();
	void skitEnd();
	int findElementState(const Common::String &name) const;

	// Flight paths (MSBSOLAR.DLL, SE-Mercury's orbits)
	void flightAction(const Action &a);
	void flightParam(const Action &a);
	void setMoon(const Common::String &name);
	void startStopFlight();
	void updateFlight();
	void moonTick(FlightMoon &moon);
	void getSpritePosition(int el, int32 &x, int32 &y, int32 &z) const;
	void setSpritePosition(int el, int32 x, int32 y, int32 z);

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
	Common::String _busGame;
	bool _busGameIntro;

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

	bool _skipVideos;
	int _skitElement;          ///< the layout's "AVI" element after AVI Init, -1 without skits
	Common::Array<SkitEntry> _skitEntries;
	int _skitPos;              ///< the next entry of the playing skit, -1 when none plays
	uint _skitExprs;

	Flight *_flight;           ///< FP: Open to FP: Close

	uint32 _lastDownTime;
	Common::Point _lastDownPos;

	Common::HashMap<Common::String, uint> _unimplemented;
	Common::HashMap<Common::String, uint> _visited;
	Common::HashMap<Common::String, bool> _warned;
	Common::RandomSource _random;
};

} // End of namespace MSPAM

#endif // MSPAM_RUNTIME_H
