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

#ifndef MTROPOLIS_PLUGIN_KW_H
#define MTROPOLIS_PLUGIN_KW_H

#include "mtropolis/modifiers.h"
#include "mtropolis/modifier_factory.h"
#include "mtropolis/runtime.h"
#include "mtropolis/plugin/kw_data.h"

#include "graphics/managed_surface.h"

namespace Video {

class VideoDecoder;

} // End of namespace Video

namespace MTropolis {

namespace KW {

class StrUtilModifier : public Modifier {
public:
	StrUtilModifier();
	~StrUtilModifier();

	bool load(const PlugInModifierLoaderContext &context, const Data::KW::StrUtilModifier &data);

	bool respondsToEvent(const Event &evt) const override;
	VThreadState consumeMessage(Runtime *runtime, const Common::SharedPtr<MessageProperties> &msg) override;

	void disable(Runtime *runtime) override;

#ifdef MTROPOLIS_DEBUG_ENABLE
	const char *debugGetTypeName() const override { return "StrUtil Modifier"; }
	void debugInspect(IDebugInspectionReport *report) const override;
#endif

private:
	Common::SharedPtr<Modifier> shallowClone() const override;
	const char *getDefaultName() const override;
};

class KWSmackerModifier : public Modifier, public IPostEffect, public IPlayMediaSignalReceiver {
public:
	KWSmackerModifier();
	KWSmackerModifier(const KWSmackerModifier &other);
	~KWSmackerModifier();

	bool load(const PlugInModifierLoaderContext &context, const Data::KW::KWSmackerModifier &data);

	bool respondsToEvent(const Event &evt) const override;
	VThreadState consumeMessage(Runtime *runtime, const Common::SharedPtr<MessageProperties> &msg) override;

	void disable(Runtime *runtime) override;

	bool readAttribute(MiniscriptThread *thread, DynamicValue &result, const Common::String &attrib) override;
	MiniscriptInstructionOutcome writeRefAttribute(MiniscriptThread *thread, DynamicValueWriteProxy &result, const Common::String &attrib) override;

	void playMedia(Runtime *runtime, Project *project) override;
	void renderPostEffect(Graphics::ManagedSurface &surface) const override;

#ifdef MTROPOLIS_DEBUG_ENABLE
	const char *debugGetTypeName() const override { return "KWSmacker Modifier"; }
	SupportStatus debugGetSupportStatus() const override { return kSupportStatusPartial; }
	void debugInspect(IDebugInspectionReport *report) const override;
#endif

private:
	Common::SharedPtr<Modifier> shallowClone() const override;
	const char *getDefaultName() const override;

	void linkInternalReferences(ObjectLinkingScope *scope) override;
	void visitInternalReferences(IStructuralReferenceVisitor *visitor) override;

	void startPlaying(Runtime *runtime);
	void stopPlaying();
	void finishPlaying(Runtime *runtime);

	Event _playWhen;
	Event _stopWhen;
	MessengerSendSpec _sendWhenDone;
	Common::String _moviePath;
	bool _changePalette;

	Runtime *_runtime;
	Common::SharedPtr<Video::VideoDecoder> _decoder;
	Common::SharedPtr<PlayMediaSignaller> _playMediaSignaller;
	Graphics::ManagedSurface _frameSurface;
	const Graphics::Surface *_frame;
	bool _isPlaying;
};

class EditCtrlModifier : public Modifier, public IPostEffect, public IKeyboardEventReceiver {
public:
	EditCtrlModifier();
	EditCtrlModifier(const EditCtrlModifier &other);
	~EditCtrlModifier();

	bool load(const PlugInModifierLoaderContext &context, const Data::KW::EditCtrlModifier &data);

	bool respondsToEvent(const Event &evt) const override;
	VThreadState consumeMessage(Runtime *runtime, const Common::SharedPtr<MessageProperties> &msg) override;

	void disable(Runtime *runtime) override;

	bool readAttribute(MiniscriptThread *thread, DynamicValue &result, const Common::String &attrib) override;
	MiniscriptInstructionOutcome writeRefAttribute(MiniscriptThread *thread, DynamicValueWriteProxy &result, const Common::String &attrib) override;

	void renderPostEffect(Graphics::ManagedSurface &surface) const override;
	void onKeyboardEvent(Runtime *runtime, const KeyboardInputEvent &keyEvt) override;

#ifdef MTROPOLIS_DEBUG_ENABLE
	const char *debugGetTypeName() const override { return "EditCtrl Modifier"; }
	SupportStatus debugGetSupportStatus() const override { return kSupportStatusPartial; }
	void debugInspect(IDebugInspectionReport *report) const override;
#endif

private:
	enum WritableAttribute {
		kAttribText,
		kAttribTextLimit,
		kAttribUpdate,
		kAttribEnable,
		kAttribDisable,
		kAttribSetFocus,
	};

	struct WriteProxy {
		static MiniscriptInstructionOutcome write(MiniscriptThread *thread, const DynamicValue &value, void *objectRef, uintptr ptrOrOffset);
		static MiniscriptInstructionOutcome refAttrib(MiniscriptThread *thread, DynamicValueWriteProxy &proxy, void *objectRef, uintptr ptrOrOffset, const Common::String &attrib);
		static MiniscriptInstructionOutcome refAttribIndexed(MiniscriptThread *thread, DynamicValueWriteProxy &proxy, void *objectRef, uintptr ptrOrOffset, const Common::String &attrib, const DynamicValue &index);
	};

	Common::SharedPtr<Modifier> shallowClone() const override;
	const char *getDefaultName() const override;

	void writeAttribute(Runtime *runtime, WritableAttribute attrib, const DynamicValue &value);
	void show(Runtime *runtime);
	void hide();
	void sendNamedMessage(Runtime *runtime, const char *messageName);

	Common::String _text;
	uint _textLimit;

	Runtime *_runtime;
	bool _isShown;
	Common::SharedPtr<KeyboardEventSignaller> _keySignaller;
};

class GameModifier : public Modifier {
public:
	bool load(const PlugInModifierLoaderContext &context, const Data::KW::GameModifier &data);

	bool respondsToEvent(const Event &evt) const override;
	VThreadState consumeMessage(Runtime *runtime, const Common::SharedPtr<MessageProperties> &msg) override;

	void disable(Runtime *runtime) override {}

#ifdef MTROPOLIS_DEBUG_ENABLE
	const char *debugGetTypeName() const override { return "Game-specific Modifier"; }
	SupportStatus debugGetSupportStatus() const override { return kSupportStatusNone; }
#endif

private:
	Common::SharedPtr<Modifier> shallowClone() const override;
	const char *getDefaultName() const override;

	Event _event1;
	Event _event2;
};

// Whale Maze mini-game from Magic School Bus Whales & Dolphins. The whale
// moves along a path graph, chased by orcas, past timed hazards. The level
// graphs are read from the original WhaleMaze.r32 plug-in.
class WhaleMazeModifier : public Modifier, public IKeyboardEventReceiver {
public:
	WhaleMazeModifier();
	WhaleMazeModifier(const WhaleMazeModifier &other);
	~WhaleMazeModifier();

	bool load(const PlugInModifierLoaderContext &context, const Data::KW::GameModifier &data);

	bool respondsToEvent(const Event &evt) const override;
	VThreadState consumeMessage(Runtime *runtime, const Common::SharedPtr<MessageProperties> &msg) override;
	void disable(Runtime *runtime) override;

	bool readAttribute(MiniscriptThread *thread, DynamicValue &result, const Common::String &attrib) override;
	MiniscriptInstructionOutcome writeRefAttribute(MiniscriptThread *thread, DynamicValueWriteProxy &result, const Common::String &attrib) override;

	void onKeyboardEvent(Runtime *runtime, const KeyboardInputEvent &keyEvt) override;

#ifdef MTROPOLIS_DEBUG_ENABLE
	const char *debugGetTypeName() const override { return "WhaleMaze Modifier"; }
	SupportStatus debugGetSupportStatus() const override { return kSupportStatusPartial; }
#endif

private:
	struct Node {
		int32 x;
		int32 y;
		int16 type;
	};

	struct JunctionExit {
		int16 path;
		bool forward;	// Leaves along the path from its start
	};

	struct Junction {
		int32 x;
		int32 y;
		Common::Array<JunctionExit> exits;
	};

	struct Graph {
		Common::Point start;
		Common::Array<Common::Array<Node> > paths;
		Common::Array<Junction> junctions;
	};

	struct Item {
		Common::Point pos;
		Common::Point hit;
		int32 type;
		int32 durations[3];
	};

	struct Level {
		Graph whale;
		Item food[3];
		Item hazards[6];
		Graph orcas[2];
	};

	// An actor moving along a graph
	struct Mover {
		Mover();

		int path;
		int target;		// Node index being moved toward
		int dir;		// +1 or -1 along the path
		double x;
		double y;
		bool moving;
	};

	struct Orca {
		Orca();

		Mover mover;
		bool active;
		bool visible;
		int animTicks;	// Ticks left of a surface or dive animation
		bool diving;
	};

	struct Hazard {
		Hazard();

		int state;
		int countdown;
		int frame;
	};

	enum Key {
		kKeyUp,
		kKeyDown,
		kKeyLeft,
		kKeyRight,

		kKeyCount,
	};

	struct WriteProxy {
		static MiniscriptInstructionOutcome write(MiniscriptThread *thread, const DynamicValue &value, void *objectRef, uintptr ptrOrOffset);
		static MiniscriptInstructionOutcome refAttrib(MiniscriptThread *thread, DynamicValueWriteProxy &proxy, void *objectRef, uintptr ptrOrOffset, const Common::String &attrib);
		static MiniscriptInstructionOutcome refAttribIndexed(MiniscriptThread *thread, DynamicValueWriteProxy &proxy, void *objectRef, uintptr ptrOrOffset, const Common::String &attrib, const DynamicValue &index);
	};

	enum WritableAttribute {
		kAttribPaused,
		kAttribGameLevel,
	};

	Common::SharedPtr<Modifier> shallowClone() const override;
	const char *getDefaultName() const override;

	bool loadLevels();
	void setupLevel(Runtime *runtime);
	void tick(Runtime *runtime);
	void scheduleTick(Runtime *runtime);
	void stopGame();

	void handleInput(Runtime *runtime, bool (*matches)(double candidate, double target), double target);
	bool chooseDirection(bool (*matches)(double candidate, double target), double target);
	void arriveAtNode(Runtime *runtime);
	void resetWhale(Runtime *runtime);
	void stepWhale(Runtime *runtime);
	void updateOrca(Runtime *runtime, int index, int axisStep, int diagonalStep);
	void updateHazards(Runtime *runtime);
	void checkCollisions(Runtime *runtime);

	const Node &moverNode(const Graph &graph, const Mover &mover, int index) const;
	bool moverAtNode(const Graph &graph, const Mover &mover) const;
	const Junction *findJunction(const Graph &graph, int32 x, int32 y) const;
	static double headingTo(double fromX, double fromY, double toX, double toY);
	static Common::String angleLabel(double heading);
	static bool matchKey(double candidate, double target);
	static bool matchMouse(double candidate, double target);

	Structural *child(int index) const;
	Structural *grandchild(int index, int subIndex) const;
	void placeTopLeft(Runtime *runtime, Structural *element, int32 x, int32 y);
	void placeCentered(Runtime *runtime, Structural *element, double x, double y);
	void setAttribute(Runtime *runtime, Structural *element, const char *attrib, const DynamicValue &value);
	void sendMessage(Runtime *runtime, const char *messageName);

	Event _startWhen;
	Event _stopWhen;

	Common::Array<Level> _levels;	// Index 0 is unused
	bool _levelsLoaded;

	int32 _gameLevel;
	bool _paused;
	bool _initialized;
	bool _running;
	bool _keyDown[kKeyCount];

	Mover _whale;
	bool _orcasFrozen;
	Orca _orcas[2];
	Hazard _hazards[6];
	bool _foodEaten[3];
	int _sonarCel;
	int _whaleRangeTicks;

	Runtime *_runtime;
	Common::SharedPtr<ScheduledEvent> _scheduledEvent;
	Common::SharedPtr<KeyboardEventSignaller> _keySignaller;
};

class DlgMgrClientModifier;
class KnowWonderPlugIn;

// The dialog manager server is a project-wide service. Clients (one per
// dialog line) queue themselves on it, and it plays one line at a time on a
// 12 frames per second timeline, driving the line's tracks: sound elements,
// lip sync animation and cue messages.
class DlgMgrServerModifier : public Modifier, public IPlayMediaSignalReceiver {
public:
	DlgMgrServerModifier();
	DlgMgrServerModifier(const DlgMgrServerModifier &other);
	~DlgMgrServerModifier();

	bool load(const PlugInModifierLoaderContext &context, const Data::KW::DlgMgrServerModifier &data);

	bool respondsToEvent(const Event &evt) const override;
	VThreadState consumeMessage(Runtime *runtime, const Common::SharedPtr<MessageProperties> &msg) override;

	void disable(Runtime *runtime) override;

	bool readAttribute(MiniscriptThread *thread, DynamicValue &result, const Common::String &attrib) override;
	MiniscriptInstructionOutcome writeRefAttribute(MiniscriptThread *thread, DynamicValueWriteProxy &result, const Common::String &attrib) override;

	void playMedia(Runtime *runtime, Project *project) override;

	void enqueue(DlgMgrClientModifier *client);
	void cancelClient(Runtime *runtime, DlgMgrClientModifier *client, bool notify);

#ifdef MTROPOLIS_DEBUG_ENABLE
	const char *debugGetTypeName() const override { return "DlgMgrServer Modifier"; }
	SupportStatus debugGetSupportStatus() const override { return kSupportStatusPartial; }
	void debugInspect(IDebugInspectionReport *report) const override;
#endif

private:
	struct WriteProxy {
		static MiniscriptInstructionOutcome write(MiniscriptThread *thread, const DynamicValue &value, void *objectRef, uintptr ptrOrOffset);
		static MiniscriptInstructionOutcome refAttrib(MiniscriptThread *thread, DynamicValueWriteProxy &proxy, void *objectRef, uintptr ptrOrOffset, const Common::String &attrib);
		static MiniscriptInstructionOutcome refAttribIndexed(MiniscriptThread *thread, DynamicValueWriteProxy &proxy, void *objectRef, uintptr ptrOrOffset, const Common::String &attrib, const DynamicValue &index);
	};

	enum WritableAttribute {
		kAttribCancel,
		kAttribFlush,
		kAttribSoundStopped,
		kAttribDebugOutput,
		kAttribRelink,
		kAttribCaption,
		kAttribPause,
		kAttribWaitCursor,
		kAttribRestoreDisplay,
	};

	Common::SharedPtr<Modifier> shallowClone() const override;
	const char *getDefaultName() const override;

	void writeAttribute(Runtime *runtime, WritableAttribute attrib, const DynamicValue &value);

	void startEngine(Runtime *runtime);
	void stopEngine();

	void startNextLine(Runtime *runtime);
	void updateCurrentLine(Runtime *runtime);
	void stopCurrentLine(Runtime *runtime, bool resetTracks, bool notifyDone);
	void flushQueue(Runtime *runtime, bool notifyDone);
	void setPaused(Runtime *runtime, bool paused);
	int32 getCurrentFrame(Runtime *runtime) const;

	DlgMgrClientModifier *getCurrentClient() const;

	// Closed captions, from a .ccd file of "SPEAKER\tcaption" entries keyed by dialogue line name
	struct CaptionEntry {
		uint32 textOffset;
		uint32 nameOffset;
		bool compareNames;	// More than one name hashes to this slot
	};

	void loadCaptions(Runtime *runtime);
	Common::String findCaption(const Common::String &lineName) const;
	void startCaption(Runtime *runtime, const Common::String &lineName, int32 startFrame, int32 endFrame);
	void updateCaption(Runtime *runtime, int32 frame);
	void sendCaptionPage(Runtime *runtime);
	void clearCaption(Runtime *runtime);
	void sendCaptionEvent(Runtime *runtime, const Event &evt, const DynamicValue &value);

	Event _enableWhen;
	Event _disableWhen;
	Event _captionTextEvent;
	Event _captionSpeakerEvent;
	Event _captionClearEvent;

	Common::String _captionFile;
	Common::String _registryKey;
	Common::String _titleName;

	KnowWonderPlugIn *_plugIn;
	Runtime *_runtime;
	Common::SharedPtr<PlayMediaSignaller> _playMediaSignaller;

	Common::Array<Common::WeakPtr<RuntimeObject> > _queue;
	Common::WeakPtr<RuntimeObject> _currentClient;

	bool _isRunning;
	bool _isPlaying;
	bool _skipRequested;
	bool _paused;
	bool _captions;
	bool _debugOutput;
	bool _relink;

	uint64 _startTime;
	int32 _frameOffset;	// Frames played before the last pause
	int32 _lastFrame;
	int32 _maxEndFrame;

	Common::Array<CaptionEntry> _captionEntries;
	Common::Array<char> _captionNames;
	Common::Array<char> _captionTexts;
	Common::Array<Common::String> _captionLines;	// Wrapped lines of the current caption
	uint _captionNextLine;
	int32 _captionNextPageFrame;
	int32 _captionDuration;
	uint32 _captionTotalChars;
	bool _captionShown;
};

class DlgMgrClientModifier : public Modifier {
public:
	struct Track {
		Track();

		int32 trackType;
		int32 startFrame;
		int32 endFrame;
		int32 loopStartCel;
		int32 loopEndCel;
		bool hideAtEnd;
		bool preload;
		int32 cueValue;

		uint32 targetGUID;
		uint32 sourceGUID;
		Common::Array<uint32> targetPath;	// Ancestor GUIDs, for targets outside the client's scene
		Common::Array<uint32> sourcePath;
		Common::WeakPtr<Structural> target;
		Common::WeakPtr<Structural> source;

		Common::String mediaName;
		Common::Array<int32> frames;

		// Playback state
		bool isStarted;
		bool isFinished;
		bool isSoundPlaying;
		bool isShown;
		bool assetSwapped;
	};

	DlgMgrClientModifier();
	~DlgMgrClientModifier();

	bool load(const PlugInModifierLoaderContext &context, const Data::KW::DlgMgrClientModifier &data);

	bool respondsToEvent(const Event &evt) const override;
	VThreadState consumeMessage(Runtime *runtime, const Common::SharedPtr<MessageProperties> &msg) override;

	void disable(Runtime *runtime) override;

	// Called by the server
	Common::Array<Track> &getTracks();
	void onPlaybackStarted();
	void onPlaybackFinished(Runtime *runtime, bool notifyDone);
	void sendCue(Runtime *runtime, int32 cueValue);
	void resolveTargets(Runtime *runtime);
	bool isQueued() const;

#ifdef MTROPOLIS_DEBUG_ENABLE
	const char *debugGetTypeName() const override { return "DlgMgrClient Modifier"; }
	SupportStatus debugGetSupportStatus() const override { return kSupportStatusPartial; }
	void debugInspect(IDebugInspectionReport *report) const override;
#endif

private:
	Common::SharedPtr<Modifier> shallowClone() const override;
	const char *getDefaultName() const override;

	void linkInternalReferences(ObjectLinkingScope *scope) override;
	void visitInternalReferences(IStructuralReferenceVisitor *visitor) override;

	DlgMgrServerModifier *findServer() const;

	Event _playWhen;
	Event _stopWhen;
	MessengerSendSpec _cueSend;
	MessengerSendSpec _doneSend;
	bool _hasCue;
	bool _hasDone;

	int32 _triggerValue;
	bool _filterByTriggerValue;

	Common::Array<Track> _tracks;

	KnowWonderPlugIn *_plugIn;
	bool _isQueued;
};

class KnowWonderPlugIn : public MTropolis::PlugIn {
public:
	KnowWonderPlugIn();
	~KnowWonderPlugIn();

	void registerModifiers(IPlugInModifierRegistrar *registrar) const override;

	void setDlgMgrServer(const Common::WeakPtr<RuntimeObject> &server);
	DlgMgrServerModifier *getDlgMgrServer() const;

private:
	PlugInModifierFactory<StrUtilModifier, Data::KW::StrUtilModifier> _strUtilModifierFactory;
	PlugInModifierFactory<DlgMgrServerModifier, Data::KW::DlgMgrServerModifier> _dlgMgrServerModifierFactory;
	PlugInModifierFactory<DlgMgrClientModifier, Data::KW::DlgMgrClientModifier> _dlgMgrClientModifierFactory;
	PlugInModifierFactory<KWSmackerModifier, Data::KW::KWSmackerModifier> _kwSmackerModifierFactory;
	PlugInModifierFactory<EditCtrlModifier, Data::KW::EditCtrlModifier> _editCtrlModifierFactory;
	PlugInModifierFactory<GameModifier, Data::KW::GameModifier> _gameModifierFactory;
	PlugInModifierFactory<WhaleMazeModifier, Data::KW::GameModifier> _whaleMazeModifierFactory;

	Common::WeakPtr<RuntimeObject> _dlgMgrServer;
};

} // End of namespace KW

} // End of namespace MTropolis

#endif
