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

class EditCtrlModifier : public Modifier {
public:
	bool load(const PlugInModifierLoaderContext &context, const Data::KW::EditCtrlModifier &data);

	bool respondsToEvent(const Event &evt) const override;
	VThreadState consumeMessage(Runtime *runtime, const Common::SharedPtr<MessageProperties> &msg) override;

	void disable(Runtime *runtime) override {}

#ifdef MTROPOLIS_DEBUG_ENABLE
	const char *debugGetTypeName() const override { return "EditCtrl Modifier"; }
	SupportStatus debugGetSupportStatus() const override { return kSupportStatusNone; }
#endif

private:
	Common::SharedPtr<Modifier> shallowClone() const override;
	const char *getDefaultName() const override;

	Event _event1;
	Event _event2;
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

	Common::WeakPtr<RuntimeObject> _dlgMgrServer;
};

} // End of namespace KW

} // End of namespace MTropolis

#endif
