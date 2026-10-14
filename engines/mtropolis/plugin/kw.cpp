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

#include "mtropolis/plugin/kw.h"
#include "mtropolis/plugins.h"
#include "mtropolis/miniscript.h"

#include "graphics/managed_surface.h"
#include "graphics/fontman.h"
#include "graphics/font.h"

#include "video/smk_decoder.h"

namespace MTropolis {

namespace KW {

StrUtilModifier::StrUtilModifier() {
}

StrUtilModifier::~StrUtilModifier() {
}

bool StrUtilModifier::load(const PlugInModifierLoaderContext &context, const Data::KW::StrUtilModifier &data) {
	return true;
}

bool StrUtilModifier::respondsToEvent(const Event &evt) const {
	return false;
}

VThreadState StrUtilModifier::consumeMessage(Runtime *runtime, const Common::SharedPtr<MessageProperties> &msg) {
	return kVThreadReturn;
}

void StrUtilModifier::disable(Runtime *runtime) {
}

#ifdef MTROPOLIS_DEBUG_ENABLE
void StrUtilModifier::debugInspect(IDebugInspectionReport *report) const {
	Modifier::debugInspect(report);
}
#endif

Common::SharedPtr<Modifier> StrUtilModifier::shallowClone() const {
	return Common::SharedPtr<Modifier>(new StrUtilModifier(*this));
}

const char *StrUtilModifier::getDefaultName() const {
	return "StrUtil Modifier"; // ???
}

static bool loadDlgMgrEvent(Event &evt, const Data::PlugInTypeTaggedValue &value) {
	if (value.type == Data::PlugInTypeTaggedValue::kNull)
		return true;

	if (value.type != Data::PlugInTypeTaggedValue::kEvent)
		return false;

	return evt.load(value.value.asEvent);
}

static bool loadDlgMgrEvent(Event &evt, const Data::KW::DlgMgrValue &value) {
	if (value.type == Data::PlugInTypeTaggedValue::kNull)
		return true;

	if (value.type != Data::PlugInTypeTaggedValue::kEvent)
		return false;

	Data::Event dataEvent;
	dataEvent.eventID = static_cast<uint32>(value.value1);
	dataEvent.eventInfo = static_cast<uint32>(value.value2);
	return evt.load(dataEvent);
}

static Common::String dlgMgrString(const Data::PlugInTypeTaggedValue &value) {
	if (value.type != Data::PlugInTypeTaggedValue::kString)
		return Common::String();

	return value.value.asString;
}

KWSmackerModifier::KWSmackerModifier() : _changePalette(false), _runtime(nullptr), _frame(nullptr), _isPlaying(false) {
}

KWSmackerModifier::KWSmackerModifier(const KWSmackerModifier &other)
	: Modifier(other), _playWhen(other._playWhen), _stopWhen(other._stopWhen), _sendWhenDone(other._sendWhenDone),
	  _moviePath(other._moviePath), _changePalette(other._changePalette), _runtime(nullptr), _frame(nullptr), _isPlaying(false) {
}

KWSmackerModifier::~KWSmackerModifier() {
	stopPlaying();
}

bool KWSmackerModifier::load(const PlugInModifierLoaderContext &context, const Data::KW::KWSmackerModifier &data) {
	if (!loadDlgMgrEvent(_playWhen, data.playWhen) || !loadDlgMgrEvent(_stopWhen, data.stopWhen))
		return false;

	// The destination is stored as a label value, with the destination in the label ID
	if (data.unknown2Label.type != Data::PlugInTypeTaggedValue::kLabel)
		return false;

	Data::PlugInTypeTaggedValue with;
	with.type = Data::PlugInTypeTaggedValue::kNull;

	if (!_sendWhenDone.load(data.unknown1Event, MessageFlags(), with, data.unknown2Label.value.asLabel.labelID))
		return false;

	_moviePath = dlgMgrString(data.filePath);
	_changePalette = (data.unknown3.type == Data::PlugInTypeTaggedValue::kBoolean && data.unknown3.value.asBoolean != 0);

	return true;
}

bool KWSmackerModifier::respondsToEvent(const Event &evt) const {
	return _playWhen.respondsTo(evt) || _stopWhen.respondsTo(evt);
}

VThreadState KWSmackerModifier::consumeMessage(Runtime *runtime, const Common::SharedPtr<MessageProperties> &msg) {
	if (_playWhen.respondsTo(msg->getEvent())) {
		if (!_isPlaying)
			startPlaying(runtime);
	} else if (_stopWhen.respondsTo(msg->getEvent())) {
		if (_isPlaying)
			finishPlaying(runtime);
	}

	return kVThreadReturn;
}

void KWSmackerModifier::disable(Runtime *runtime) {
	stopPlaying();
}

bool KWSmackerModifier::readAttribute(MiniscriptThread *thread, DynamicValue &result, const Common::String &attrib) {
	if (attrib == "moviepath") {
		result.setString(_moviePath);
		return true;
	}
	if (attrib == "changepal") {
		result.setBool(_changePalette);
		return true;
	}

	return Modifier::readAttribute(thread, result, attrib);
}

MiniscriptInstructionOutcome KWSmackerModifier::writeRefAttribute(MiniscriptThread *thread, DynamicValueWriteProxy &result, const Common::String &attrib) {
	if (attrib == "moviepath") {
		DynamicValueWriteStringHelper::create(&_moviePath, result);
		return kMiniscriptInstructionOutcomeContinue;
	}
	if (attrib == "changepal") {
		DynamicValueWriteBoolHelper::create(&_changePalette, result);
		return kMiniscriptInstructionOutcomeContinue;
	}

	return Modifier::writeRefAttribute(thread, result, attrib);
}

void KWSmackerModifier::startPlaying(Runtime *runtime) {
	stopPlaying();

	Common::SeekableReadStream *stream = runtime->getProject()->createReadStreamForProjectPath(_moviePath);
	if (!stream) {
		warning("KWSmacker: Couldn't open movie '%s'", _moviePath.c_str());
		finishPlaying(runtime);
		return;
	}

	Common::SharedPtr<Video::SmackerDecoder> decoder(new Video::SmackerDecoder());
	if (!decoder->loadStream(stream)) {
		warning("KWSmacker: Couldn't load movie '%s'", _moviePath.c_str());
		finishPlaying(runtime);
		return;
	}

	Common::SharedPtr<Window> mainWindow = runtime->getMainWindow().lock();
	if (mainWindow)
		decoder->setOutputPixelFormat(mainWindow->getSurface()->format);

	decoder->start();

	_runtime = runtime;
	_decoder = decoder;
	_frame = nullptr;
	_isPlaying = true;

	runtime->addPostEffect(this);
	_playMediaSignaller = runtime->getProject()->notifyOnPlayMedia(this);
}

void KWSmackerModifier::stopPlaying() {
	if (!_isPlaying)
		return;

	_isPlaying = false;
	_frame = nullptr;

	if (_playMediaSignaller) {
		_playMediaSignaller->removeReceiver(this);
		_playMediaSignaller.reset();
	}

	if (_runtime) {
		_runtime->removePostEffect(this);
		_runtime->setSceneGraphDirty();
	}

	_decoder.reset();
}

void KWSmackerModifier::finishPlaying(Runtime *runtime) {
	stopPlaying();
	_sendWhenDone.sendFromMessenger(runtime, this, nullptr, DynamicValue(), nullptr);
}

void KWSmackerModifier::playMedia(Runtime *runtime, Project *project) {
	if (!_isPlaying)
		return;

	while (_decoder->needsUpdate()) {
		const Graphics::Surface *frame = _decoder->decodeNextFrame();
		if (!frame)
			break;

		Common::SharedPtr<Window> mainWindow = runtime->getMainWindow().lock();
		if (!mainWindow)
			break;

		const Graphics::PixelFormat &destFormat = mainWindow->getSurface()->format;
		if (frame->format == destFormat || !frame->format.isCLUT8())
			_frameSurface.copyFrom(*frame);
		else {
			Graphics::Surface *converted = frame->convertTo(destFormat, _decoder->getPalette());
			_frameSurface.copyFrom(*converted);
			converted->free();
			delete converted;
		}

		_frame = &_frameSurface.rawSurface();
		runtime->setSceneGraphDirty();
	}

	if (_decoder->endOfVideo())
		finishPlaying(runtime);
}

void KWSmackerModifier::renderPostEffect(Graphics::ManagedSurface &surface) const {
	if (!_frame)
		return;

	Common::Rect srcRect(0, 0, _frame->w, _frame->h);
	Common::Rect destRect(0, 0, _frame->w, _frame->h);
	destRect.clip(Common::Rect(0, 0, surface.w, surface.h));
	srcRect.setWidth(destRect.width());
	srcRect.setHeight(destRect.height());

	surface.blitFrom(*_frame, srcRect, destRect);
}

#ifdef MTROPOLIS_DEBUG_ENABLE
void KWSmackerModifier::debugInspect(IDebugInspectionReport *report) const {
	Modifier::debugInspect(report);

	report->declareDynamic("moviePath", _moviePath);
	report->declareDynamic("isPlaying", _isPlaying ? "true" : "false");
}
#endif

Common::SharedPtr<Modifier> KWSmackerModifier::shallowClone() const {
	return Common::SharedPtr<Modifier>(new KWSmackerModifier(*this));
}

const char *KWSmackerModifier::getDefaultName() const {
	return "KWSmacker";
}

void KWSmackerModifier::linkInternalReferences(ObjectLinkingScope *scope) {
	Modifier::linkInternalReferences(scope);
	_sendWhenDone.linkInternalReferences(scope);
}

void KWSmackerModifier::visitInternalReferences(IStructuralReferenceVisitor *visitor) {
	Modifier::visitInternalReferences(visitor);
	_sendWhenDone.visitInternalReferences(visitor);
}

EditCtrlModifier::EditCtrlModifier() : _textLimit(16), _runtime(nullptr), _isShown(false) {
}

EditCtrlModifier::EditCtrlModifier(const EditCtrlModifier &other)
	: Modifier(other), _text(other._text), _textLimit(other._textLimit), _runtime(nullptr), _isShown(false) {
}

EditCtrlModifier::~EditCtrlModifier() {
	hide();
}

bool EditCtrlModifier::load(const PlugInModifierLoaderContext &context, const Data::KW::EditCtrlModifier &data) {
	return true;
}

bool EditCtrlModifier::respondsToEvent(const Event &evt) const {
	return false;
}

VThreadState EditCtrlModifier::consumeMessage(Runtime *runtime, const Common::SharedPtr<MessageProperties> &msg) {
	return kVThreadReturn;
}

void EditCtrlModifier::disable(Runtime *runtime) {
	hide();
}

void EditCtrlModifier::show(Runtime *runtime) {
	if (_isShown)
		return;

	_runtime = runtime;
	_isShown = true;
	runtime->addPostEffect(this);
	_keySignaller = runtime->getProject()->notifyOnKeyboardEvent(this);
	runtime->setSceneGraphDirty();
}

void EditCtrlModifier::hide() {
	if (!_isShown)
		return;

	_isShown = false;
	if (_keySignaller) {
		_keySignaller->removeReceiver(this);
		_keySignaller.reset();
	}
	if (_runtime) {
		_runtime->removePostEffect(this);
		_runtime->setSceneGraphDirty();
	}
}

void EditCtrlModifier::sendNamedMessage(Runtime *runtime, const char *messageName) {
	uint32 messageID = 0;
	if (!runtime->getProject()->findAuthorMessageID(messageName, messageID)) {
		warning("EditCtrl: No author message named '%s'", messageName);
		return;
	}

	Structural *owner = findStructuralOwner();
	if (!owner)
		return;

	DynamicValue data;
	data.setInt(0);

	Common::SharedPtr<MessageProperties> props(new MessageProperties(Event(EventIDs::kAuthorMessage, messageID), data, getSelfReference()));
	Common::SharedPtr<MessageDispatch> dispatch(new MessageDispatch(props, owner, true, true, false));
	runtime->queueMessage(dispatch);
}

void EditCtrlModifier::onKeyboardEvent(Runtime *runtime, const KeyboardInputEvent &keyEvt) {
	if (keyEvt.getKeyEventType() != Common::EVENT_KEYDOWN)
		return;

	const Common::KeyState &key = keyEvt.getKeyState();

	switch (key.keycode) {
	case Common::KEYCODE_RETURN:
	case Common::KEYCODE_KP_ENTER:
		sendNamedMessage(runtime, "enterpressed");
		return;
	case Common::KEYCODE_ESCAPE:
		sendNamedMessage(runtime, "canceldialog");
		return;
	case Common::KEYCODE_BACKSPACE:
		if (!_text.empty())
			_text.deleteLastChar();
		break;
	default:
		if (key.keycode == Common::KEYCODE_a && (key.flags & Common::KBD_SHIFT) && (key.flags & Common::KBD_ALT)) {
			sendNamedMessage(runtime, "aboutboxlaunch");
			return;
		}
		if (key.ascii >= 32 && key.ascii < 127 && _text.size() < _textLimit)
			_text += static_cast<char>(key.ascii);
		else
			return;
		break;
	}

	runtime->setSceneGraphDirty();
}

void EditCtrlModifier::renderPostEffect(Graphics::ManagedSurface &surface) const {
	Structural *owner = findStructuralOwner();
	if (!owner || !owner->isElement() || !static_cast<Element *>(owner)->isVisual())
		return;

	const VisualElement *element = static_cast<const VisualElement *>(owner);
	const Common::Point &origin = element->getCachedAbsoluteOrigin();
	const Common::Rect &relRect = element->getRelativeRect();

	// The original places the edit window one pixel below the element
	Common::Rect rect(origin.x, origin.y + 1, origin.x + relRect.width(), origin.y + 1 + relRect.height());
	rect.clip(Common::Rect(0, 0, surface.w, surface.h));
	if (rect.isEmpty())
		return;

	uint32 white, black;
	if (surface.format.isCLUT8()) {
		const byte *pal = _runtime->getGlobalPalette().getPalette();
		uint bestWhite = 0, bestBlack = 0;
		int bestWhiteDist = INT_MAX, bestBlackDist = INT_MAX;
		for (uint i = 0; i < 256; i++) {
			int sum = pal[i * 3] + pal[i * 3 + 1] + pal[i * 3 + 2];
			if (765 - sum < bestWhiteDist) {
				bestWhiteDist = 765 - sum;
				bestWhite = i;
			}
			if (sum < bestBlackDist) {
				bestBlackDist = sum;
				bestBlack = i;
			}
		}
		white = bestWhite;
		black = bestBlack;
	} else {
		white = surface.format.RGBToColor(255, 255, 255);
		black = surface.format.RGBToColor(0, 0, 0);
	}

	surface.fillRect(rect, white);

	const Graphics::Font *font = FontMan.getFontByUsage(Graphics::FontManager::kBigGUIFont);
	int y = rect.top + (rect.height() - font->getFontHeight()) / 2;
	font->drawString(&surface, _text, rect.left + 2, y, rect.width() - 4, black);

	// Caret
	if ((_runtime->getRealTime() / 500) % 2 == 0) {
		int caretX = rect.left + 2 + font->getStringWidth(_text);
		if (caretX < rect.right)
			surface.vLine(caretX, y, y + font->getFontHeight() - 1, black);
	}
}

bool EditCtrlModifier::readAttribute(MiniscriptThread *thread, DynamicValue &result, const Common::String &attrib) {
	if (attrib == "text") {
		result.setString(_text);
		return true;
	}
	if (attrib == "length") {
		result.setInt(_text.size());
		return true;
	}
	if (attrib == "textlimit") {
		result.setInt(_textLimit);
		return true;
	}

	return Modifier::readAttribute(thread, result, attrib);
}

MiniscriptInstructionOutcome EditCtrlModifier::writeRefAttribute(MiniscriptThread *thread, DynamicValueWriteProxy &result, const Common::String &attrib) {
	static const struct {
		const char *name;
		WritableAttribute attrib;
	} kAttribs[] = {
		{"text", kAttribText},
		{"textlimit", kAttribTextLimit},
		{"update", kAttribUpdate},
		{"enable", kAttribEnable},
		{"disable", kAttribDisable},
		{"setfocus", kAttribSetFocus},
	};

	for (const auto &entry : kAttribs) {
		if (attrib == entry.name) {
			result.pod.ifc = DynamicValueWriteInterfaceGlue<WriteProxy>::getInstance();
			result.pod.objectRef = this;
			result.pod.ptrOrOffset = entry.attrib;
			return kMiniscriptInstructionOutcomeContinue;
		}
	}

	return Modifier::writeRefAttribute(thread, result, attrib);
}

MiniscriptInstructionOutcome EditCtrlModifier::WriteProxy::write(MiniscriptThread *thread, const DynamicValue &value, void *objectRef, uintptr ptrOrOffset) {
	static_cast<EditCtrlModifier *>(objectRef)->writeAttribute(thread->getRuntime(), static_cast<WritableAttribute>(ptrOrOffset), value);
	return kMiniscriptInstructionOutcomeContinue;
}

MiniscriptInstructionOutcome EditCtrlModifier::WriteProxy::refAttrib(MiniscriptThread *thread, DynamicValueWriteProxy &proxy, void *objectRef, uintptr ptrOrOffset, const Common::String &attrib) {
	return kMiniscriptInstructionOutcomeFailed;
}

MiniscriptInstructionOutcome EditCtrlModifier::WriteProxy::refAttribIndexed(MiniscriptThread *thread, DynamicValueWriteProxy &proxy, void *objectRef, uintptr ptrOrOffset, const Common::String &attrib, const DynamicValue &index) {
	return kMiniscriptInstructionOutcomeFailed;
}

void EditCtrlModifier::writeAttribute(Runtime *runtime, WritableAttribute attrib, const DynamicValue &value) {
	switch (attrib) {
	case kAttribText: {
			DynamicValue converted;
			if (value.getType() == DynamicValueTypes::kString)
				_text = value.getString();
			else if (value.convertToType(DynamicValueTypes::kString, converted))
				_text = converted.getString();
			if (_text.size() > _textLimit)
				_text = _text.substr(0, _textLimit);
			runtime->setSceneGraphDirty();
		} break;
	case kAttribTextLimit: {
			int32 limit = 0;
			if (value.roundToInt(limit) && limit > 0)
				_textLimit = limit;
		} break;
	case kAttribEnable:
		show(runtime);
		break;
	case kAttribDisable:
		hide();
		break;
	case kAttribUpdate:
	case kAttribSetFocus:
		runtime->setSceneGraphDirty();
		break;
	}
}

#ifdef MTROPOLIS_DEBUG_ENABLE
void EditCtrlModifier::debugInspect(IDebugInspectionReport *report) const {
	Modifier::debugInspect(report);

	report->declareDynamic("text", _text);
	report->declareDynamic("shown", _isShown ? "true" : "false");
}
#endif

Common::SharedPtr<Modifier> EditCtrlModifier::shallowClone() const {
	return Common::SharedPtr<Modifier>(new EditCtrlModifier(*this));
}

const char *EditCtrlModifier::getDefaultName() const {
	return "EditCtrl";
}

bool GameModifier::load(const PlugInModifierLoaderContext &context, const Data::KW::GameModifier &data) {
	if (!loadDlgMgrEvent(_event1, data.enableWhen) || !loadDlgMgrEvent(_event2, data.disableWhen))
		return false;

	return true;
}

bool GameModifier::respondsToEvent(const Event &evt) const {
	return _event1.respondsTo(evt) || _event2.respondsTo(evt);
}

VThreadState GameModifier::consumeMessage(Runtime *runtime, const Common::SharedPtr<MessageProperties> &msg) {
	warning("Game-specific modifier '%s' is not implemented", getName().c_str());
	return kVThreadReturn;
}

Common::SharedPtr<Modifier> GameModifier::shallowClone() const {
	return Common::SharedPtr<Modifier>(new GameModifier(*this));
}

const char *GameModifier::getDefaultName() const {
	return "Game-specific";
}

static bool loadDlgMgrSend(MessengerSendSpec &send, const Data::KW::DlgMgrValue &evt, const Data::KW::DlgMgrValue &dest) {
	// An event ID of 1 is used for "no message"
	if (evt.type != Data::PlugInTypeTaggedValue::kEvent || evt.value1 == 1)
		return false;

	Data::Event dataEvent;
	dataEvent.eventID = static_cast<uint32>(evt.value1);
	dataEvent.eventInfo = static_cast<uint32>(evt.value2);
	if (!send.send.load(dataEvent))
		return false;

	send.destination = (dest.type == Data::PlugInTypeTaggedValue::kLabel) ? static_cast<uint32>(dest.value1) : 0;
	return true;
}

static void dlgMgrSendCommand(Runtime *runtime, Modifier *sender, RuntimeObject *target, EventIDs::EventID eventID) {
	if (!target || !target->isStructural())
		return;

	debug(2, "DlgMgr: Command %u to '%s' (%s)", static_cast<uint>(eventID), static_cast<Structural *>(target)->getName().c_str(),
		static_cast<Structural *>(target)->debugGetTypeName());

	Common::SharedPtr<MessageProperties> props(new MessageProperties(Event(eventID, 0), DynamicValue(), sender->getSelfReference()));
	Common::SharedPtr<MessageDispatch> dispatch(new MessageDispatch(props, static_cast<Structural *>(target), false, true, true));
	runtime->queueMessage(dispatch);
}

static bool dlgMgrReadAttribute(Runtime *runtime, Modifier *sender, RuntimeObject *target, const char *attrib, DynamicValue &result) {
	if (!target)
		return false;

	MiniscriptThread thread(runtime, nullptr, nullptr, nullptr, sender);
	return target->readAttribute(&thread, result, attrib);
}

static void dlgMgrWriteAttribute(Runtime *runtime, Modifier *sender, RuntimeObject *target, const char *attrib, const DynamicValue &value) {
	if (!target)
		return;

	MiniscriptThread thread(runtime, nullptr, nullptr, nullptr, sender);
	DynamicValueWriteProxy proxy;
	if (target->writeRefAttribute(&thread, proxy, attrib) == kMiniscriptInstructionOutcomeContinue)
		(void)proxy.pod.ifc->write(&thread, value, proxy.pod.objectRef, proxy.pod.ptrOrOffset);
}

static void dlgMgrSetBool(Runtime *runtime, Modifier *sender, RuntimeObject *target, const char *attrib, bool value) {
	DynamicValue v;
	v.setBool(value);
	dlgMgrWriteAttribute(runtime, sender, target, attrib, v);
}

static void dlgMgrSetInt(Runtime *runtime, Modifier *sender, RuntimeObject *target, const char *attrib, int32 value) {
	DynamicValue v;
	v.setInt(value);
	dlgMgrWriteAttribute(runtime, sender, target, attrib, v);
}

// Cel for an animation track at a timeline frame, or -1 to leave it alone
static int32 dlgMgrCelForFrame(const DlgMgrClientModifier::Track &track, int32 frame) {
	if (track.frames.size() > 0) {
		if (frame < track.startFrame || frame > track.endFrame)
			return 1;

		int32 index = frame - track.startFrame;
		if (index < static_cast<int32>(track.frames.size()))
			return track.frames[index];

		return -1;
	}

	if (track.loopStartCel <= 0 || track.loopEndCel < track.loopStartCel)
		return -1;

	int32 rangeSize = track.loopEndCel - track.loopStartCel + 1;
	int32 pos = (frame - track.startFrame + 1) % rangeSize;
	if (pos == 0)
		return track.loopEndCel;

	return track.loopStartCel - 1 + pos;
}

DlgMgrServerModifier::DlgMgrServerModifier()
	: _plugIn(nullptr), _runtime(nullptr), _isRunning(false), _isPlaying(false), _skipRequested(false), _paused(false),
	  _captions(false), _debugOutput(false), _relink(false), _startTime(0), _frameOffset(0), _lastFrame(-1), _maxEndFrame(0) {
}

DlgMgrServerModifier::DlgMgrServerModifier(const DlgMgrServerModifier &other)
	: Modifier(other), _enableWhen(other._enableWhen), _disableWhen(other._disableWhen), _captionTextEvent(other._captionTextEvent),
	  _captionSpeakerEvent(other._captionSpeakerEvent), _captionClearEvent(other._captionClearEvent), _captionFile(other._captionFile),
	  _registryKey(other._registryKey), _titleName(other._titleName), _plugIn(other._plugIn), _runtime(nullptr), _isRunning(false),
	  _isPlaying(false), _skipRequested(false), _paused(false), _captions(other._captions), _debugOutput(false), _relink(false),
	  _startTime(0), _frameOffset(0), _lastFrame(-1), _maxEndFrame(0) {
}

DlgMgrServerModifier::~DlgMgrServerModifier() {
	stopEngine();
}

bool DlgMgrServerModifier::load(const PlugInModifierLoaderContext &context, const Data::KW::DlgMgrServerModifier &data) {
	if (!loadDlgMgrEvent(_enableWhen, data.enableWhen) || !loadDlgMgrEvent(_disableWhen, data.disableWhen)
		|| !loadDlgMgrEvent(_captionTextEvent, data.captionTextEvent) || !loadDlgMgrEvent(_captionSpeakerEvent, data.captionSpeakerEvent)
		|| !loadDlgMgrEvent(_captionClearEvent, data.captionClearEvent))
		return false;

	_captionFile = dlgMgrString(data.captionFile);
	_registryKey = dlgMgrString(data.registryKey);
	_plugIn = static_cast<KnowWonderPlugIn *>(context.plugIn);

	return true;
}

bool DlgMgrServerModifier::respondsToEvent(const Event &evt) const {
	return _enableWhen.respondsTo(evt) || _disableWhen.respondsTo(evt) || evt.eventType == EventIDs::kSceneEnded
		|| evt.eventType == EventIDs::kStop || evt.eventType == EventIDs::kParentDisabled;
}

VThreadState DlgMgrServerModifier::consumeMessage(Runtime *runtime, const Common::SharedPtr<MessageProperties> &msg) {
	const Event &evt = msg->getEvent();

	if (_enableWhen.respondsTo(evt)) {
		startEngine(runtime);
	} else if (evt.eventType == EventIDs::kSceneEnded) {
		// Scene changes stop the current line and drop everything queued
		stopCurrentLine(runtime, false, false);
		flushQueue(runtime, true);
	} else if (evt.eventType == EventIDs::kStop) {
		if (_isPlaying)
			_skipRequested = true;
	} else if (_disableWhen.respondsTo(evt) || evt.eventType == EventIDs::kParentDisabled) {
		stopCurrentLine(runtime, false, false);
		flushQueue(runtime, true);
		stopEngine();
	}

	return kVThreadReturn;
}

void DlgMgrServerModifier::disable(Runtime *runtime) {
	stopEngine();
}

bool DlgMgrServerModifier::readAttribute(MiniscriptThread *thread, DynamicValue &result, const Common::String &attrib) {
	if (attrib == "playing") {
		result.setBool(_isPlaying || _queue.size() > 0);
		return true;
	}
	if (attrib == "logpixelsx" || attrib == "logpixelsy") {
		result.setInt(96);
		return true;
	}
	if (attrib == "osver") {
		result.setInt(0x5f);
		return true;
	}
	if (attrib == "windows") {
		result.setBool(true);
		return true;
	}
	if (attrib == "debugoutput") {
		result.setBool(_debugOutput);
		return true;
	}
	if (attrib == "relink") {
		result.setBool(_relink);
		return true;
	}
	if (attrib == "caption") {
		result.setBool(_captions);
		return true;
	}
	if (attrib == "pause") {
		result.setBool(_paused);
		return true;
	}
	if (attrib == "titlename") {
		result.setString(_titleName);
		return true;
	}
	if (attrib == "titlepath" || attrib == "installpath" || attrib == "sourcepath") {
		// The original reads these from the registry. Paths are resolved
		// relative to the project, so an empty path is used.
		result.setString("");
		return true;
	}
	if (attrib == "ccdefaultenabled") {
		result.setBool(false);
		return true;
	}

	return Modifier::readAttribute(thread, result, attrib);
}

MiniscriptInstructionOutcome DlgMgrServerModifier::writeRefAttribute(MiniscriptThread *thread, DynamicValueWriteProxy &result, const Common::String &attrib) {
	static const struct {
		const char *name;
		WritableAttribute attrib;
	} kAttribs[] = {
		{"cancel", kAttribCancel},
		{"flush", kAttribFlush},
		{"soundstopped", kAttribSoundStopped},
		{"debugoutput", kAttribDebugOutput},
		{"relink", kAttribRelink},
		{"caption", kAttribCaption},
		{"pause", kAttribPause},
		{"waitcursor", kAttribWaitCursor},
		{"restoredisplay", kAttribRestoreDisplay},
	};

	for (const auto &entry : kAttribs) {
		if (attrib == entry.name) {
			result.pod.ifc = DynamicValueWriteInterfaceGlue<WriteProxy>::getInstance();
			result.pod.objectRef = this;
			result.pod.ptrOrOffset = entry.attrib;
			return kMiniscriptInstructionOutcomeContinue;
		}
	}

	if (attrib == "titlename") {
		DynamicValueWriteStringHelper::create(&_titleName, result);
		return kMiniscriptInstructionOutcomeContinue;
	}

	return Modifier::writeRefAttribute(thread, result, attrib);
}

MiniscriptInstructionOutcome DlgMgrServerModifier::WriteProxy::write(MiniscriptThread *thread, const DynamicValue &value, void *objectRef, uintptr ptrOrOffset) {
	static_cast<DlgMgrServerModifier *>(objectRef)->writeAttribute(thread->getRuntime(), static_cast<WritableAttribute>(ptrOrOffset), value);
	return kMiniscriptInstructionOutcomeContinue;
}

MiniscriptInstructionOutcome DlgMgrServerModifier::WriteProxy::refAttrib(MiniscriptThread *thread, DynamicValueWriteProxy &proxy, void *objectRef, uintptr ptrOrOffset, const Common::String &attrib) {
	return kMiniscriptInstructionOutcomeFailed;
}

MiniscriptInstructionOutcome DlgMgrServerModifier::WriteProxy::refAttribIndexed(MiniscriptThread *thread, DynamicValueWriteProxy &proxy, void *objectRef, uintptr ptrOrOffset, const Common::String &attrib, const DynamicValue &index) {
	return kMiniscriptInstructionOutcomeFailed;
}

void DlgMgrServerModifier::writeAttribute(Runtime *runtime, WritableAttribute attrib, const DynamicValue &value) {
	bool flag = false;
	if (value.getType() == DynamicValueTypes::kBoolean)
		flag = value.getBool();
	else if (value.getType() == DynamicValueTypes::kInteger)
		flag = (value.getInt() != 0);

	switch (attrib) {
	case kAttribCancel:
		if (flag)
			stopCurrentLine(runtime, true, true);
		break;
	case kAttribFlush:
		if (flag) {
			stopCurrentLine(runtime, false, true);
			flushQueue(runtime, true);
		}
		break;
	case kAttribDebugOutput:
		_debugOutput = flag;
		break;
	case kAttribRelink:
		_relink = flag;
		break;
	case kAttribCaption:
		_captions = flag;
		break;
	case kAttribPause:
		setPaused(runtime, flag);
		break;
	case kAttribSoundStopped:
	case kAttribWaitCursor:
	case kAttribRestoreDisplay:
		break;
	}
}

void DlgMgrServerModifier::playMedia(Runtime *runtime, Project *project) {
	if (_skipRequested) {
		_skipRequested = false;
		stopCurrentLine(runtime, true, true);
	}

	if (!_isPlaying) {
		if (!_paused)
			startNextLine(runtime);
	} else if (!_paused)
		updateCurrentLine(runtime);
}

void DlgMgrServerModifier::enqueue(DlgMgrClientModifier *client) {
	_queue.push_back(client->getSelfReference());
}

void DlgMgrServerModifier::cancelClient(Runtime *runtime, DlgMgrClientModifier *client, bool notify) {
	if (getCurrentClient() == client) {
		stopCurrentLine(runtime, true, notify);
		return;
	}

	for (uint i = 0; i < _queue.size(); i++) {
		if (_queue[i].lock().get() == client) {
			_queue.remove_at(i);
			client->onPlaybackFinished(runtime, notify);
			return;
		}
	}
}

void DlgMgrServerModifier::startEngine(Runtime *runtime) {
	if (_isRunning)
		return;

	_runtime = runtime;
	_isRunning = true;
	_playMediaSignaller = runtime->getProject()->notifyOnPlayMedia(this);

	if (_plugIn)
		_plugIn->setDlgMgrServer(getSelfReference());
}

void DlgMgrServerModifier::stopEngine() {
	if (!_isRunning)
		return;

	_isRunning = false;
	_isPlaying = false;
	_queue.clear();
	_currentClient.reset();

	if (_playMediaSignaller) {
		_playMediaSignaller->removeReceiver(this);
		_playMediaSignaller.reset();
	}
}

DlgMgrClientModifier *DlgMgrServerModifier::getCurrentClient() const {
	Common::SharedPtr<RuntimeObject> client = _currentClient.lock();
	return client ? static_cast<DlgMgrClientModifier *>(client.get()) : nullptr;
}

int32 DlgMgrServerModifier::getCurrentFrame(Runtime *runtime) const {
	// The original plug-in runs its timeline at 12 frames per second
	uint64 elapsed = runtime->getPlayTime() - _startTime;
	return _frameOffset + 1 + static_cast<int32>(elapsed * 12 / 1000);
}

void DlgMgrServerModifier::startNextLine(Runtime *runtime) {
	while (_queue.size() > 0) {
		Common::SharedPtr<RuntimeObject> clientObj = _queue[0].lock();
		_queue.remove_at(0);

		if (!clientObj)
			continue;

		DlgMgrClientModifier *client = static_cast<DlgMgrClientModifier *>(clientObj.get());

		_currentClient = clientObj;
		_isPlaying = true;
		_startTime = runtime->getPlayTime();
		_frameOffset = 0;
		_lastFrame = -1;
		_maxEndFrame = 0;

		client->resolveTargets(runtime);

		for (DlgMgrClientModifier::Track &track : client->getTracks()) {
			track.isStarted = false;
			track.isFinished = false;
			track.isSoundPlaying = false;
			track.isShown = false;
			track.assetSwapped = false;

			if (track.endFrame > _maxEndFrame)
				_maxEndFrame = track.endFrame;

			Common::SharedPtr<Structural> target = track.target.lock();
			if (track.preload && target)
				dlgMgrSendCommand(runtime, this, target.get(), EventIDs::kPreloadMedia);
		}

		client->onPlaybackStarted();

		debug(1, "DlgMgr: Playing '%s' (%u tracks, %i frames)", client->getName().c_str(), client->getTracks().size(), _maxEndFrame);

		return;
	}
}

void DlgMgrServerModifier::updateCurrentLine(Runtime *runtime) {
	DlgMgrClientModifier *client = getCurrentClient();
	if (!client) {
		_isPlaying = false;
		return;
	}

	int32 frame = getCurrentFrame(runtime);
	if (frame <= _lastFrame)
		return;

	_lastFrame = frame;

	for (DlgMgrClientModifier::Track &track : client->getTracks()) {
		if (track.isFinished)
			continue;

		Common::SharedPtr<Structural> target = track.target.lock();

		if (track.trackType == Data::KW::DlgMgrTrack::kTrackTypeCue) {
			if (frame >= track.startFrame) {
				track.isFinished = true;
				client->sendCue(runtime, track.cueValue);
			}
			continue;
		}

		if (frame < track.startFrame)
			continue;

		if (!track.isStarted) {
			track.isStarted = true;

			// Sound tracks with a source element play the source's sound through the target
			Common::SharedPtr<Structural> source = track.source.lock();
			if (target && track.sourceGUID != 0 && !track.assetSwapped) {
				DynamicValue asset;
				if (!source || !dlgMgrReadAttribute(runtime, this, source.get(), "asset", asset) || asset.getType() != DynamicValueTypes::kString) {
					// The source may be in a scene that isn't loaded, so sounds use the media name stored with the track
					if (track.trackType == Data::KW::DlgMgrTrack::kTrackTypeSound)
						asset.setString(track.mediaName);
					else
						asset.clear();
				}

				if (asset.getType() == DynamicValueTypes::kString && !asset.getString().empty()) {
					debug(2, "DlgMgr: Sound '%s' plays asset '%s'", target->getName().c_str(), asset.getString().c_str());
					dlgMgrWriteAttribute(runtime, this, target.get(), "asset", asset);
				}
				track.assetSwapped = true;
			}
		}

		if (track.trackType == Data::KW::DlgMgrTrack::kTrackTypeSound) {
			if (!track.isSoundPlaying) {
				track.isSoundPlaying = true;
				dlgMgrSendCommand(runtime, this, target.get(), EventIDs::kPlay);
			}
		} else {
			if (!track.isShown && (!track.hideAtEnd || track.endFrame != track.startFrame)) {
				track.isShown = true;
				dlgMgrSetBool(runtime, this, target.get(), "visible", true);
			}

			if (track.trackType == Data::KW::DlgMgrTrack::kTrackTypeAnimation && frame <= track.endFrame) {
				int32 cel = dlgMgrCelForFrame(track, frame);
				if (cel > 0)
					dlgMgrSetInt(runtime, this, target.get(), "cel", cel);
			}
		}

		if (frame > track.endFrame) {
			track.isFinished = true;

			if (track.trackType == Data::KW::DlgMgrTrack::kTrackTypeSound) {
				if (track.isSoundPlaying)
					dlgMgrSendCommand(runtime, this, target.get(), EventIDs::kStop);
				track.isSoundPlaying = false;
			} else if (track.hideAtEnd) {
				dlgMgrSetBool(runtime, this, target.get(), "visible", false);
			}
		}
	}

	if (frame > _maxEndFrame) {
		_isPlaying = false;
		_currentClient.reset();
		client->onPlaybackFinished(runtime, true);
	}
}

void DlgMgrServerModifier::stopCurrentLine(Runtime *runtime, bool resetTracks, bool notifyDone) {
	DlgMgrClientModifier *client = getCurrentClient();
	_isPlaying = false;
	_currentClient.reset();
	_skipRequested = false;

	if (!client)
		return;

	for (DlgMgrClientModifier::Track &track : client->getTracks()) {
		Common::SharedPtr<Structural> target = track.target.lock();

		if (track.trackType == Data::KW::DlgMgrTrack::kTrackTypeSound) {
			if (track.isSoundPlaying)
				dlgMgrSendCommand(runtime, this, target.get(), EventIDs::kStop);
			track.isSoundPlaying = false;
		} else if (resetTracks) {
			if (track.trackType == Data::KW::DlgMgrTrack::kTrackTypeCue) {
				if (!track.isFinished)
					client->sendCue(runtime, track.cueValue);
			} else {
				if (track.trackType == Data::KW::DlgMgrTrack::kTrackTypeAnimation) {
					int32 cel = dlgMgrCelForFrame(track, track.endFrame);
					if (cel > 0)
						dlgMgrSetInt(runtime, this, target.get(), "cel", cel);
				}

				if (track.hideAtEnd)
					dlgMgrSetBool(runtime, this, target.get(), "visible", false);
			}
		}

		track.isFinished = true;
	}

	client->onPlaybackFinished(runtime, notifyDone);
}

void DlgMgrServerModifier::flushQueue(Runtime *runtime, bool notifyDone) {
	Common::Array<Common::WeakPtr<RuntimeObject> > queue;
	queue.swap(_queue);

	for (const Common::WeakPtr<RuntimeObject> &clientWeak : queue) {
		Common::SharedPtr<RuntimeObject> client = clientWeak.lock();
		if (client)
			static_cast<DlgMgrClientModifier *>(client.get())->onPlaybackFinished(runtime, notifyDone);
	}
}

void DlgMgrServerModifier::setPaused(Runtime *runtime, bool paused) {
	if (paused == _paused)
		return;

	_paused = paused;

	DlgMgrClientModifier *client = getCurrentClient();
	if (_isPlaying && client) {
		if (paused)
			_frameOffset = getCurrentFrame(runtime) - 1;
		else
			_startTime = runtime->getPlayTime();

		for (DlgMgrClientModifier::Track &track : client->getTracks()) {
			if (track.trackType == Data::KW::DlgMgrTrack::kTrackTypeSound && track.isSoundPlaying)
				dlgMgrSetBool(runtime, this, track.target.lock().get(), "paused", paused);
		}
	}
}

#ifdef MTROPOLIS_DEBUG_ENABLE
void DlgMgrServerModifier::debugInspect(IDebugInspectionReport *report) const {
	Modifier::debugInspect(report);

	report->declareDynamic("captionFile", _captionFile);
	report->declareDynamic("registryKey", _registryKey);
	report->declareDynamic("queued", Common::String::format("%u", _queue.size()));
	report->declareDynamic("playing", _isPlaying ? "true" : "false");
}
#endif

Common::SharedPtr<Modifier> DlgMgrServerModifier::shallowClone() const {
	return Common::SharedPtr<Modifier>(new DlgMgrServerModifier(*this));
}

const char *DlgMgrServerModifier::getDefaultName() const {
	return "DlgMgrServer";
}

DlgMgrClientModifier::Track::Track()
	: trackType(0), startFrame(0), endFrame(0), loopStartCel(0), loopEndCel(0), hideAtEnd(false), preload(false), cueValue(0),
	  targetGUID(0), sourceGUID(0), isStarted(false), isFinished(false), isSoundPlaying(false), isShown(false), assetSwapped(false) {
}

DlgMgrClientModifier::DlgMgrClientModifier()
	: _hasCue(false), _hasDone(false), _triggerValue(0), _filterByTriggerValue(false), _plugIn(nullptr), _isQueued(false) {
}

DlgMgrClientModifier::~DlgMgrClientModifier() {
}

bool DlgMgrClientModifier::load(const PlugInModifierLoaderContext &context, const Data::KW::DlgMgrClientModifier &data) {
	if (!loadDlgMgrEvent(_playWhen, data.playWhen) || !loadDlgMgrEvent(_stopWhen, data.stopWhen))
		return false;

	_hasCue = loadDlgMgrSend(_cueSend, data.cueEvent, data.cueDestination);
	_hasDone = loadDlgMgrSend(_doneSend, data.doneEvent, data.doneDestination);

	if (data.triggerValue.type == Data::PlugInTypeTaggedValue::kInteger)
		_triggerValue = data.triggerValue.value1;
	_filterByTriggerValue = (data.filterByTriggerValue.type == Data::PlugInTypeTaggedValue::kBoolean && data.filterByTriggerValue.value1 != 0);

	_tracks.resize(data.tracks.size());
	for (uint i = 0; i < data.tracks.size(); i++) {
		const Data::KW::DlgMgrTrack &trackData = data.tracks[i];
		Track &track = _tracks[i];

		track.trackType = trackData.trackType;
		track.startFrame = trackData.startFrame;
		track.endFrame = trackData.endFrame;
		track.loopStartCel = trackData.loopStartCel;
		track.loopEndCel = trackData.loopEndCel;
		track.hideAtEnd = (trackData.hideAtEnd != 0);
		track.preload = (trackData.preload != 0);
		track.cueValue = trackData.cueValue;
		track.targetGUID = static_cast<uint32>(trackData.targetGUID);
		track.sourceGUID = static_cast<uint32>(trackData.sourceGUID);

		for (int32 guid : trackData.targetPath)
			if (guid != 0)
				track.targetPath.push_back(static_cast<uint32>(guid));
		for (int32 guid : trackData.sourcePath)
			if (guid != 0)
				track.sourcePath.push_back(static_cast<uint32>(guid));
		track.frames = trackData.frames;

		if (trackData.hasMedia && trackData.mediaName.data.size() > 0)
			track.mediaName = Common::String(reinterpret_cast<const char *>(&trackData.mediaName.data[0]), trackData.mediaName.data.size());
	}

	_plugIn = static_cast<KnowWonderPlugIn *>(context.plugIn);

	return true;
}

bool DlgMgrClientModifier::respondsToEvent(const Event &evt) const {
	return _playWhen.respondsTo(evt) || _stopWhen.respondsTo(evt) || evt.eventType == EventIDs::kParentDisabled;
}

VThreadState DlgMgrClientModifier::consumeMessage(Runtime *runtime, const Common::SharedPtr<MessageProperties> &msg) {
	const Event &evt = msg->getEvent();

	if (_playWhen.respondsTo(evt)) {
		if (_filterByTriggerValue) {
			int32 value = 0;
			if (!msg->getValue().roundToInt(value) || value != _triggerValue)
				return kVThreadReturn;
		}

		if (!_isQueued) {
			DlgMgrServerModifier *server = findServer();
			if (server) {
				_isQueued = true;
				server->enqueue(this);
			} else
				warning("DlgMgrClient '%s': No dialog manager server is running", getName().c_str());
		}
	} else if (_stopWhen.respondsTo(evt) || evt.eventType == EventIDs::kParentDisabled) {
		DlgMgrServerModifier *server = findServer();
		if (server && _isQueued)
			server->cancelClient(runtime, this, evt.eventType != EventIDs::kParentDisabled);
	}

	return kVThreadReturn;
}

void DlgMgrClientModifier::disable(Runtime *runtime) {
	DlgMgrServerModifier *server = findServer();
	if (server && _isQueued)
		server->cancelClient(runtime, this, false);
}

Common::Array<DlgMgrClientModifier::Track> &DlgMgrClientModifier::getTracks() {
	return _tracks;
}

void DlgMgrClientModifier::onPlaybackStarted() {
}

void DlgMgrClientModifier::onPlaybackFinished(Runtime *runtime, bool notifyDone) {
	_isQueued = false;

	if (notifyDone && _hasDone)
		_doneSend.sendFromMessengerWithCustomData(runtime, this, nullptr, DynamicValue(), nullptr);
}

void DlgMgrClientModifier::sendCue(Runtime *runtime, int32 cueValue) {
	if (!_hasCue)
		return;

	DynamicValue value;
	value.setInt(cueValue);
	_cueSend.sendFromMessengerWithCustomData(runtime, this, nullptr, value, nullptr);
}

static Common::SharedPtr<Structural> dlgMgrResolvePath(Runtime *runtime, const Common::Array<uint32> &path, uint32 guid) {
	Structural *current = runtime->getProject();

	for (uint32 pathGUID : path) {
		if (current->getStaticGUID() == pathGUID)
			continue;

		Structural *next = nullptr;
		for (const Common::SharedPtr<Structural> &child : current->getChildren()) {
			if (child->getStaticGUID() == pathGUID) {
				next = child.get();
				break;
			}
		}

		if (!next)
			return nullptr;

		current = next;
	}

	if (current->getStaticGUID() != guid)
		return nullptr;

	return current->getSelfReference().lock().staticCast<Structural>();
}

void DlgMgrClientModifier::resolveTargets(Runtime *runtime) {
	for (Track &track : _tracks) {
		// Cue tracks have no target, their target fields are left over data
		if (track.trackType == Data::KW::DlgMgrTrack::kTrackTypeCue)
			continue;

		if (track.targetGUID != 0 && track.target.expired()) {
			track.target = dlgMgrResolvePath(runtime, track.targetPath, track.targetGUID);
			if (track.target.expired())
				warning("DlgMgrClient '%s': Couldn't resolve track target %x", getName().c_str(), track.targetGUID);
		}
		if (track.sourceGUID != 0 && track.source.expired())
			track.source = dlgMgrResolvePath(runtime, track.sourcePath, track.sourceGUID);
	}
}

bool DlgMgrClientModifier::isQueued() const {
	return _isQueued;
}

DlgMgrServerModifier *DlgMgrClientModifier::findServer() const {
	return _plugIn ? _plugIn->getDlgMgrServer() : nullptr;
}

void DlgMgrClientModifier::linkInternalReferences(ObjectLinkingScope *scope) {
	Modifier::linkInternalReferences(scope);

	if (_hasCue)
		_cueSend.linkInternalReferences(scope);
	if (_hasDone)
		_doneSend.linkInternalReferences(scope);

	for (Track &track : _tracks) {
		if (track.targetGUID != 0) {
			Common::SharedPtr<RuntimeObject> target = scope->resolve(track.targetGUID).lock();
			if (target && target->isStructural())
				track.target = target.staticCast<Structural>();
			// Otherwise the target is in another scene, and is resolved by its path when played
		}
		if (track.sourceGUID != 0) {
			Common::SharedPtr<RuntimeObject> source = scope->resolve(track.sourceGUID).lock();
			if (source && source->isStructural())
				track.source = source.staticCast<Structural>();
		}
	}
}

void DlgMgrClientModifier::visitInternalReferences(IStructuralReferenceVisitor *visitor) {
	Modifier::visitInternalReferences(visitor);

	if (_hasCue)
		_cueSend.visitInternalReferences(visitor);
	if (_hasDone)
		_doneSend.visitInternalReferences(visitor);

	for (Track &track : _tracks) {
		visitor->visitWeakStructuralRef(track.target);
		visitor->visitWeakStructuralRef(track.source);
	}
}

#ifdef MTROPOLIS_DEBUG_ENABLE
void DlgMgrClientModifier::debugInspect(IDebugInspectionReport *report) const {
	Modifier::debugInspect(report);

	report->declareDynamic("numTracks", Common::String::format("%u", _tracks.size()));
	report->declareDynamic("triggerValue", Common::String::format("%i", _triggerValue));
	report->declareDynamic("queued", _isQueued ? "true" : "false");
}
#endif

Common::SharedPtr<Modifier> DlgMgrClientModifier::shallowClone() const {
	return Common::SharedPtr<Modifier>(new DlgMgrClientModifier(*this));
}

const char *DlgMgrClientModifier::getDefaultName() const {
	return "DlgMgrClient";
}

KnowWonderPlugIn::KnowWonderPlugIn()
	: _strUtilModifierFactory(this), _dlgMgrServerModifierFactory(this), _dlgMgrClientModifierFactory(this),
	  _kwSmackerModifierFactory(this), _editCtrlModifierFactory(this), _gameModifierFactory(this) {
}

KnowWonderPlugIn::~KnowWonderPlugIn() {
}

void KnowWonderPlugIn::setDlgMgrServer(const Common::WeakPtr<RuntimeObject> &server) {
	_dlgMgrServer = server;
}

DlgMgrServerModifier *KnowWonderPlugIn::getDlgMgrServer() const {
	Common::SharedPtr<RuntimeObject> server = _dlgMgrServer.lock();
	return server ? static_cast<DlgMgrServerModifier *>(server.get()) : nullptr;
}

void KnowWonderPlugIn::registerModifiers(IPlugInModifierRegistrar *registrar) const {
	registrar->registerPlugInModifier("StrUtil", &_strUtilModifierFactory);
	registrar->registerPlugInModifier("DlgMgrServer", &_dlgMgrServerModifierFactory);
	registrar->registerPlugInModifier("DlgMgrClient", &_dlgMgrClientModifierFactory);
	registrar->registerPlugInModifier("KWSmacker", &_kwSmackerModifierFactory);
	registrar->registerPlugInModifier("EditCtrl", &_editCtrlModifierFactory);

	// Mini-game modifiers from separate Magic School Bus plug-ins
	static const char *const gameModifierNames[] = {
		"AntMaze", "Art", "Balloon", "Eyefollower", "LLava", "Lander", "RoverRace", "SmkJump", "Sub", "SvTown", "WhaleMaze"
	};
	for (const char *name : gameModifierNames)
		registrar->registerPlugInModifier(name, &_gameModifierFactory);
}

} // End of namespace KW

namespace PlugIns {

Common::SharedPtr<PlugIn> createKnowWonder() {
	return Common::SharedPtr<PlugIn>(new KW::KnowWonderPlugIn());
}

} // End of namespace PlugIns

} // End of namespace MTropolis
