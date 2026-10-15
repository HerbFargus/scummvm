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

#include "common/endian.h"
#include "common/file.h"

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

WhaleMazeModifier::Mover::Mover() : path(0), target(0), dir(1), x(0.0), y(0.0), moving(false) {
}

WhaleMazeModifier::Orca::Orca() : active(false), visible(false), animTicks(0), diving(false) {
}

WhaleMazeModifier::Hazard::Hazard() : state(0), countdown(0), frame(-1) {
}

WhaleMazeModifier::WhaleMazeModifier()
	: _levelsLoaded(false), _gameLevel(1), _paused(true), _initialized(false), _running(false), _orcasFrozen(true),
	  _sonarCel(1), _whaleRangeTicks(0), _runtime(nullptr) {
	for (bool &key : _keyDown)
		key = false;
	for (bool &eaten : _foodEaten)
		eaten = false;
}

WhaleMazeModifier::WhaleMazeModifier(const WhaleMazeModifier &other)
	: Modifier(other), _startWhen(other._startWhen), _stopWhen(other._stopWhen), _levels(other._levels), _levelsLoaded(other._levelsLoaded),
	  _gameLevel(other._gameLevel), _paused(true), _initialized(false), _running(false), _orcasFrozen(true), _sonarCel(1),
	  _whaleRangeTicks(0), _runtime(nullptr) {
	for (bool &key : _keyDown)
		key = false;
	for (bool &eaten : _foodEaten)
		eaten = false;
}

WhaleMazeModifier::~WhaleMazeModifier() {
	stopGame();
}

bool WhaleMazeModifier::load(const PlugInModifierLoaderContext &context, const Data::KW::GameModifier &data) {
	if (!loadDlgMgrEvent(_startWhen, data.enableWhen) || !loadDlgMgrEvent(_stopWhen, data.disableWhen))
		return false;

	return true;
}

// The level graphs are tables in the plug-in's data section
bool WhaleMazeModifier::loadLevels() {
	if (_levelsLoaded)
		return !_levels.empty();

	_levelsLoaded = true;

	Common::File f;
	if (!f.open(Common::Path("mTrop_Resources/WhaleMaze.r32")) && !f.open(Common::Path("WhaleMaze.r32"))) {
		warning("WhaleMaze: WhaleMaze.r32 from the installed game is needed for the maze levels");
		return false;
	}

	Common::Array<byte> image;
	image.resize(f.size());
	f.read(&image[0], image.size());

	if (image.size() < 0x40)
		return false;

	uint32 peOffset = READ_LE_UINT32(&image[0x3c]);
	if (peOffset + 0xf8 > image.size())
		return false;

	uint16 numSections = READ_LE_UINT16(&image[peOffset + 6]);
	uint16 optionalSize = READ_LE_UINT16(&image[peOffset + 20]);
	uint32 imageBase = READ_LE_UINT32(&image[peOffset + 24 + 28]);
	uint32 sectionTable = peOffset + 24 + optionalSize;

	// Maps a virtual address to a file offset, or 0 if it isn't in a section
	auto fileOffset = [&](uint32 va, uint32 size) -> uint32 {
		uint32 rva = va - imageBase;
		for (uint i = 0; i < numSections; i++) {
			const byte *section = &image[sectionTable + i * 40];
			uint32 virtualSize = READ_LE_UINT32(section + 8);
			uint32 virtualAddress = READ_LE_UINT32(section + 12);
			uint32 rawSize = READ_LE_UINT32(section + 16);
			uint32 rawPointer = READ_LE_UINT32(section + 20);
			if (rva >= virtualAddress && rva + size <= virtualAddress + MAX(virtualSize, rawSize)) {
				uint32 offset = rawPointer + (rva - virtualAddress);
				if (offset + size <= image.size())
					return offset;
			}
		}
		return 0;
	};

	bool failed = false;
	auto readS32 = [&](uint32 va) -> int32 {
		uint32 offset = fileOffset(va, 4);
		if (!offset) {
			failed = true;
			return 0;
		}
		return static_cast<int32>(READ_LE_UINT32(&image[offset]));
	};
	auto readS16 = [&](uint32 va) -> int16 {
		uint32 offset = fileOffset(va, 2);
		if (!offset) {
			failed = true;
			return 0;
		}
		return static_cast<int16>(READ_LE_UINT16(&image[offset]));
	};
	auto readU8 = [&](uint32 va) -> uint8 {
		uint32 offset = fileOffset(va, 1);
		if (!offset) {
			failed = true;
			return 0;
		}
		return image[offset];
	};

	auto readPaths = [&](uint32 countBase, uint32 nodeBase, uint maxPaths, Common::Array<Common::Array<Node> > &paths) {
		for (uint p = 0; p < maxPaths; p++) {
			int16 count = readS16(countBase + p * 0x98);
			Common::Array<Node> nodes;
			for (int n = 0; n < count && n < 15; n++) {
				uint32 nodeVA = nodeBase + p * 0x98 + n * 10;
				Node node;
				node.x = readS32(nodeVA);
				node.y = readS32(nodeVA + 4);
				node.type = readS16(nodeVA + 8);
				nodes.push_back(node);
			}
			paths.push_back(nodes);
		}
	};

	auto readJunctions = [&](uint32 countVA, uint32 entryBase, Common::Array<Junction> &junctions) {
		int16 count = readS16(countVA);
		for (int j = 0; j < count && j < 40; j++) {
			uint32 entryVA = entryBase + j * 0x1c;
			Junction junction;
			junction.x = readS32(entryVA);
			junction.y = readS32(entryVA + 4);
			for (uint k = 0; k < 5; k++) {
				int16 path = readS16(entryVA + 8 + k * 4);
				if (path < 0)
					break;
				JunctionExit exit;
				exit.path = path;
				exit.forward = (readU8(entryVA + 10 + k * 4) != 0);
				junction.exits.push_back(exit);
			}
			junctions.push_back(junction);
		}
	};

	const uint kNumLevels = 15;
	Common::Array<Level> levels;
	levels.resize(kNumLevels + 1);

	for (uint l = 1; l <= kNumLevels; l++) {
		Level &level = levels[l];
		uint32 base = imageBase + 0xb76e + l * 0x1952;

		level.whale.start = Common::Point(readS32(base), readS32(base + 4));
		readPaths(base + 0x10, base + 0x12, 30, level.whale.paths);
		readJunctions(base + 0x11e0, base + 0x11e2, level.whale.junctions);

		for (uint i = 0; i < 3; i++) {
			uint32 itemVA = base + 0x1402 + (i + 1) * 0x10;
			Item &item = level.food[i];
			item.pos = Common::Point(readS32(itemVA), readS32(itemVA + 4));
			item.hit = Common::Point(readS32(itemVA + 8), readS32(itemVA + 12));
			item.type = 0;
		}

		for (uint i = 0; i < 6; i++) {
			uint32 itemVA = base + 0x1422 + (i + 1) * 0x20;
			Item &item = level.hazards[i];
			item.pos = Common::Point(readS32(itemVA), readS32(itemVA + 4));
			item.hit = Common::Point(readS32(itemVA + 8), readS32(itemVA + 12));
			item.type = readS32(itemVA + 16);
			for (uint d = 0; d < 3; d++)
				item.durations[d] = readS32(itemVA + 20 + d * 4);
		}

		for (uint i = 0; i < 2; i++) {
			uint32 orcaVA = base + 0x12da + (i + 1) * 0x228;
			Graph &orca = level.orcas[i];
			orca.start = Common::Point(readS32(orcaVA), readS32(orcaVA + 4));
			readPaths(orcaVA + 0xa, orcaVA + 0xc, 3, orca.paths);
			readJunctions(orcaVA + 0x1d2, orcaVA + 0x1d4, orca.junctions);
		}
	}

	if (failed) {
		warning("WhaleMaze: WhaleMaze.r32 isn't the expected version");
		return false;
	}

	_levels.swap(levels);
	return true;
}

bool WhaleMazeModifier::respondsToEvent(const Event &evt) const {
	return _startWhen.respondsTo(evt) || _stopWhen.respondsTo(evt) || evt.eventType == EventIDs::kParentDisabled
		|| evt.eventType == EventIDs::kMouseDown || evt.eventType == EventIDs::kMouseUp;
}

VThreadState WhaleMazeModifier::consumeMessage(Runtime *runtime, const Common::SharedPtr<MessageProperties> &msg) {
	const Event &evt = msg->getEvent();

	if (_stopWhen.respondsTo(evt) || evt.eventType == EventIDs::kParentDisabled) {
		stopGame();
		return kVThreadReturn;
	}

	if (_startWhen.respondsTo(evt)) {
		_runtime = runtime;
		_paused = true;
		if (!_running) {
			_running = true;
			_keySignaller = runtime->getProject()->notifyOnKeyboardEvent(this);
			scheduleTick(runtime);
		}
		return kVThreadReturn;
	}

	if (!_running || !_initialized)
		return kVThreadReturn;

	if (evt.eventType == EventIDs::kMouseUp) {
		for (bool &key : _keyDown)
			key = false;
		return kVThreadReturn;
	}

	if (evt.eventType == EventIDs::kMouseDown && !_paused && msg->getValue().getType() == DynamicValueTypes::kPoint) {
		Common::Point mousePos = msg->getValue().getPoint();
		double dx = mousePos.x - _whale.x;
		double dy = mousePos.y - _whale.y;
		double target = headingTo(_whale.x, _whale.y, mousePos.x, mousePos.y);

		// Clicks only choose a direction near the whale
		bool axisAligned = (target == 0.0 || target == 90.0 || target == 180.0 || target == 270.0);
		double distance = sqrt(dx * dx + dy * dy);
		if (distance < (axisAligned ? 80.0 : 115.0))
			handleInput(runtime, matchMouse, target);
		else
			handleInput(runtime, nullptr, 0.0);
	}

	return kVThreadReturn;
}

void WhaleMazeModifier::onKeyboardEvent(Runtime *runtime, const KeyboardInputEvent &keyEvt) {
	Key key = kKeyCount;
	double target = 0.0;
	switch (keyEvt.getKeyState().keycode) {
	case Common::KEYCODE_UP:
		key = kKeyUp;
		target = 0.0;
		break;
	case Common::KEYCODE_RIGHT:
		key = kKeyRight;
		target = 90.0;
		break;
	case Common::KEYCODE_DOWN:
		key = kKeyDown;
		target = 180.0;
		break;
	case Common::KEYCODE_LEFT:
		key = kKeyLeft;
		target = 270.0;
		break;
	default:
		return;
	}

	if (keyEvt.getKeyEventType() == Common::EVENT_KEYUP) {
		_keyDown[key] = false;
		return;
	}

	if (keyEvt.getKeyEventType() != Common::EVENT_KEYDOWN || keyEvt.isRepeat() || _keyDown[key])
		return;

	_keyDown[key] = true;
	if (_running && _initialized && !_paused)
		handleInput(runtime, matchKey, target);
}

void WhaleMazeModifier::disable(Runtime *runtime) {
	stopGame();
}

void WhaleMazeModifier::stopGame() {
	_running = false;
	if (_scheduledEvent) {
		_scheduledEvent->cancel();
		_scheduledEvent.reset();
	}
	if (_keySignaller) {
		_keySignaller->removeReceiver(this);
		_keySignaller.reset();
	}
}

void WhaleMazeModifier::scheduleTick(Runtime *runtime) {
	// The original ticks every 83 milliseconds
	_scheduledEvent = runtime->getScheduler().scheduleMethod<WhaleMazeModifier, &WhaleMazeModifier::tick>(runtime->getPlayTime() + 83, this);
}

bool WhaleMazeModifier::readAttribute(MiniscriptThread *thread, DynamicValue &result, const Common::String &attrib) {
	if (attrib == "paused") {
		result.setBool(_paused);
		return true;
	} else if (attrib == "gamelevel") {
		result.setInt(_gameLevel);
		return true;
	} else if (attrib == "points") {
		result.setInt(0);
		return true;
	}

	return Modifier::readAttribute(thread, result, attrib);
}

MiniscriptInstructionOutcome WhaleMazeModifier::writeRefAttribute(MiniscriptThread *thread, DynamicValueWriteProxy &result, const Common::String &attrib) {
	WritableAttribute writable;
	if (attrib == "paused")
		writable = kAttribPaused;
	else if (attrib == "gamelevel")
		writable = kAttribGameLevel;
	else
		return Modifier::writeRefAttribute(thread, result, attrib);

	result.pod.ifc = DynamicValueWriteInterfaceGlue<WriteProxy>::getInstance();
	result.pod.objectRef = this;
	result.pod.ptrOrOffset = writable;
	return kMiniscriptInstructionOutcomeContinue;
}

MiniscriptInstructionOutcome WhaleMazeModifier::WriteProxy::write(MiniscriptThread *thread, const DynamicValue &assigned, void *objectRef, uintptr ptrOrOffset) {
	WhaleMazeModifier *self = static_cast<WhaleMazeModifier *>(objectRef);

	// Variables are passed by reference
	DynamicValue value = assigned;
	if (value.getType() == DynamicValueTypes::kObject) {
		Common::SharedPtr<RuntimeObject> obj = value.getObject().object.lock();
		if (obj && obj->isModifier() && static_cast<Modifier *>(obj.get())->isVariable())
			static_cast<VariableModifier *>(obj.get())->varGetValue(value);
	}

	if (ptrOrOffset == kAttribPaused) {
		bool paused = false;
		if (value.getType() == DynamicValueTypes::kBoolean)
			paused = value.getBool();
		else if (value.getType() == DynamicValueTypes::kInteger)
			paused = (value.getInt() != 0);
		else
			return kMiniscriptInstructionOutcomeFailed;

		// Unpausing does nothing until a level is set up
		if (self->_initialized || paused)
			self->_paused = paused;
	} else {
		int32 level = 0;
		if (!value.roundToInt(level))
			return kMiniscriptInstructionOutcomeFailed;

		self->_gameLevel = level;
		self->setupLevel(thread->getRuntime());
	}

	return kMiniscriptInstructionOutcomeContinue;
}

MiniscriptInstructionOutcome WhaleMazeModifier::WriteProxy::refAttrib(MiniscriptThread *thread, DynamicValueWriteProxy &proxy, void *objectRef, uintptr ptrOrOffset, const Common::String &attrib) {
	return kMiniscriptInstructionOutcomeFailed;
}

MiniscriptInstructionOutcome WhaleMazeModifier::WriteProxy::refAttribIndexed(MiniscriptThread *thread, DynamicValueWriteProxy &proxy, void *objectRef, uintptr ptrOrOffset, const Common::String &attrib, const DynamicValue &index) {
	return kMiniscriptInstructionOutcomeFailed;
}

// Children of the owner element: 1 whale, 2 backgrounds, 3 food, 4 hazards, 5 orcas, 6 sonar
Structural *WhaleMazeModifier::child(int index) const {
	Structural *owner = findStructuralOwner();
	if (!owner || index < 1 || index > static_cast<int>(owner->getChildren().size()))
		return nullptr;
	return owner->getChildren()[index - 1].get();
}

Structural *WhaleMazeModifier::grandchild(int index, int subIndex) const {
	Structural *container = child(index);
	if (!container || subIndex < 1 || subIndex > static_cast<int>(container->getChildren().size()))
		return nullptr;
	return container->getChildren()[subIndex - 1].get();
}

void WhaleMazeModifier::setAttribute(Runtime *runtime, Structural *element, const char *attrib, const DynamicValue &value) {
	dlgMgrWriteAttribute(runtime, this, element, attrib, value);
}

void WhaleMazeModifier::placeTopLeft(Runtime *runtime, Structural *element, int32 x, int32 y) {
	DynamicValue pos;
	pos.setPoint(Common::Point(x, y));
	setAttribute(runtime, element, "position", pos);
}

void WhaleMazeModifier::placeCentered(Runtime *runtime, Structural *element, double x, double y) {
	if (!element || !element->isElement() || !static_cast<Element *>(element)->isVisual())
		return;

	const Common::Rect &rect = static_cast<VisualElement *>(element)->getRelativeRect();
	placeTopLeft(runtime, element, static_cast<int32>(round(x)) - rect.width() / 2, static_cast<int32>(round(y)) - rect.height() / 2);
}

void WhaleMazeModifier::sendMessage(Runtime *runtime, const char *messageName) {
	uint32 messageID = 0;
	if (!runtime->getProject()->findAuthorMessageID(messageName, messageID))
		return;

	Structural *owner = findStructuralOwner();
	if (!owner)
		return;

	Common::SharedPtr<MessageProperties> props(new MessageProperties(Event(EventIDs::kAuthorMessage, messageID), DynamicValue(), getSelfReference()));
	Common::SharedPtr<MessageDispatch> dispatch(new MessageDispatch(props, owner, true, true, false));
	runtime->queueMessage(dispatch);
}

const WhaleMazeModifier::Node &WhaleMazeModifier::moverNode(const Graph &graph, const Mover &mover, int index) const {
	return graph.paths[mover.path][index];
}

bool WhaleMazeModifier::moverAtNode(const Graph &graph, const Mover &mover) const {
	const Node &node = moverNode(graph, mover, mover.target);
	return mover.x == node.x && mover.y == node.y;
}

const WhaleMazeModifier::Junction *WhaleMazeModifier::findJunction(const Graph &graph, int32 x, int32 y) const {
	for (const Junction &junction : graph.junctions) {
		if (junction.x == x && junction.y == y)
			return &junction;
	}
	return nullptr;
}

// 0 is up, increasing clockwise, truncated to a tenth of a degree like the original
double WhaleMazeModifier::headingTo(double fromX, double fromY, double toX, double toY) {
	double degrees = atan2(toX - fromX, -(toY - fromY)) * 57.3248;
	if (degrees < 0.0)
		degrees += 360.0;
	if (degrees >= 360.0)
		degrees -= 360.0;
	return floor(degrees * 10.0) / 10.0;
}

Common::String WhaleMazeModifier::angleLabel(double heading) {
	static const double kBounds[] = {10, 25, 55, 75, 100, 120, 145, 165, 190, 215, 235, 255, 280, 300, 325, 345};
	static const char *const kLabels[] = {"0", "22.5", "45", "67.5", "90", "112.5", "135", "157.5", "180", "202.5", "225", "247.5", "270", "292.5", "315", "337.5"};

	for (uint i = 0; i < ARRAYSIZE(kBounds); i++) {
		if (heading < kBounds[i])
			return kLabels[i];
	}
	return "0";
}

bool WhaleMazeModifier::matchKey(double candidate, double target) {
	double delta = fabs(candidate - target);
	if (delta > 180.0)
		delta = 360.0 - delta;
	return delta <= 45.0;
}

bool WhaleMazeModifier::matchMouse(double candidate, double target) {
	if (target < 22.5 || target > 337.5)
		return candidate > 345.0 || candidate < 15.0;
	return fabs(candidate - target) < 45.0;
}

void WhaleMazeModifier::setupLevel(Runtime *runtime) {
	_runtime = runtime;
	if (!loadLevels() || _gameLevel < 1 || _gameLevel >= static_cast<int32>(_levels.size()))
		return;

	const Level &level = _levels[_gameLevel];

	_paused = false;
	for (bool &key : _keyDown)
		key = false;

	// Show this level's background
	Structural *backgrounds = child(2);
	if (backgrounds) {
		for (uint i = 0; i < backgrounds->getChildren().size(); i++)
			placeTopLeft(runtime, backgrounds->getChildren()[i].get(), 0, (static_cast<int32>(i + 1) == _gameLevel) ? 0 : 600);
	}

	for (uint i = 0; i < 3; i++) {
		_foodEaten[i] = false;
		placeTopLeft(runtime, grandchild(3, i + 1), level.food[i].pos.x, level.food[i].pos.y);
	}

	for (uint i = 0; i < 6; i++) {
		const Item &hazard = level.hazards[i];
		_hazards[i] = Hazard();
		_hazards[i].countdown = hazard.durations[0];
		placeTopLeft(runtime, grandchild(4, i + 1), hazard.pos.x, hazard.pos.y);
	}

	_orcasFrozen = true;
	for (uint i = 0; i < 2; i++) {
		const Graph &graph = level.orcas[i];
		Orca &orca = _orcas[i];
		orca = Orca();

		orca.active = (graph.start.y < 480 && !graph.paths.empty() && graph.paths[0].size() >= 2);
		if (orca.active) {
			orca.visible = true;
			orca.mover.x = graph.start.x;
			orca.mover.y = graph.start.y;
			for (uint p = 0; p < graph.paths.size(); p++) {
				for (uint n = 0; n < graph.paths[p].size(); n++) {
					if (graph.paths[p][n].x == graph.start.x && graph.paths[p][n].y == graph.start.y) {
						orca.mover.path = p;
						orca.mover.target = n;
					}
				}
			}
			orca.mover.dir = (orca.mover.target + 1 < static_cast<int>(graph.paths[orca.mover.path].size())) ? 1 : -1;
			orca.mover.moving = true;
			orca.mover.target += orca.mover.dir;
			placeCentered(runtime, grandchild(5, i + 1), orca.mover.x, orca.mover.y);
		} else
			placeTopLeft(runtime, grandchild(5, i + 1), 0, 700);
	}

	resetWhale(runtime);
	_initialized = true;
}

void WhaleMazeModifier::resetWhale(Runtime *runtime) {
	const Graph &graph = _levels[_gameLevel].whale;

	_whale = Mover();
	_whale.x = graph.start.x;
	_whale.y = graph.start.y;
	for (uint p = 0; p < graph.paths.size(); p++) {
		for (uint n = 0; n < graph.paths[p].size(); n++) {
			if (graph.paths[p][n].x == graph.start.x && graph.paths[p][n].y == graph.start.y) {
				_whale.path = p;
				_whale.target = n;
				p = graph.paths.size() - 1;
				break;
			}
		}
	}

	placeCentered(runtime, child(1), _whale.x, _whale.y);
	placeCentered(runtime, child(6), _whale.x, _whale.y);
}

void WhaleMazeModifier::handleInput(Runtime *runtime, bool (*matches)(double candidate, double target), double target) {
	_whale.moving = false;
	_orcasFrozen = false;
	placeTopLeft(runtime, child(6), 0, 700);

	if (matches)
		chooseDirection(matches, target);
}

// Picks a way to go from where the whale is, by the heading to the next node
bool WhaleMazeModifier::chooseDirection(bool (*matches)(double candidate, double target), double target) {
	const Graph &graph = _levels[_gameLevel].whale;
	const Common::Array<Node> &path = graph.paths[_whale.path];

	bool found = false;
	Mover chosen = _whale;

	if (moverAtNode(graph, _whale)) {
		const Node &node = path[_whale.target];
		const Junction *junction = (node.type == 0) ? findJunction(graph, node.x, node.y) : nullptr;

		if (junction) {
			for (const JunctionExit &exit : junction->exits) {
				if (exit.path < 0 || exit.path >= static_cast<int16>(graph.paths.size()))
					continue;
				const Common::Array<Node> &exitPath = graph.paths[exit.path];
				if (exitPath.size() < 2)
					continue;

				int nextIndex = exit.forward ? 1 : static_cast<int>(exitPath.size()) - 2;
				if (matches(headingTo(_whale.x, _whale.y, exitPath[nextIndex].x, exitPath[nextIndex].y), target)) {
					chosen.path = exit.path;
					chosen.target = nextIndex;
					chosen.dir = exit.forward ? 1 : -1;
					found = true;
				}
			}
		} else {
			// Continue along the path, or turn back
			for (int dir : {_whale.dir, -_whale.dir}) {
				int nextIndex = _whale.target + dir;
				if (nextIndex < 0 || nextIndex >= static_cast<int>(path.size()))
					continue;
				if (matches(headingTo(_whale.x, _whale.y, path[nextIndex].x, path[nextIndex].y), target)) {
					chosen.target = nextIndex;
					chosen.dir = dir;
					found = true;
					break;
				}
			}
		}
	} else {
		// Between nodes: keep going or turn around
		for (int dir : {_whale.dir, -_whale.dir}) {
			int nextIndex = (dir == _whale.dir) ? _whale.target : _whale.target - _whale.dir;
			if (nextIndex < 0 || nextIndex >= static_cast<int>(path.size()))
				continue;
			if (matches(headingTo(_whale.x, _whale.y, path[nextIndex].x, path[nextIndex].y), target)) {
				chosen.target = nextIndex;
				chosen.dir = dir;
				found = true;
				break;
			}
		}
	}

	if (found) {
		chosen.moving = true;
		_whale = chosen;
	}

	return found;
}

void WhaleMazeModifier::arriveAtNode(Runtime *runtime) {
	const Level &level = _levels[_gameLevel];
	const Common::Array<Node> &path = level.whale.paths[_whale.path];
	const Node &node = path[_whale.target];

	auto continueOn = [&]() {
		int nextIndex = _whale.target + _whale.dir;
		if (nextIndex >= 0 && nextIndex < static_cast<int>(path.size()))
			_whale.target = nextIndex;
		else
			_whale.moving = false;
	};

	switch (node.type) {
	case 2:
		// Bounce back the way it came
		_whale.dir = -_whale.dir;
		continueOn();
		break;
	case 10:
	case 20:
	case 50:
		resetWhale(runtime);
		sendMessage(runtime, node.type == 10 ? "PlaySFX3" : node.type == 20 ? "PlaySFX4" : "PlaySFX5");
		break;
	case 99:
		_whale.moving = false;
		_paused = true;
		sendMessage(runtime, "YouWin");
		break;
	case 30:
		for (uint i = 0; i < 3; i++) {
			if (!_foodEaten[i] && level.food[i].hit.x == node.x && level.food[i].hit.y == node.y) {
				_foodEaten[i] = true;
				placeTopLeft(runtime, grandchild(3, i + 1), level.food[i].pos.x, level.food[i].pos.y + 600);
				sendMessage(runtime, "ExtraChance");
				sendMessage(runtime, "PlaySFX6");
			}
		}
		continueOn();
		break;
	case 40:
		continueOn();
		break;
	default:
		// Junctions, corners and path ends stop the whale
		_whale.moving = false;
		break;
	}

	if (!_whale.moving && !_paused) {
		// A held arrow key keeps the whale going
		static const double kKeyHeadings[kKeyCount] = {0.0, 180.0, 270.0, 90.0};
		for (uint k = 0; k < kKeyCount; k++) {
			if (_keyDown[k] && chooseDirection(matchKey, kKeyHeadings[k]))
				break;
		}
	}
}

void WhaleMazeModifier::stepWhale(Runtime *runtime) {
	if (!_whale.moving)
		return;

	const Graph &graph = _levels[_gameLevel].whale;
	const Node &node = moverNode(graph, _whale, _whale.target);

	double dx = node.x - _whale.x;
	double dy = node.y - _whale.y;
	double heading = headingTo(_whale.x, _whale.y, node.x, node.y);

	// 10 pixels along an axis, or 5 across with the matching distance down
	double stepX = 0.0;
	double stepY = 0.0;
	if (dx == 0.0)
		stepY = (dy > 0.0) ? 10.0 : -10.0;
	else if (dy == 0.0)
		stepX = (dx > 0.0) ? 10.0 : -10.0;
	else {
		stepX = (dx > 0.0) ? 5.0 : -5.0;
		stepY = dy * (5.0 / fabs(dx));
	}

	if (fabs(stepX) >= fabs(dx) && fabs(stepY) >= fabs(dy)) {
		_whale.x = node.x;
		_whale.y = node.y;
		arriveAtNode(runtime);
	} else {
		_whale.x += stepX;
		_whale.y += stepY;
	}

	DynamicValue range;
	range.setString(angleLabel(heading));
	setAttribute(runtime, child(1), "range", range);
	placeCentered(runtime, child(1), _whale.x, _whale.y);
}

void WhaleMazeModifier::updateOrca(Runtime *runtime, int index, int axisStep, int diagonalStep) {
	Orca &orca = _orcas[index];
	const Graph &graph = _levels[_gameLevel].orcas[index];
	Structural *element = grandchild(5, index + 1);
	if (!orca.active)
		return;

	Mover &mover = orca.mover;
	const Common::Array<Node> &path = graph.paths[mover.path];
	const Node &node = path[mover.target];

	double dx = node.x - mover.x;
	double dy = node.y - mover.y;
	double heading = headingTo(mover.x, mover.y, node.x, node.y);

	double stepX = 0.0;
	double stepY = 0.0;
	if (dx == 0.0)
		stepY = (dy > 0.0) ? axisStep : -axisStep;
	else if (dy == 0.0)
		stepX = (dx > 0.0) ? axisStep : -axisStep;
	else {
		stepX = (dx > 0.0) ? diagonalStep : -diagonalStep;
		stepY = dy * (diagonalStep / fabs(dx));
	}

	if (fabs(stepX) >= fabs(dx) && fabs(stepY) >= fabs(dy)) {
		mover.x = node.x;
		mover.y = node.y;

		const Junction *junction = (node.type == 0) ? findJunction(graph, node.x, node.y) : nullptr;
		if (junction) {
			// Take the exit heading closest to the whale
			double towardWhale = headingTo(mover.x, mover.y, _whale.x, _whale.y);
			double bestDelta = 1000.0;
			for (const JunctionExit &exit : junction->exits) {
				if (exit.path < 0 || exit.path >= static_cast<int16>(graph.paths.size()) || graph.paths[exit.path].size() < 2)
					continue;
				const Common::Array<Node> &exitPath = graph.paths[exit.path];
				int nextIndex = exit.forward ? 1 : static_cast<int>(exitPath.size()) - 2;
				double delta = fabs(headingTo(mover.x, mover.y, exitPath[nextIndex].x, exitPath[nextIndex].y) - towardWhale);
				if (delta < bestDelta) {
					bestDelta = delta;
					mover.path = exit.path;
					mover.target = nextIndex;
					mover.dir = exit.forward ? 1 : -1;
				}
			}
		} else {
			if (node.type == 0x37 || node.type == -1 || node.type == -2) {
				// Surface or dive
				orca.diving = (node.type == 0x37) ? orca.visible : (node.type == -2);
				orca.animTicks = 4;
				if (!orca.diving)
					orca.visible = true;
			}
			if (node.type == -1 || node.type == -2)
				mover.dir = -mover.dir;

			int nextIndex = mover.target + mover.dir;
			if (nextIndex < 0 || nextIndex >= static_cast<int>(path.size())) {
				mover.dir = -mover.dir;
				nextIndex = mover.target + mover.dir;
			}
			if (nextIndex >= 0 && nextIndex < static_cast<int>(path.size()))
				mover.target = nextIndex;
		}
	} else {
		mover.x += stepX;
		mover.y += stepY;
	}

	Common::String label = angleLabel(heading);
	if (orca.animTicks > 0) {
		label += orca.diving ? "Down" : "Up";
		if (--orca.animTicks == 0 && orca.diving)
			orca.visible = false;
	}

	DynamicValue range;
	range.setString(label);
	setAttribute(runtime, element, "range", range);

	DynamicValue visible;
	visible.setBool(orca.visible || orca.animTicks > 0);
	setAttribute(runtime, element, "visible", visible);

	placeCentered(runtime, element, mover.x, mover.y);
}

void WhaleMazeModifier::updateHazards(Runtime *runtime) {
	static const int32 kBaseCels[4] = {1, 9, 17, 25};
	const Level &level = _levels[_gameLevel];

	for (uint i = 0; i < 6; i++) {
		const Item &item = level.hazards[i];
		Hazard &hazard = _hazards[i];
		if (item.type < 1 || item.type > 4 || item.pos.y >= 480)
			continue;

		if (--hazard.countdown <= 0) {
			hazard.state = (hazard.state + 1) % 3;
			hazard.countdown = MAX<int32>(1, item.durations[hazard.state]);
			hazard.frame = 0;
		}

		if (hazard.frame >= 0) {
			DynamicValue cel;
			cel.setInt(kBaseCels[item.type - 1] + hazard.frame);
			setAttribute(runtime, grandchild(4, i + 1), "cel", cel);
			if (++hazard.frame == 8)
				hazard.frame = -1;
		}
	}
}

void WhaleMazeModifier::checkCollisions(Runtime *runtime) {
	const Level &level = _levels[_gameLevel];

	for (uint i = 0; i < 2; i++) {
		const Orca &orca = _orcas[i];
		if (!orca.active || !orca.visible)
			continue;

		double dx = orca.mover.x - _whale.x;
		double dy = orca.mover.y - _whale.y;
		if (fabs(dx) < 24.0 && dy > -22.0 && dy < 34.0) {
			resetWhale(runtime);
			sendMessage(runtime, "LoseChance");
			sendMessage(runtime, "PlaySFX1");
			return;
		}
	}

	for (uint i = 0; i < 6; i++) {
		const Item &item = level.hazards[i];
		if (item.type < 1 || item.type > 4 || _hazards[i].frame < 2)
			continue;

		double dx = fabs(item.hit.x - _whale.x);
		double dy = fabs(item.hit.y - _whale.y);
		bool vertical = (item.type == 1 || item.type == 3);
		if (vertical ? (dx < 8.0 && dy < 22.0) : (dy <= 7.0 && dx <= 21.0)) {
			resetWhale(runtime);
			sendMessage(runtime, "LoseChance");
			sendMessage(runtime, "PlaySFX2");
			return;
		}
	}
}

void WhaleMazeModifier::tick(Runtime *runtime) {
	_scheduledEvent.reset();
	if (!_running)
		return;

	scheduleTick(runtime);

	if (_paused || !_initialized || _gameLevel < 1 || _gameLevel >= static_cast<int32>(_levels.size()))
		return;

	updateHazards(runtime);

	if (!_orcasFrozen) {
		int axisStep = (_gameLevel < 6) ? 4 : (_gameLevel < 11) ? 6 : 8;
		for (int i = 0; i < 2; i++)
			updateOrca(runtime, i, axisStep, axisStep / 2);
	}

	// The sonar shows where the whale waits
	if (++_sonarCel > 12)
		_sonarCel = 1;
	DynamicValue cel;
	cel.setInt(_sonarCel);
	setAttribute(runtime, child(6), "cel", cel);

	stepWhale(runtime);

	if (!_paused)
		checkCollisions(runtime);
}

Common::SharedPtr<Modifier> WhaleMazeModifier::shallowClone() const {
	return Common::SharedPtr<Modifier>(new WhaleMazeModifier(*this));
}

const char *WhaleMazeModifier::getDefaultName() const {
	return "WhaleMaze";
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
	  _captions(false), _debugOutput(false), _relink(false), _startTime(0), _frameOffset(0), _lastFrame(-1), _maxEndFrame(0),
	  _captionNextLine(0), _captionNextPageFrame(0), _captionDuration(0), _captionTotalChars(0), _captionShown(false) {
}

DlgMgrServerModifier::DlgMgrServerModifier(const DlgMgrServerModifier &other)
	: Modifier(other), _enableWhen(other._enableWhen), _disableWhen(other._disableWhen), _captionTextEvent(other._captionTextEvent),
	  _captionSpeakerEvent(other._captionSpeakerEvent), _captionClearEvent(other._captionClearEvent), _captionFile(other._captionFile),
	  _registryKey(other._registryKey), _titleName(other._titleName), _plugIn(other._plugIn), _runtime(nullptr), _isRunning(false),
	  _isPlaying(false), _skipRequested(false), _paused(false), _captions(other._captions), _debugOutput(false), _relink(false),
	  _startTime(0), _frameOffset(0), _lastFrame(-1), _maxEndFrame(0),
	  _captionNextLine(0), _captionNextPageFrame(0), _captionDuration(0), _captionTotalChars(0), _captionShown(false) {
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
		if (flag)
			loadCaptions(runtime);
		else {
			_captionEntries.clear();
			clearCaption(runtime);
		}
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

	DlgMgrClientModifier *client = getCurrentClient();
	_currentClient.reset();
	if (client && _runtime)
		client->onPlaybackFinished(_runtime, false);

	// Release queued lines so their clients can queue again
	if (_runtime)
		flushQueue(_runtime, false);
	else
		_queue.clear();

	if (_plugIn && _plugIn->getDlgMgrServer() == this)
		_plugIn->setDlgMgrServer(Common::WeakPtr<RuntimeObject>());

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
				startCaption(runtime, client->getName(), track.startFrame, track.endFrame);
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

	updateCaption(runtime, frame);

	if (frame > _maxEndFrame) {
		_isPlaying = false;
		_currentClient.reset();
		clearCaption(runtime);
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

	clearCaption(runtime);
	client->onPlaybackFinished(runtime, notifyDone);
}

void DlgMgrServerModifier::loadCaptions(Runtime *runtime) {
	_captionEntries.clear();
	_captionNames.clear();
	_captionTexts.clear();

	if (_captionFile.empty())
		return;

	Common::ScopedPtr<Common::SeekableReadStream> stream(runtime->getProject()->createReadStreamForProjectPath(_captionFile));
	if (!stream) {
		warning("DlgMgrServer: Couldn't open caption file '%s'", _captionFile.c_str());
		return;
	}

	uint32 numEntries = stream->readUint32LE();
	if (stream->err() || numEntries == 0 || numEntries > 0x10000)
		return;

	Common::Array<CaptionEntry> entries;
	entries.resize(numEntries);
	for (CaptionEntry &entry : entries) {
		entry.textOffset = stream->readUint32LE();
		entry.nameOffset = stream->readUint32LE();
		entry.compareNames = (stream->readUint32LE() != 0);
	}

	uint32 namesSize = stream->readUint32LE();
	Common::Array<char> names;
	names.resize(namesSize + 1);
	if (namesSize)
		stream->read(&names[0], namesSize);
	names[namesSize] = '\0';

	uint32 textsSize = stream->readUint32LE();
	Common::Array<char> texts;
	texts.resize(textsSize + 1);
	if (textsSize)
		stream->read(&texts[0], textsSize);
	texts[textsSize] = '\0';

	if (stream->err()) {
		warning("DlgMgrServer: Caption file '%s' is damaged", _captionFile.c_str());
		return;
	}

	_captionEntries.swap(entries);
	_captionNames.swap(names);
	_captionTexts.swap(texts);
}

Common::String DlgMgrServerModifier::findCaption(const Common::String &lineName) const {
	if (_captionEntries.empty() || lineName.empty() || lineName[0] == '~')
		return Common::String();

	// Line names are normalized before lookup
	Common::String key = lineName;
	size_t hashPos = key.findFirstOf('#');
	if (hashPos != Common::String::npos)
		key = key.substr(0, hashPos);
	if (key.size() > 3 && (key.hasPrefix("dlg") || key.hasPrefix("Dlg")))
		key = key.substr(3);
	for (uint i = 0; i < key.size();) {
		if (key[i] == '.' || key[i] == '_')
			key.deleteChar(i);
		else
			i++;
	}

	uint32 hash = 1;
	uint32 mult = 1;
	for (uint i = key.size(); i > 0; i--) {
		signed char c = static_cast<signed char>(key[i - 1]);
		hash += static_cast<uint32>(static_cast<int32>(c)) * mult;
		mult *= Common::isDigit(c) ? 5 : 13;
		mult <<= 1;
	}

	uint numEntries = _captionEntries.size();
	uint start = hash % numEntries;
	for (uint probe = 0; probe < numEntries; probe++) {
		const CaptionEntry &entry = _captionEntries[(start + probe) % numEntries];
		if (entry.textOffset == 0xffffffffu)
			continue;

		bool matches = !entry.compareNames;
		if (!matches && entry.nameOffset < _captionNames.size())
			matches = (key == Common::String(&_captionNames[entry.nameOffset]));

		if (matches) {
			if (entry.textOffset >= _captionTexts.size())
				return Common::String();
			return Common::String(&_captionTexts[entry.textOffset]);
		}

		if (!entry.compareNames)
			break;
	}

	return Common::String();
}

void DlgMgrServerModifier::startCaption(Runtime *runtime, const Common::String &lineName, int32 startFrame, int32 endFrame) {
	if (!_captions)
		return;

	Common::String caption = findCaption(lineName);
	if (caption.empty())
		return;

	Common::String speaker;
	size_t tabPos = caption.findFirstOf('\t');
	if (tabPos != Common::String::npos) {
		speaker = caption.substr(0, tabPos);
		caption = caption.substr(tabPos + 1);
	}

	// The original wraps at 560 pixels of 14 point Arial, about 68 average characters
	const uint kMaxLineChars = 68;

	_captionLines.clear();
	Common::String line;
	size_t pos = 0;
	while (pos < caption.size()) {
		size_t spacePos = caption.find(' ', pos);
		if (spacePos == Common::String::npos)
			spacePos = caption.size();

		Common::String word = caption.substr(pos, spacePos - pos);
		if (!line.empty() && line.size() + 1 + word.size() > kMaxLineChars) {
			_captionLines.push_back(line);
			line.clear();
		}
		if (!line.empty())
			line += ' ';
		line += word;
		pos = spacePos + 1;
	}
	if (!line.empty())
		_captionLines.push_back(line);

	if (_captionLines.empty())
		return;

	_captionTotalChars = 0;
	for (const Common::String &captionLine : _captionLines)
		_captionTotalChars += captionLine.size() + 1;
	_captionTotalChars = MAX<uint32>(1, _captionTotalChars - 1);

	_captionDuration = MAX<int32>(0, endFrame - startFrame);
	_captionNextPageFrame = startFrame;
	_captionNextLine = 0;
	_captionShown = true;

	if (!speaker.empty()) {
		DynamicValue speakerValue;
		speakerValue.setString(speaker + ":");
		sendCaptionEvent(runtime, _captionSpeakerEvent, speakerValue);
	}

	sendCaptionPage(runtime);
}

void DlgMgrServerModifier::updateCaption(Runtime *runtime, int32 frame) {
	if (_captionShown && _captionNextLine < _captionLines.size() && frame >= _captionNextPageFrame)
		sendCaptionPage(runtime);
}

// Pages are two lines, each shown for its share of the line's duration
void DlgMgrServerModifier::sendCaptionPage(Runtime *runtime) {
	Common::String text;
	uint32 pageChars = 0;
	for (uint i = 0; i < 2 && _captionNextLine < _captionLines.size(); i++) {
		const Common::String &captionLine = _captionLines[_captionNextLine++];
		text += captionLine + "\r";
		pageChars += captionLine.size() + 1;
	}

	_captionNextPageFrame += static_cast<int32>(static_cast<int64>(_captionDuration) * pageChars / _captionTotalChars);

	DynamicValue textValue;
	textValue.setString(text);
	sendCaptionEvent(runtime, _captionTextEvent, textValue);
}

void DlgMgrServerModifier::clearCaption(Runtime *runtime) {
	if (!_captionShown)
		return;

	_captionShown = false;
	_captionLines.clear();
	sendCaptionEvent(runtime, _captionClearEvent, DynamicValue());
}

void DlgMgrServerModifier::sendCaptionEvent(Runtime *runtime, const Event &evt, const DynamicValue &value) {
	if (evt.eventType == EventIDs::kNothing || static_cast<uint32>(evt.eventType) == 1)
		return;

	Common::SharedPtr<Structural> scene = runtime->getActiveMainScene();
	if (!scene)
		return;

	debug(2, "DlgMgr: Caption event %u,%u", static_cast<uint>(evt.eventType), static_cast<uint>(evt.eventInfo));

	Common::SharedPtr<MessageProperties> props(new MessageProperties(evt, value, getSelfReference()));
	Common::SharedPtr<MessageDispatch> dispatch(new MessageDispatch(props, scene.get(), true, true, false));
	runtime->queueMessage(dispatch);
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
	  _kwSmackerModifierFactory(this), _editCtrlModifierFactory(this), _gameModifierFactory(this), _whaleMazeModifierFactory(this) {
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
		"AntMaze", "Art", "Balloon", "Eyefollower", "LLava", "Lander", "RoverRace", "SmkJump", "Sub", "SvTown"
	};
	for (const char *name : gameModifierNames)
		registrar->registerPlugInModifier(name, &_gameModifierFactory);

	registrar->registerPlugInModifier("WhaleMaze", &_whaleMazeModifierFactory);
}

} // End of namespace KW

namespace PlugIns {

Common::SharedPtr<PlugIn> createKnowWonder() {
	return Common::SharedPtr<PlugIn>(new KW::KnowWonderPlugIn());
}

} // End of namespace PlugIns

} // End of namespace MTropolis
