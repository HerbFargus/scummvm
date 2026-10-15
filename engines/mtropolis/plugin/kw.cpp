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
#include "common/random.h"

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

	// 10 pixels along an axis, or 5 along the longer axis of a diagonal
	double stepX = 0.0;
	double stepY = 0.0;
	if (dx == 0.0)
		stepY = (dy > 0.0) ? 10.0 : -10.0;
	else if (dy == 0.0)
		stepX = (dx > 0.0) ? 10.0 : -10.0;
	else if (fabs(dx) >= fabs(dy)) {
		stepX = (dx > 0.0) ? 5.0 : -5.0;
		stepY = dy * (5.0 / fabs(dx));
	} else {
		stepY = (dy > 0.0) ? 5.0 : -5.0;
		stepX = dx * (5.0 / fabs(dy));
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
	else if (fabs(dx) >= fabs(dy)) {
		stepX = (dx > 0.0) ? diagonalStep : -diagonalStep;
		stepY = dy * (diagonalStep / fabs(dx));
	} else {
		stepY = (dy > 0.0) ? diagonalStep : -diagonalStep;
		stepX = dx * (diagonalStep / fabs(dy));
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
				// 0x37 toggles between surfaced and dived, -1 surfaces and -2 dives
				bool dive = (node.type == 0x37) ? orca.visible : (node.type == -2);
				if (dive == orca.visible) {
					orca.diving = dive;
					orca.animTicks = 4;
					if (!dive)
						orca.visible = true;
				}
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

EyefollowerModifier::EyefollowerModifier() : _cel(0) {
}

EyefollowerModifier::EyefollowerModifier(const EyefollowerModifier &other)
	: Modifier(other), _startWhen(other._startWhen), _stopWhen(other._stopWhen), _cel(0) {
}

EyefollowerModifier::~EyefollowerModifier() {
	stop();
}

bool EyefollowerModifier::load(const PlugInModifierLoaderContext &context, const Data::KW::GameModifier &data) {
	return loadDlgMgrEvent(_startWhen, data.enableWhen) && loadDlgMgrEvent(_stopWhen, data.disableWhen);
}

bool EyefollowerModifier::respondsToEvent(const Event &evt) const {
	return _startWhen.respondsTo(evt) || _stopWhen.respondsTo(evt) || evt.eventType == EventIDs::kParentDisabled;
}

VThreadState EyefollowerModifier::consumeMessage(Runtime *runtime, const Common::SharedPtr<MessageProperties> &msg) {
	const Event &evt = msg->getEvent();
	if (_stopWhen.respondsTo(evt) || evt.eventType == EventIDs::kParentDisabled) {
		stop();
		return kVThreadReturn;
	}

	if (_startWhen.respondsTo(evt) && !_scheduledEvent) {
		Structural *eyes = getEyes();
		if (!eyes)
			return kVThreadReturn;

		// The eyes' position is only read when starting
		_eyesPos = static_cast<VisualElement *>(eyes)->getGlobalPosition();
		_cel = 1;
		dlgMgrSetInt(runtime, this, eyes, "cel", 1);
		_scheduledEvent = runtime->getScheduler().scheduleMethod<EyefollowerModifier, &EyefollowerModifier::tick>(runtime->getPlayTime() + 83, this);
	}

	return kVThreadReturn;
}

void EyefollowerModifier::disable(Runtime *runtime) {
	stop();
}

void EyefollowerModifier::stop() {
	if (_scheduledEvent) {
		_scheduledEvent->cancel();
		_scheduledEvent.reset();
	}
}

Structural *EyefollowerModifier::getEyes() const {
	Structural *owner = findStructuralOwner();
	if (!owner || owner->getChildren().empty())
		return nullptr;

	Structural *eyes = owner->getChildren()[0].get();
	if (!eyes->isElement() || !static_cast<Element *>(eyes)->isVisual())
		return nullptr;

	return eyes;
}

// Cels go around clockwise from looking straight up
int32 EyefollowerModifier::celForMouse(const Common::Point &eyes, const Common::Point &mouse) {
	double dx = mouse.x - eyes.x;
	double dy = eyes.y - mouse.y;	// Up is positive
	double slope = (dx != 0.0) ? dy / dx : (dy >= 0.0 ? 1000.0 : -1000.0);

	if (eyes.x < mouse.x) {
		if (dy > 0.0)
			return (slope > 1.25) ? 1 : (slope > 0.4) ? 2 : (slope > 0.15) ? 3 : 4;
		return (-slope > 1.25) ? 8 : (-slope > 0.4) ? 7 : (-slope > 0.15) ? 6 : 5;
	}

	if (dx == 0.0)
		return (dy > 0.0) ? 1 : 8;

	if (dy > 0.0)
		return (-slope > 1.25) ? 1 : (-slope > 0.4) ? 16 : (-slope > 0.15) ? 15 : (-slope > 0.05) ? 14 : 13;
	return (slope > 1.25) ? 8 : (slope > 0.4) ? 9 : (slope > 0.15) ? 10 : (slope > 0.05) ? 11 : 12;
}

void EyefollowerModifier::tick(Runtime *runtime) {
	_scheduledEvent = runtime->getScheduler().scheduleMethod<EyefollowerModifier, &EyefollowerModifier::tick>(runtime->getPlayTime() + 83, this);

	Structural *eyes = getEyes();
	if (!eyes)
		return;

	int32 cel = celForMouse(_eyesPos, runtime->getCachedMousePosition());
	if (cel != _cel) {
		_cel = cel;
		dlgMgrSetInt(runtime, this, eyes, "cel", cel);
	}
}

Common::SharedPtr<Modifier> EyefollowerModifier::shallowClone() const {
	return Common::SharedPtr<Modifier>(new EyefollowerModifier(*this));
}

const char *EyefollowerModifier::getDefaultName() const {
	return "Eyefollower";
}

SmkJumpModifier::SmkJumpModifier()
	: _levelsLoaded(false), _runtime(nullptr), _running(false), _initialized(false), _paused(true), _introDlg(false), _level(1),
	  _points(0), _fuel(0), _water(0), _extraPlanes(0), _iconCount(0), _newGame(true), _planeX(0.0), _planeY(0.0), _bank(0),
	  _speed(0.0), _climbOffset(0.0), _intro(false), _introCount(0), _dropHeld(false), _dropCooldown(0), _refillCooldown(0),
	  _fuelTicks(0), _combo(0), _overLake(false), _fuelLow(false), _emptyHintPlayed(false), _fuelHintPlayed(false), _dropTicks(0),
	  _dropX(0.0), _dropY(0.0), _approach(false), _landingTicks(0), _crashTicks(0), _levelDone(false), _progressCountdown(0.0),
	  _progressCel(1) {
	_runway = Object();
}

SmkJumpModifier::~SmkJumpModifier() {
	stopGame();
}

bool SmkJumpModifier::load(const PlugInModifierLoaderContext &context, const Data::KW::GameModifier &data) {
	return loadDlgMgrEvent(_startWhen, data.enableWhen) && loadDlgMgrEvent(_stopWhen, data.disableWhen);
}

// The levels are tables in the plug-in's data section
bool SmkJumpModifier::loadLevels() {
	if (_levelsLoaded)
		return !_levels.empty();

	_levelsLoaded = true;

	Common::File f;
	if (!f.open(Common::Path("mTrop_Resources/smkjump.r32")) && !f.open(Common::Path("smkjump.r32"))) {
		warning("SmkJump: smkjump.r32 from the installed game is needed for the levels");
		return false;
	}

	Common::Array<byte> image;
	image.resize(f.size());
	f.read(&image[0], image.size());

	const uint32 kImageBase = 0x10000000;
	auto offsetOf = [&](uint32 va, uint32 size) -> int64 {
		uint32 offset = va - kImageBase;
		return (offset + size <= image.size()) ? offset : -1;
	};

	bool failed = false;
	auto readS32 = [&](uint32 va) -> int32 {
		int64 offset = offsetOf(va, 4);
		if (offset < 0) {
			failed = true;
			return 0;
		}
		return static_cast<int32>(READ_LE_UINT32(&image[offset]));
	};
	auto readDouble = [&](uint32 va) -> double {
		int64 offset = offsetOf(va, 8);
		if (offset < 0) {
			failed = true;
			return 0.0;
		}
		uint64 bits = READ_LE_UINT64(&image[offset]);
		double value;
		memcpy(&value, &bits, sizeof(value));
		return value;
	};
	auto readObject = [&](uint32 va) -> Object {
		Object obj;
		obj.x = readDouble(va);
		obj.dist = readDouble(va + 8);
		obj.type = readS32(va + 16);
		obj.value = readS32(va + 20);
		return obj;
	};

	static const uint32 kListOffsets[kListCount] = {0x000, 0x5a0, 0x708, 0xac8, 0xdc8, 0xfc0};
	static const uint32 kListSizes[kListCount] = {60, 15, 40, 32, 21, 20};

	Common::Array<Level> levels;
	levels.resize(16);
	for (uint l = 1; l <= 15; l++) {
		Level &level = levels[l];
		uint32 base = 0x1000cf2c + l * 0x11cc;

		level.planeStart = Common::Point(readS32(base), readS32(base + 4));
		level.fuel = static_cast<int32>(readDouble(base + 8));
		level.water = readS32(base + 16);

		for (uint list = 0; list < kListCount; list++) {
			for (uint i = 1; i <= kListSizes[list]; i++)
				level.lists[list].push_back(readObject(base - 4 + kListOffsets[list] + i * 0x18));
		}

		level.runway = readObject(base - 4 + 0x11b8);
	}

	// Level 1 starts the plane at (320, 240) with 20 fuel
	if (failed || levels[1].planeStart != Common::Point(320, 240) || levels[1].fuel != 20) {
		warning("SmkJump: smkjump.r32 isn't the expected version");
		return false;
	}

	_levels.swap(levels);
	return true;
}

bool SmkJumpModifier::respondsToEvent(const Event &evt) const {
	return _startWhen.respondsTo(evt) || _stopWhen.respondsTo(evt) || evt.eventType == EventIDs::kParentDisabled
		|| evt.eventType == EventIDs::kMouseDown || evt.eventType == EventIDs::kMouseUp;
}

VThreadState SmkJumpModifier::consumeMessage(Runtime *runtime, const Common::SharedPtr<MessageProperties> &msg) {
	const Event &evt = msg->getEvent();

	if (_stopWhen.respondsTo(evt) || evt.eventType == EventIDs::kParentDisabled) {
		stopGame();
		return kVThreadReturn;
	}

	if (_startWhen.respondsTo(evt)) {
		_runtime = runtime;
		_newGame = true;
		_introDlg = true;
		_emptyHintPlayed = false;
		_fuelHintPlayed = false;
		setupLevel(runtime, 1);
		_paused = true;

		if (!_running) {
			_running = true;
			// The original ticks every 71 milliseconds
			_scheduledEvent = runtime->getScheduler().scheduleMethod<SmkJumpModifier, &SmkJumpModifier::tick>(runtime->getPlayTime() + 71, this);
		}
		return kVThreadReturn;
	}

	if (evt.eventType == EventIDs::kMouseDown) {
		if (_paused && _initialized)
			_paused = false;
		else
			_dropHeld = true;

		// A click skips the intro
		if (_intro && !_introDlg)
			_introCount = 0;
	} else if (evt.eventType == EventIDs::kMouseUp)
		_dropHeld = false;

	return kVThreadReturn;
}

void SmkJumpModifier::disable(Runtime *runtime) {
	stopGame();
}

void SmkJumpModifier::stopGame() {
	_running = false;
	if (_scheduledEvent) {
		_scheduledEvent->cancel();
		_scheduledEvent.reset();
	}
}

bool SmkJumpModifier::readAttribute(MiniscriptThread *thread, DynamicValue &result, const Common::String &attrib) {
	if (attrib == "paused") {
		result.setBool(_paused);
		return true;
	} else if (attrib == "gamelevel") {
		result.setInt(_level);
		return true;
	} else if (attrib == "points") {
		result.setInt(_points);
		return true;
	}

	return Modifier::readAttribute(thread, result, attrib);
}

MiniscriptInstructionOutcome SmkJumpModifier::writeRefAttribute(MiniscriptThread *thread, DynamicValueWriteProxy &result, const Common::String &attrib) {
	WritableAttribute writable;
	if (attrib == "paused")
		writable = kAttribPaused;
	else if (attrib == "gamelevel")
		writable = kAttribGameLevel;
	else if (attrib == "introdlg")
		writable = kAttribIntroDlg;
	else
		return Modifier::writeRefAttribute(thread, result, attrib);

	result.pod.ifc = DynamicValueWriteInterfaceGlue<WriteProxy>::getInstance();
	result.pod.objectRef = this;
	result.pod.ptrOrOffset = writable;
	return kMiniscriptInstructionOutcomeContinue;
}

MiniscriptInstructionOutcome SmkJumpModifier::WriteProxy::write(MiniscriptThread *thread, const DynamicValue &assigned, void *objectRef, uintptr ptrOrOffset) {
	SmkJumpModifier *self = static_cast<SmkJumpModifier *>(objectRef);

	// Variables are passed by reference
	DynamicValue value = assigned;
	if (value.getType() == DynamicValueTypes::kObject) {
		Common::SharedPtr<RuntimeObject> obj = value.getObject().object.lock();
		if (obj && obj->isModifier() && static_cast<Modifier *>(obj.get())->isVariable())
			static_cast<VariableModifier *>(obj.get())->varGetValue(value);
	}

	if (ptrOrOffset == kAttribGameLevel) {
		int32 level = 0;
		if (!value.roundToInt(level))
			return kMiniscriptInstructionOutcomeFailed;
		if (level >= 1 && level < 16)
			self->setupLevel(thread->getRuntime(), level);
		return kMiniscriptInstructionOutcomeContinue;
	}

	bool flag = false;
	if (value.getType() == DynamicValueTypes::kBoolean)
		flag = value.getBool();
	else if (value.getType() == DynamicValueTypes::kInteger)
		flag = (value.getInt() != 0);
	else
		return kMiniscriptInstructionOutcomeFailed;

	if (ptrOrOffset == kAttribPaused) {
		// Pausing only takes effect once a level is set up
		if (self->_initialized || !flag)
			self->_paused = flag;
	} else
		self->_introDlg = flag;

	return kMiniscriptInstructionOutcomeContinue;
}

MiniscriptInstructionOutcome SmkJumpModifier::WriteProxy::refAttrib(MiniscriptThread *thread, DynamicValueWriteProxy &proxy, void *objectRef, uintptr ptrOrOffset, const Common::String &attrib) {
	return kMiniscriptInstructionOutcomeFailed;
}

MiniscriptInstructionOutcome SmkJumpModifier::WriteProxy::refAttribIndexed(MiniscriptThread *thread, DynamicValueWriteProxy &proxy, void *objectRef, uintptr ptrOrOffset, const Common::String &attrib, const DynamicValue &index) {
	return kMiniscriptInstructionOutcomeFailed;
}

// Children of the owner element: 1 plane, 2 runway, 3-8 object lists, 9 water meter, 10 fuel gauge,
// 12 extra plane icons, 13 crash, 14 shadow, 15 water drop, 16 landing, 17 level bars, 18 fuel light,
// 19 progress, 20 water light, 21 level banner
Structural *SmkJumpModifier::child(int index) const {
	Structural *owner = findStructuralOwner();
	if (!owner || index < 1 || index > static_cast<int>(owner->getChildren().size()))
		return nullptr;
	return owner->getChildren()[index - 1].get();
}

Structural *SmkJumpModifier::grandchild(int index, int subIndex) const {
	Structural *container = child(index);
	if (!container || subIndex < 1 || subIndex > static_cast<int>(container->getChildren().size()))
		return nullptr;
	return container->getChildren()[subIndex - 1].get();
}

Structural *SmkJumpModifier::listElement(int list, uint index) const {
	return grandchild(3 + list, index + 1);
}

void SmkJumpModifier::place(Runtime *runtime, Structural *element, int32 x, int32 y) {
	if (!element || !element->isElement() || !static_cast<Element *>(element)->isVisual())
		return;

	// Placed centered on the point
	const Common::Rect &rect = static_cast<VisualElement *>(element)->getRelativeRect();
	DynamicValue pos;
	pos.setPoint(Common::Point(x - rect.width() / 2, y - rect.height() / 2));
	dlgMgrWriteAttribute(runtime, this, element, "position", pos);
}

void SmkJumpModifier::hide(Runtime *runtime, Structural *element) {
	if (!element)
		return;

	DynamicValue pos;
	pos.setPoint(Common::Point(-1, 600));
	dlgMgrWriteAttribute(runtime, this, element, "position", pos);
}

void SmkJumpModifier::setInt(Runtime *runtime, Structural *element, const char *attrib, int32 value) {
	dlgMgrSetInt(runtime, this, element, attrib, value);
}

void SmkJumpModifier::setBool(Runtime *runtime, Structural *element, const char *attrib, bool value) {
	dlgMgrSetBool(runtime, this, element, attrib, value);
}

void SmkJumpModifier::sendMessage(Runtime *runtime, const char *messageName) {
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

void SmkJumpModifier::placePlane(Runtime *runtime) {
	int32 x = static_cast<int32>(round(_planeX));
	int32 y = static_cast<int32>(round(_planeY));
	place(runtime, child(1), x, y);
	place(runtime, child(14), x + 18, y + 15);
}

void SmkJumpModifier::setupLevel(Runtime *runtime, int32 levelNum) {
	_runtime = runtime;
	if (!loadLevels() || levelNum < 1 || levelNum > 15)
		return;

	const Level &level = _levels[levelNum];
	_level = levelNum;

	if (levelNum <= 5) {
		_speed = 2.6;
		_climbOffset = 0.2;
		_iconCount = 4;
	} else if (levelNum <= 10) {
		_speed = 3.0;
		_climbOffset = 0.4;
		_iconCount = 3;
	} else {
		_speed = 3.9;
		_climbOffset = 0.7;
		_iconCount = 2;
	}

	_planeX = level.planeStart.x;
	_planeY = level.planeStart.y;
	_bank = 0;
	_fuel = level.fuel;
	_water = level.water;
	_dropCooldown = 6;
	_refillCooldown = 16;
	_fuelTicks = 25;
	_combo = 0;
	_dropTicks = 0;
	_approach = false;
	_landingTicks = 0;
	_crashTicks = 0;
	_levelDone = false;
	_overLake = false;
	_fuelLow = false;
	_dropHeld = false;
	_intro = true;
	_introCount = 28;
	_paused = true;

	for (uint list = 0; list < kListCount; list++) {
		_lists[list] = level.lists[list];
		for (uint i = 0; i < _lists[list].size(); i++)
			hide(runtime, listElement(list, i));
	}

	// Fires burn in a loop until they're put out
	_fireLit.clear();
	_fireLit.resize(_lists[kListFires].size());
	for (uint i = 0; i < _lists[kListFires].size(); i++) {
		Structural *fire = listElement(kListFires, i);
		bool lit = (_lists[kListFires][i].dist > 0.0);
		_fireLit[i] = lit;
		setInt(runtime, fire, "cel", 1);
		DynamicValue range;
		range.setString("loop");
		dlgMgrWriteAttribute(runtime, this, fire, "range", range);
		setBool(runtime, fire, "paused", !lit);
	}

	_runway = level.runway;
	hide(runtime, child(2));
	_progressCel = 1;
	_progressCountdown = (_runway.dist - 480.0) * 0.05;
	setInt(runtime, child(19), "cel", 1);

	placePlane(runtime);
	setInt(runtime, child(1), "cel", 1);
	setInt(runtime, child(14), "cel", 1);
	hide(runtime, child(13));
	hide(runtime, child(15));
	hide(runtime, child(16));

	setInt(runtime, child(9), "cel", _water + 1);
	setInt(runtime, child(10), "cel", _fuel);
	setBool(runtime, child(18), "paused", true);
	setInt(runtime, child(18), "cel", 1);
	setInt(runtime, child(20), "cel", 1);

	// Difficulty bars, and the level banner and badge
	int bar = (levelNum < 5) ? 1 : (levelNum < 10) ? 2 : (levelNum < 15) ? 3 : 0;
	for (int i = 1; i <= 3; i++) {
		if (i == bar) {
			DynamicValue pos;
			pos.setPoint(Common::Point(255, 13));
			dlgMgrWriteAttribute(runtime, this, grandchild(17, i), "position", pos);
		} else
			hide(runtime, grandchild(17, i));
	}

	DynamicValue bannerPos;
	bannerPos.setPoint(Common::Point(151, 160));
	dlgMgrWriteAttribute(runtime, this, grandchild(21, 1), "position", bannerPos);
	setInt(runtime, grandchild(21, 2), "cel", levelNum % 5);
	DynamicValue badgePos;
	badgePos.setPoint(Common::Point(391, 198));
	dlgMgrWriteAttribute(runtime, this, grandchild(21, 2), "position", badgePos);

	if (_newGame) {
		_points = 0;
		sendMessage(runtime, "UpdateScore");

		static const int16 kIconPositions[4][2] = {{521, 76}, {550, 90}, {521, 108}, {551, 122}};
		_extraPlanes = 0;
		for (int i = 0; i < 4; i++) {
			Structural *icon = grandchild(12, i + 1);
			if (i < _iconCount) {
				DynamicValue pos;
				pos.setPoint(Common::Point(kIconPositions[i][0], kIconPositions[i][1]));
				dlgMgrWriteAttribute(runtime, this, icon, "position", pos);
				_extraPlanes++;
			} else
				hide(runtime, icon);
		}
		_newGame = false;
	}

	_initialized = true;
}

void SmkJumpModifier::tick(Runtime *runtime) {
	_scheduledEvent.reset();
	if (!_running)
		return;

	_scheduledEvent = runtime->getScheduler().scheduleMethod<SmkJumpModifier, &SmkJumpModifier::tick>(runtime->getPlayTime() + 71, this);

	if (!_initialized)
		return;

	// After landing, the fuel left is counted into the score
	if (_levelDone) {
		if (_fuel > 0) {
			_points += 10;
			sendMessage(runtime, "UpdateScore");
			setInt(runtime, child(10), "cel", _fuel);
			_fuel--;
			return;
		}

		_levelDone = false;
		_paused = true;
		sendMessage(runtime, "levelwon");
		if (_level < 15 && _level != 5 && _level != 10)
			setupLevel(runtime, _level + 1);
		else
			sendMessage(runtime, "YouWin");
		return;
	}

	if (_intro && !_introDlg) {
		if (_introCount-- > 0)
			return;

		_intro = false;
		_paused = false;
		hide(runtime, grandchild(21, 1));
		hide(runtime, grandchild(21, 2));
		return;
	}

	// The game waits while the mouse is outside the play area
	const Common::Point &mouse = runtime->getCachedMousePosition();
	if (_paused || _introDlg || mouse.y < 11 || mouse.y > 429 || mouse.x < 11 || mouse.x > 629)
		return;

	if (tickStates(runtime))
		return;

	steer(runtime, mouse);

	if (_dropCooldown > 0)
		_dropCooldown--;
	if (_refillCooldown > 0)
		_refillCooldown--;
	if (_combo > 0)
		_combo--;

	tickFuel(runtime);
	if (_crashTicks > 0)
		return;

	// The progress bar fills as the runway approaches
	if (_progressCountdown <= 0.0) {
		if (_progressCel < 20)
			setInt(runtime, child(19), "cel", ++_progressCel);
		_progressCountdown = (_levels[_level].runway.dist - 480.0) * 0.05;
	} else
		_progressCountdown -= _speed;

	scrollObjects(runtime);
	checkCollisions(runtime);
}

// The drop, approach, landing and crash animations. Returns true if the tick is over.
bool SmkJumpModifier::tickStates(Runtime *runtime) {
	if (_dropTicks > 0) {
		Structural *drop = child(15);
		if (++_dropTicks >= 8) {
			_dropTicks = 0;
			hide(runtime, drop);
		} else {
			_dropY += _speed * 1.5;
			place(runtime, drop, static_cast<int32>(_dropX), static_cast<int32>(_dropY));
			setInt(runtime, drop, "cel", _dropTicks);
		}
	}

	if (_approach) {
		if (_planeX != _runway.x) {
			_planeX = _runway.x;
			place(runtime, child(1), static_cast<int32>(_runway.x) + 15, static_cast<int32>(_planeY));
			place(runtime, child(14), static_cast<int32>(_runway.x) + 33, static_cast<int32>(_planeY) + 15);
			if (_bank != 0) {
				_bank = 0;
				setInt(runtime, child(1), "cel", 1);
				setInt(runtime, child(14), "cel", 1);
			}
		}

		if (fabs(_planeX - _runway.x) < 20.0 && fabs(_planeY - (480.0 - _runway.dist)) < 40.0) {
			_approach = false;
			_landingTicks = 1;
			_speed = 3.0;
			hide(runtime, child(1));
			hide(runtime, child(14));
			place(runtime, child(16), static_cast<int32>(_planeX) + 15, static_cast<int32>(_planeY));
			setInt(runtime, child(16), "cel", 1);
		}
	}

	if (_landingTicks > 0) {
		if (++_landingTicks >= 26) {
			_landingTicks = 0;
			hide(runtime, child(16));
			_newGame = (_level > 14 || _level == 5 || _level == 10);
			_levelDone = true;
			return true;
		}
		if (_landingTicks < 6)
			setInt(runtime, child(16), "cel", _landingTicks);
		return true;
	}

	if (_crashTicks > 0) {
		if (_crashTicks == 1) {
			hide(runtime, child(1));
			hide(runtime, child(14));
			place(runtime, child(13), static_cast<int32>(_planeX) + 10, static_cast<int32>(_planeY) + 2);
			sendMessage(runtime, "fuelSFXOff");
		}

		setInt(runtime, child(13), "cel", _crashTicks);
		if (++_crashTicks >= 28) {
			_crashTicks = 0;
			hide(runtime, child(13));
			if (!_newGame)
				setupLevel(runtime, _level);
			else {
				_paused = true;
				sendMessage(runtime, "GameOver");
			}
		}
		return true;
	}

	// The runway scrolls in, then the plane lines up with it
	if (_runway.dist > 0.0) {
		_runway.dist -= _speed;
		if (_runway.dist < 490.0 && _runway.dist > 0.0) {
			place(runtime, child(2), static_cast<int32>(_runway.x), 480 - static_cast<int32>(_runway.dist + 0.5));
			if (!_approach && _landingTicks == 0) {
				_approach = true;
				_speed = 6.0;
				sendMessage(runtime, "fuelSFXOff");
				setBool(runtime, child(18), "paused", true);
				setInt(runtime, child(18), "cel", 1);
			}
		}
	}

	return false;
}

void SmkJumpModifier::steer(Runtime *runtime, const Common::Point &mouse) {
	if (_approach)
		return;

	double oldX = _planeX;
	double oldY = _planeY;
	int32 oldBank = _bank;

	int32 rx = static_cast<int32>(round(_planeX));
	int32 ry = static_cast<int32>(round(_planeY));

	if (rx < mouse.x && rx < 430) {
		double d = (mouse.x - _planeX) / 1.8;
		_planeX += d;
		if (_planeX > 430.0) {
			_planeX = 430.0;
			_bank = 0;
		} else
			_bank = (d > 5.0) ? 2 : 0;
	} else if (rx > mouse.x && rx > 211) {
		double d = (_planeX - mouse.x) / 1.8;
		_planeX -= d;
		if (_planeX < 211.0) {
			_planeX = 211.0;
			_bank = 0;
		} else
			_bank = (d > 5.0) ? 1 : 0;
	} else
		_bank = 0;

	if (ry > mouse.y && ry > 146) {
		_planeY -= (_planeY - mouse.y) / 5.0;
		if (_planeY < 146.0)
			_planeY = 146.0;
	} else if (ry < mouse.y && ry < 345) {
		_planeY += MIN((mouse.y - _planeY) / 12.0, _speed - _climbOffset);
		if (_planeY > 345.0)
			_planeY = 345.0;
	}

	if (_planeX != oldX || _planeY != oldY)
		placePlane(runtime);

	if (_bank != oldBank) {
		setInt(runtime, child(1), "cel", _bank + 1);
		setInt(runtime, child(14), "cel", _bank + 1);
	}
}

void SmkJumpModifier::tickFuel(Runtime *runtime) {
	if (--_fuelTicks >= 1 || _approach)
		return;

	_fuelTicks = 25;
	if (_fuel < 2) {
		loseAPlane(runtime);
		return;
	}

	_fuel--;
	setInt(runtime, child(10), "cel", _fuel);
	if (_fuel == 5) {
		setBool(runtime, child(18), "paused", false);
		sendMessage(runtime, "fuelSFXOn");
		if (!_fuelHintPlayed) {
			_fuelHintPlayed = true;
			sendMessage(runtime, "PlaySFX8");
		}
	}
}

// Crashing uses up an extra plane, or ends the game without one
void SmkJumpModifier::loseAPlane(Runtime *runtime) {
	if (_extraPlanes > 1) {
		_extraPlanes--;
		hide(runtime, grandchild(12, _extraPlanes + 1));
		_newGame = false;
	} else {
		_extraPlanes = 0;
		hide(runtime, grandchild(12, 1));
		_newGame = true;
	}

	sendMessage(runtime, "fuelSFXOff");
	_crashTicks = 1;
}

void SmkJumpModifier::startDrop(Runtime *runtime) {
	_water--;
	setInt(runtime, child(9), "cel", _water + 1);
	_dropCooldown = 6;
	_dropTicks = 1;
	_dropX = _planeX;
	_dropY = _planeY + 14.0;
	place(runtime, child(15), static_cast<int32>(_dropX), static_cast<int32>(_dropY));
	setInt(runtime, child(15), "cel", 1);
}

void SmkJumpModifier::scrollObjects(Runtime *runtime) {
	for (uint list = 0; list < kListCount; list++) {
		Common::Array<Object> &objects = _lists[list];
		for (uint i = 0; i < objects.size(); i++) {
			Object &obj = objects[i];
			if (obj.dist <= 0.0)
				continue;

			obj.dist -= _speed;
			if (obj.dist >= 490.0)
				continue;

			Structural *element = listElement(list, i);
			if (obj.dist <= 0.0) {
				hide(runtime, element);
				obj.dist = -100.0;
			} else
				place(runtime, element, static_cast<int32>(obj.x), 480 - static_cast<int32>(obj.dist + 0.5));
		}
	}
}

// Returns true if the plane crashed
bool SmkJumpModifier::checkCollisions(Runtime *runtime) {
	bool busy = (_approach || _landingTicks > 0 || _crashTicks > 0);

	// Lakes refill the tank
	bool overLake = false;
	for (const Object &lake : _lists[kListLakes]) {
		if (lake.dist <= 0.0 || lake.dist >= 490.0)
			continue;

		double halfW = (lake.type == 5) ? 36.0 : (lake.type == 6) ? 37.0 : 25.0;
		double halfH = (lake.type == 5) ? 32.0 : (lake.type == 6) ? 38.0 : 21.0;
		double ly = 480.0 - lake.dist;
		if (fabs(_planeX - lake.x) < halfW && fabs(_planeY - ly) < halfH) {
			overLake = true;
			if (_dropHeld && _refillCooldown <= 0 && _water < 8) {
				_water = MIN<int32>(8, _water + lake.value);
				_refillCooldown = 16;
				setInt(runtime, child(9), "cel", _water + 1);
				sendMessage(runtime, "PlaySFX3");
			}
		}
	}

	if (overLake != _overLake) {
		_overLake = overLake;
		setInt(runtime, child(20), "cel", overLake ? 2 : 1);
	}

	// Water drops on fires
	if (_dropHeld && _dropCooldown <= 0 && !busy) {
		if (_water > 0) {
			bool hit = false;
			for (uint i = 0; i < _lists[kListFires].size(); i++) {
				Object &fire = _lists[kListFires][i];
				if (!_fireLit[i] || fire.dist <= 0.0 || fire.dist >= 490.0)
					continue;

				if (fabs(_planeX - fire.x) < 10.0 && fabs(_planeY - (480.0 - fire.dist + 0.5)) < 22.0) {
					hit = true;
					_fireLit[i] = false;
					_points += (_combo > 0) ? 75 : 25;
					_combo = 28;
					sendMessage(runtime, "UpdateScore");

					Structural *fireElement = listElement(kListFires, i);
					setBool(runtime, fireElement, "paused", true);
					setInt(runtime, fireElement, "cel", 9);
					startDrop(runtime);
					sendMessage(runtime, "PlaySFX1");
					if (_water == 0 && !_emptyHintPlayed) {
						_emptyHintPlayed = true;
						sendMessage(runtime, "PlaySFX9");
					}
					break;
				}
			}

			if (!hit && !overLake) {
				startDrop(runtime);
				sendMessage(runtime, "PlaySFX2");
			}
		} else if (!overLake) {
			_dropCooldown = 6;
			sendMessage(runtime, "PlaySFX10");
		}
	}

	if (busy)
		return false;

	// Obstructions crash the plane
	for (const Object &obj : _lists[kListObstructions]) {
		if (obj.dist <= 0.0 || obj.dist >= 490.0)
			continue;

		double oy = 480.0 - obj.dist;
		auto inBox = [&](double cx, double halfW, double cy, double halfH) {
			return fabs(_planeX - cx) < halfW && fabs(_planeY - cy) < halfH;
		};

		bool crashed = false;
		switch (obj.type) {
		case 7:
			crashed = inBox(obj.x, 37.0, oy - 5.0, 31.0);
			break;
		case 8:
			crashed = inBox(obj.x - 6.0, 45.0, oy - 13.0, 46.0) || inBox(obj.x - 14.0, 28.0, oy - 20.0, 32.0) || inBox(obj.x - 10.0, 27.0, oy - 19.0, 29.0);
			break;
		case 9:
			crashed = inBox(obj.x - 14.0, 28.0, oy - 20.0, 32.0) || inBox(obj.x - 10.0, 27.0, oy - 19.0, 29.0);
			break;
		case 10:
			crashed = inBox(obj.x - 10.0, 27.0, oy - 19.0, 29.0);
			break;
		default:
			break;
		}

		if (crashed) {
			sendMessage(runtime, "PlaySFX5");
			loseAPlane(runtime);
			return true;
		}
	}

	// Bonuses
	for (uint i = 0; i < _lists[kListBonuses].size(); i++) {
		Object &bonus = _lists[kListBonuses][i];
		if (bonus.dist <= 0.0 || bonus.dist >= 490.0)
			continue;

		double by = 480.0 - bonus.dist;
		bool taken = false;
		if (bonus.type == 11 && fabs(_planeX - bonus.x) < 15.0 && fabs(_planeY - by) < 15.0) {
			_points += 50;
			sendMessage(runtime, "UpdateScore");
			sendMessage(runtime, "PlaySFX6");
			taken = true;
		} else if (bonus.type == 12 && fabs(_planeX - (bonus.x - 5.0)) < 24.0 && fabs(_planeY - (by - 13.0)) < 24.0) {
			_fuel = MIN<int32>(20, _fuel + 6);
			setBool(runtime, child(18), "paused", true);
			setInt(runtime, child(18), "cel", 1);
			sendMessage(runtime, "fuelSFXOff");
			setInt(runtime, child(10), "cel", _fuel);
			sendMessage(runtime, "PlaySFX4");
			taken = true;
		} else if (bonus.type == 13 && _extraPlanes < 5 && fabs(_planeX - bonus.x) < 15.0 && fabs(_planeY - by) < 15.0) {
			static const int16 kIconPositions[4][2] = {{521, 76}, {550, 90}, {521, 108}, {551, 122}};
			if (_extraPlanes < 4) {
				DynamicValue pos;
				pos.setPoint(Common::Point(kIconPositions[_extraPlanes][0], kIconPositions[_extraPlanes][1]));
				dlgMgrWriteAttribute(runtime, this, grandchild(12, _extraPlanes + 1), "position", pos);
			}
			_extraPlanes++;
			sendMessage(runtime, "PlaySFX7");
			taken = true;
		}

		if (taken) {
			hide(runtime, listElement(kListBonuses, i));
			bonus.dist = -100.0;
		}
	}

	return false;
}

Common::SharedPtr<Modifier> SmkJumpModifier::shallowClone() const {
	Common::SharedPtr<SmkJumpModifier> clone(new SmkJumpModifier(*this));
	clone->_scheduledEvent.reset();
	clone->_running = false;
	clone->_initialized = false;
	return clone;
}

const char *SmkJumpModifier::getDefaultName() const {
	return "SmkJump";
}

BalloonModifier::BalloonModifier()
	: _levelsLoaded(false), _runtime(nullptr), _running(false), _initialized(false), _paused(true), _level(1), _tier(0), _padShown(false),
	  _kidsNeeded(0), _points(0), _lives(5), _kids(0), _baseSpeed(2.7), _speed(2.7), _wind(2), _windCountdown(0), _countingDown(false),
	  _countdown(0), _flyState(kFlyNormal), _stateTicks(0), _bx(0.0), _by(0.0), _mouseHeld(false), _keyboardMode(false), _keyUp(false),
	  _keyDown(false), _upSFX(false), _downSFX(false), _planeSFX(false), _heliSFX(false), _lightningSFX(false), _birdTimer(0),
	  _boltOn(false), _boltTimer(0) {
	_pad = Object();
}

BalloonModifier::~BalloonModifier() {
	stopGame();
}

bool BalloonModifier::load(const PlugInModifierLoaderContext &context, const Data::KW::GameModifier &data) {
	return loadDlgMgrEvent(_startWhen, data.enableWhen) && loadDlgMgrEvent(_stopWhen, data.disableWhen);
}

// The levels and flyer motions are tables in the plug-in's data section
bool BalloonModifier::loadLevels() {
	if (_levelsLoaded)
		return !_levels.empty();

	_levelsLoaded = true;

	Common::File f;
	if (!f.open(Common::Path("mTrop_Resources/balloon.r32")) && !f.open(Common::Path("balloon.r32"))) {
		warning("Balloon: balloon.r32 from the installed game is needed for the levels");
		return false;
	}

	Common::Array<byte> image;
	image.resize(f.size());
	f.read(&image[0], image.size());

	bool failed = false;
	auto readS32 = [&](uint32 va) -> int32 {
		uint32 offset = va - 0x10000000;
		if (offset + 4 > image.size()) {
			failed = true;
			return 0;
		}
		return static_cast<int32>(READ_LE_UINT32(&image[offset]));
	};
	auto readDouble = [&](uint32 va) -> double {
		uint32 offset = va - 0x10000000;
		if (offset + 8 > image.size()) {
			failed = true;
			return 0.0;
		}
		uint64 bits = READ_LE_UINT64(&image[offset]);
		double value;
		memcpy(&value, &bits, sizeof(value));
		return value;
	};
	auto readObject = [&](uint32 va) -> Object {
		Object obj;
		obj.x = readDouble(va);
		obj.y = readDouble(va + 8);
		obj.type = readS32(va + 0x18);
		obj.sub = readS32(va + 0x1c);
		return obj;
	};

	static const uint32 kListSizes[kListCount] = {80, 33, 65, 54, 30};

	Common::Array<Level> levels;
	levels.resize(16);
	for (uint l = 1; l <= 15; l++) {
		Level &level = levels[l];
		uint32 base = 0x1000e2dc + l * 0x20ec;

		level.start = Common::Point(readS32(base), readS32(base + 4));
		level.kidsNeeded = readS32(base + 8);

		uint32 record = base + 0xc;
		for (uint list = 0; list < kListCount; list++) {
			for (uint i = 0; i < kListSizes[list]; i++) {
				level.lists[list].push_back(readObject(record));
				record += 32;
			}
		}

		level.pad = readObject(base + 0x20cc);
	}

	// Patterns 1-5 are consecutive, pattern 6 is separate
	for (uint p = 1; p <= 6; p++) {
		uint32 table = (p == 6) ? 0x100100c0 : 0x10010140 + 0x80 * (p - 1);
		for (uint i = 0; i < 8; i++) {
			MotionStep step;
			step.dx = readDouble(table + i * 16);
			step.dy = readDouble(table + i * 16 + 8);
			_motion[p].push_back(step);
		}
	}

	// Level 1 starts at (200, 170), needs 3 kids and lands at x 2100
	if (failed || levels[1].start != Common::Point(200, 170) || levels[1].kidsNeeded != 3 || levels[1].pad.x != 2100.0) {
		warning("Balloon: balloon.r32 isn't the expected version");
		return false;
	}

	_levels.swap(levels);
	return true;
}

bool BalloonModifier::respondsToEvent(const Event &evt) const {
	return _startWhen.respondsTo(evt) || _stopWhen.respondsTo(evt) || evt.eventType == EventIDs::kParentDisabled
		|| evt.eventType == EventIDs::kMouseDown || evt.eventType == EventIDs::kMouseUp;
}

VThreadState BalloonModifier::consumeMessage(Runtime *runtime, const Common::SharedPtr<MessageProperties> &msg) {
	const Event &evt = msg->getEvent();

	if (_stopWhen.respondsTo(evt) || evt.eventType == EventIDs::kParentDisabled) {
		stopGame();
		return kVThreadReturn;
	}

	if (_startWhen.respondsTo(evt)) {
		_runtime = runtime;
		newGame(runtime, 1);

		if (!_running) {
			_running = true;
			_keySignaller = runtime->getProject()->notifyOnKeyboardEvent(this);
			_scheduledEvent = runtime->getScheduler().scheduleMethod<BalloonModifier, &BalloonModifier::tick>(runtime->getPlayTime() + 83, this);
		}
		return kVThreadReturn;
	}

	if (evt.eventType == EventIDs::kMouseDown) {
		// A click skips the countdown
		if (_countingDown)
			_countdown = 0;
		else {
			_mouseHeld = true;
			_keyboardMode = false;
		}
	} else if (evt.eventType == EventIDs::kMouseUp) {
		_mouseHeld = false;
		_keyboardMode = false;
	}

	return kVThreadReturn;
}

void BalloonModifier::onKeyboardEvent(Runtime *runtime, const KeyboardInputEvent &keyEvt) {
	Common::KeyCode key = keyEvt.getKeyState().keycode;
	if (key != Common::KEYCODE_UP && key != Common::KEYCODE_DOWN)
		return;

	bool down = (keyEvt.getKeyEventType() == Common::EVENT_KEYDOWN);
	if (down)
		_keyboardMode = true;

	if (key == Common::KEYCODE_UP)
		_keyUp = down;
	else
		_keyDown = down;
}

void BalloonModifier::disable(Runtime *runtime) {
	stopGame();
}

void BalloonModifier::stopGame() {
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

bool BalloonModifier::readAttribute(MiniscriptThread *thread, DynamicValue &result, const Common::String &attrib) {
	if (attrib == "paused") {
		result.setBool(_paused);
		return true;
	} else if (attrib == "gamelevel") {
		result.setInt(_level);
		return true;
	} else if (attrib == "points") {
		result.setInt(_points);
		return true;
	}

	return Modifier::readAttribute(thread, result, attrib);
}

MiniscriptInstructionOutcome BalloonModifier::writeRefAttribute(MiniscriptThread *thread, DynamicValueWriteProxy &result, const Common::String &attrib) {
	WritableAttribute writable;
	if (attrib == "paused")
		writable = kAttribPaused;
	else if (attrib == "gamelevel")
		writable = kAttribGameLevel;
	else if (attrib == "introdlg")
		writable = kAttribIntroDlg;
	else if (attrib == "goup" || attrib == "godown" || attrib == "stopballoon")
		writable = kAttribIgnored;	// Debugging aids
	else
		return Modifier::writeRefAttribute(thread, result, attrib);

	result.pod.ifc = DynamicValueWriteInterfaceGlue<WriteProxy>::getInstance();
	result.pod.objectRef = this;
	result.pod.ptrOrOffset = writable;
	return kMiniscriptInstructionOutcomeContinue;
}

MiniscriptInstructionOutcome BalloonModifier::WriteProxy::write(MiniscriptThread *thread, const DynamicValue &assigned, void *objectRef, uintptr ptrOrOffset) {
	BalloonModifier *self = static_cast<BalloonModifier *>(objectRef);

	// Variables are passed by reference
	DynamicValue value = assigned;
	if (value.getType() == DynamicValueTypes::kObject) {
		Common::SharedPtr<RuntimeObject> obj = value.getObject().object.lock();
		if (obj && obj->isModifier() && static_cast<Modifier *>(obj.get())->isVariable())
			static_cast<VariableModifier *>(obj.get())->varGetValue(value);
	}

	if (ptrOrOffset == kAttribGameLevel) {
		int32 level = 0;
		if (!value.roundToInt(level))
			return kMiniscriptInstructionOutcomeFailed;
		if (level >= 1 && level < 16)
			self->newGame(thread->getRuntime(), level);
		return kMiniscriptInstructionOutcomeContinue;
	}

	if (ptrOrOffset == kAttribPaused) {
		bool flag = false;
		if (value.getType() == DynamicValueTypes::kBoolean)
			flag = value.getBool();
		else if (value.getType() == DynamicValueTypes::kInteger)
			flag = (value.getInt() != 0);
		else
			return kMiniscriptInstructionOutcomeFailed;

		// Unpausing only takes effect once a level is set up
		if (self->_initialized || flag)
			self->_paused = flag;
	}

	return kMiniscriptInstructionOutcomeContinue;
}

MiniscriptInstructionOutcome BalloonModifier::WriteProxy::refAttrib(MiniscriptThread *thread, DynamicValueWriteProxy &proxy, void *objectRef, uintptr ptrOrOffset, const Common::String &attrib) {
	return kMiniscriptInstructionOutcomeFailed;
}

MiniscriptInstructionOutcome BalloonModifier::WriteProxy::refAttribIndexed(MiniscriptThread *thread, DynamicValueWriteProxy &proxy, void *objectRef, uintptr ptrOrOffset, const Common::String &attrib, const DynamicValue &index) {
	return kMiniscriptInstructionOutcomeFailed;
}

// Children of the owner element: 1 balloon per tier, 2 banner and badge, 3 trees, 4 mountains,
// 5 buildings, 6 flyers, 7 lightning bolt, 8 kids, 9 midground, 10 lives, 11 windsock, 12 pad,
// 13 airship, 14 fireworks, 15 crash per tier
Structural *BalloonModifier::child(int index) const {
	Structural *owner = findStructuralOwner();
	if (!owner || index < 1 || index > static_cast<int>(owner->getChildren().size()))
		return nullptr;
	return owner->getChildren()[index - 1].get();
}

Structural *BalloonModifier::grandchild(int index, int subIndex) const {
	Structural *container = child(index);
	if (!container || subIndex < 1 || subIndex > static_cast<int>(container->getChildren().size()))
		return nullptr;
	return container->getChildren()[subIndex - 1].get();
}

Structural *BalloonModifier::listElement(int list, uint index) const {
	static const int kContainers[kListCount] = {3, 4, 5, 6, 8};
	return grandchild(kContainers[list], index + 1);
}

Structural *BalloonModifier::balloonElement() const {
	return grandchild(1, _tier + 1);
}

void BalloonModifier::place(Runtime *runtime, Structural *element, int32 x, int32 y) {
	if (!element || !element->isElement() || !static_cast<Element *>(element)->isVisual())
		return;

	// Placed centered on the point
	const Common::Rect &rect = static_cast<VisualElement *>(element)->getRelativeRect();
	placeTopLeft(runtime, element, x - rect.width() / 2, y - rect.height() / 2);
}

void BalloonModifier::placeTopLeft(Runtime *runtime, Structural *element, int32 x, int32 y) {
	if (!element)
		return;

	DynamicValue pos;
	pos.setPoint(Common::Point(x, y));
	dlgMgrWriteAttribute(runtime, this, element, "position", pos);
}

void BalloonModifier::hide(Runtime *runtime, Structural *element) {
	placeTopLeft(runtime, element, -100, 600);
}

void BalloonModifier::sendMessage(Runtime *runtime, const char *messageName) {
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

void BalloonModifier::setSFX(Runtime *runtime, bool &state, bool on, const char *onMessage, const char *offMessage) {
	if (state != on) {
		state = on;
		sendMessage(runtime, on ? onMessage : offMessage);
	}
}

void BalloonModifier::newGame(Runtime *runtime, int32 level) {
	_points = 0;
	_lives = (level <= 5) ? 5 : (level <= 10) ? 4 : 3;
	sendMessage(runtime, "UpdateScore");
	setupLevel(runtime, level);
}

void BalloonModifier::setupLevel(Runtime *runtime, int32 levelNum) {
	_runtime = runtime;
	if (!loadLevels() || levelNum < 1 || levelNum > 15)
		return;

	const Level &level = _levels[levelNum];
	_level = levelNum;
	_tier = (levelNum <= 5) ? 0 : (levelNum <= 10) ? 1 : 2;

	static const double kSpeeds[3] = {2.7, 3.2, 3.6};
	_baseSpeed = _speed = kSpeeds[_tier];
	_wind = 2;
	_windCountdown = runtime->getRandom()->getRandomNumberRng(110, 400);
	dlgMgrSetInt(runtime, this, child(11), "cel", _wind);

	_bx = level.start.x;
	_by = level.start.y;
	_kids = 0;
	_kidsNeeded = level.kidsNeeded;
	_flyState = kFlyNormal;
	_stateTicks = 0;
	_countingDown = true;
	_countdown = 28;
	_mouseHeld = false;
	_keyUp = false;
	_keyDown = false;
	_paused = true;
	_birdTimer = runtime->getRandom()->getRandomNumberRng(9, 30);
	_boltOn = false;
	_boltTimer = runtime->getRandom()->getRandomNumberRng(30, 140);
	hide(runtime, child(7));

	for (int t = 0; t < 3; t++) {
		if (t == _tier)
			place(runtime, grandchild(1, t + 1), static_cast<int32>(_bx), static_cast<int32>(_by));
		else
			hide(runtime, grandchild(1, t + 1));
		hide(runtime, grandchild(15, t + 1));
	}

	for (uint list = 0; list < kListCount; list++) {
		_lists[list] = level.lists[list];
		for (uint i = 0; i < _lists[list].size(); i++)
			hide(runtime, listElement(list, i));
	}

	_flyerActive.clear();
	_flyerActive.resize(_lists[kListFlyers].size());
	_flyerStep.clear();
	_flyerStep.resize(_lists[kListFlyers].size());
	for (uint i = 0; i < _lists[kListFlyers].size(); i++)
		dlgMgrSetBool(runtime, this, listElement(kListFlyers, i), "paused", true);

	_kidTaken.clear();
	_kidTaken.resize(_lists[kListKids].size());

	// This tier's midground pieces scroll, the others are hidden
	static const int kMidgroundFirst[3] = {1, 4, 7};
	static const int kMidgroundLast[3] = {3, 6, 10};
	_midgroundX.clear();
	_midgroundX.resize(11);
	for (int i = 1; i <= 10; i++) {
		Structural *piece = grandchild(9, i);
		if (i >= kMidgroundFirst[_tier] && i <= kMidgroundLast[_tier] && piece && piece->isElement() && static_cast<Element *>(piece)->isVisual())
			_midgroundX[i] = static_cast<VisualElement *>(piece)->getRelativeRect().left;
		else
			hide(runtime, piece);
	}

	_pad = level.pad;
	_padShown = false;
	hide(runtime, child(12));
	hide(runtime, child(13));
	for (int i = 1; i <= 5; i++)
		hide(runtime, grandchild(14, i));

	placeTopLeft(runtime, grandchild(2, 1), 151, 160);
	dlgMgrSetInt(runtime, this, grandchild(2, 2), "cel", levelNum % 5);
	placeTopLeft(runtime, grandchild(2, 2), 391, 198);
	showLives(runtime);

	_initialized = true;
}

void BalloonModifier::showLives(Runtime *runtime) {
	for (int i = 1; i <= 5; i++) {
		if (i == _lives - 1)
			placeTopLeft(runtime, grandchild(10, i), 207, 437);
		else
			hide(runtime, grandchild(10, i));
	}
}

void BalloonModifier::tick(Runtime *runtime) {
	_scheduledEvent.reset();
	if (!_running)
		return;

	_scheduledEvent = runtime->getScheduler().scheduleMethod<BalloonModifier, &BalloonModifier::tick>(runtime->getPlayTime() + 83, this);

	if (_paused || !_initialized)
		return;

	if (_countingDown) {
		if (_countdown-- > 0)
			return;

		_countingDown = false;
		hide(runtime, grandchild(2, 1));
		hide(runtime, grandchild(2, 2));
		showLives(runtime);
		return;
	}

	// The game waits while the mouse is over the bottom bar, except the up and down buttons
	const Common::Point &mouse = runtime->getCachedMousePosition();
	if (mouse.y > 480 || (mouse.y >= 430 && (mouse.x < 79 || mouse.x > 195)))
		return;

	if (_flyState == kFlyNormal || _flyState == kFlyOverPad)
		moveBalloon(runtime, mouse);

	if (tickFlyState(runtime))
		return;

	tickWind(runtime);
	scrollGround(runtime);
	scrollFlyers(runtime);
	scrollMidground(runtime);
	scrollPad(runtime);
}

void BalloonModifier::moveBalloon(Runtime *runtime, const Common::Point &mouse) {
	bool onButtons = (mouse.y >= 427 && mouse.y <= 477);
	bool up = (_mouseHeld && onButtons && mouse.x >= 78 && mouse.x <= 138) || (_keyboardMode && _keyUp);
	bool down = (_mouseHeld && onButtons && mouse.x >= 140 && mouse.x <= 196) || (_keyboardMode && _keyDown);

	bool rising = false;
	bool sinking = false;
	if (up) {
		if (_by > 62.0) {
			_by -= _speed;
			rising = true;
		}
	} else if (down) {
		if (_by < 378.0) {
			_by += _speed * 1.5;
			sinking = true;
		}
	} else if (!_mouseHeld && !_keyUp && !_keyDown) {
		// It drifts down unless held
		if (_by < 378.0)
			_by += 0.1;
	}

	setSFX(runtime, _upSFX, rising, "UpSFXOn", "UpSFXOff");
	setSFX(runtime, _downSFX, sinking, "DownSFXOn", "DownSFXOff");
	place(runtime, balloonElement(), static_cast<int32>(_bx), static_cast<int32>(_by));
}

void BalloonModifier::crash() {
	if (_flyState == kFlyNormal || _flyState == kFlyOverPad)
		_flyState = kFlyCrash;
}

// Crashing and landing. Returns true if nothing else moves this tick.
bool BalloonModifier::tickFlyState(Runtime *runtime) {
	Structural *crashElement = grandchild(15, _tier + 1);

	switch (_flyState) {
	case kFlyCrash:
		hide(runtime, balloonElement());
		place(runtime, crashElement, static_cast<int32>(_bx), static_cast<int32>(_by));
		dlgMgrSetInt(runtime, this, crashElement, "cel", 1);
		sendMessage(runtime, "PlaySFX5");
		setSFX(runtime, _upSFX, false, "UpSFXOn", "UpSFXOff");
		setSFX(runtime, _downSFX, false, "DownSFXOn", "DownSFXOff");
		setSFX(runtime, _planeSFX, false, "PlaneSFXOn", "PlaneSFXOff");
		setSFX(runtime, _heliSFX, false, "HelicopterSFXOn", "HelicopterSFXOff");
		setSFX(runtime, _lightningSFX, false, "LightningSFXOn", "LightningSFXOff");
		_flyState = kFlyFalling;
		_stateTicks = 0;
		return true;
	case kFlyFalling:
		if (++_stateTicks == 13)
			_flyState = kFlyDropping;
		else if (_by >= 490.0)
			_flyState = kFlyCrashed;
		else
			_by += 9.5;
		dlgMgrSetInt(runtime, this, crashElement, "cel", _stateTicks);
		place(runtime, crashElement, static_cast<int32>(_bx), static_cast<int32>(_by));
		return true;
	case kFlyDropping:
		if (_by < 490.0) {
			_by += 8.5;
			place(runtime, crashElement, static_cast<int32>(_bx), static_cast<int32>(_by));
		} else
			_flyState = kFlyCrashed;
		return true;
	case kFlyCrashed:
		hide(runtime, crashElement);
		if (_lives > 1) {
			_lives--;
			setupLevel(runtime, _level);
		} else {
			_paused = true;
			sendMessage(runtime, "GameOver");
		}
		return true;
	case kFlyLanded:
		_stateTicks++;
		if (_stateTicks == 1) {
			sendMessage(runtime, "levelwon");
			for (int i = 1; i <= 5; i++)
				place(runtime, grandchild(14, i), runtime->getRandom()->getRandomNumberRng(120, 520), runtime->getRandom()->getRandomNumberRng(45, 190));
			if (_kids == _kidsNeeded) {
				_points += 200;
				sendMessage(runtime, "UpdateScore");
			}
		} else if (_stateTicks == 12) {
			for (int i = 1; i <= 5; i++)
				hide(runtime, grandchild(14, i));
			if (_level < 15 && _level != 5 && _level != 10) {
				setupLevel(runtime, _level + 1);
				_paused = false;
			} else {
				_paused = true;
				sendMessage(runtime, "YouWin");
			}
		} else {
			for (int i = 1; i <= 5; i++)
				dlgMgrSetInt(runtime, this, grandchild(14, i), "cel", _stateTicks);
		}
		return true;
	case kFlyOverPad: {
			// Only the flyers move while the balloon comes down onto the pad
			bool airship = (_pad.type == 31);
			double basket = _by + 46.0;
			bool landed = airship ? (_bx > _pad.x - 17.0 && _bx <= _pad.x + 51.0 && basket >= _pad.y - 27.0 && basket < _pad.y - 17.0)
								  : (_bx > _pad.x - 98.0 && _bx < _pad.x + 98.0 && basket > _pad.y - 41.0 && basket < _pad.y - 31.0);
			if (landed) {
				sendMessage(runtime, "PlaySFX1");
				setSFX(runtime, _upSFX, false, "UpSFXOn", "UpSFXOff");
				setSFX(runtime, _downSFX, false, "DownSFXOn", "DownSFXOff");
				_flyState = kFlyLanded;
				_stateTicks = 0;
			}
			scrollFlyers(runtime);
			return true;
		}
	default:
		return false;
	}
}

void BalloonModifier::tickWind(Runtime *runtime) {
	if (--_windCountdown > 0)
		return;

	static const double kWindFactors[3] = {0.85, 1.0, 1.2};
	_windCountdown = runtime->getRandom()->getRandomNumberRng(110, 400);
	_wind = runtime->getRandom()->getRandomNumberRng(1, 3);
	_speed = _baseSpeed * kWindFactors[_wind - 1];
	dlgMgrSetInt(runtime, this, child(11), "cel", _wind);
}

// Triangle test: the width grows linearly from the top point down to the base
static bool balloonInTriangle(double px, double py, double left, double right, double top, double bottom) {
	if (py < top || py > bottom || bottom <= top)
		return false;

	int32 w = static_cast<int32>(static_cast<int32>(py - top) * ((right - left) / 2.0) / static_cast<int32>(bottom - top));
	int32 cx = static_cast<int32>(left + (right - left) / 2.0);
	return px >= cx - w && px <= cx + w;
}

static bool balloonInRect(double px, double py, double left, double right, double top, double bottom) {
	return px > left && px < right && py > top && py < bottom;
}

// Ground objects are tested against two points at the bottom of the basket
bool BalloonModifier::hitsGround(const Object &obj) const {
	const double ox = obj.x;
	const double oy = obj.y;
	const double probes[2][2] = {{_bx + 20.0, _by + 46.0}, {_bx - 12.0, _by + 46.0}};

	for (const auto &probe : probes) {
		double px = probe[0];
		double py = probe[1];
		bool hit = false;

		switch (obj.type) {
		case 0: hit = balloonInTriangle(px, py, ox - 35, ox + 35, oy - 70, oy + 70); break;
		case 1: hit = balloonInRect(px, py, ox - 22, ox + 14, oy - 90, oy + 98); break;
		case 2: hit = balloonInRect(px, py, ox - 40, ox + 40, oy - 58, oy + 86); break;
		case 3: hit = balloonInTriangle(px, py, ox - 87, ox + 87, oy - 74, oy + 74); break;
		case 4: hit = balloonInRect(px, py, ox - 36, ox + 36, oy - 72, oy + 80); break;
		case 5: hit = balloonInRect(px, py, ox - 21, ox + 21, oy - 67, oy + 67); break;
		case 6: hit = balloonInTriangle(px, py, ox - 105, ox + 95, oy - 54, oy + 54); break;
		case 7: hit = balloonInTriangle(px, py, ox - 70, ox + 68, oy - 56, oy + 55); break;
		case 8: hit = balloonInTriangle(px, py, ox - 67, ox + 59, oy - 65, oy + 59); break;
		case 9: hit = balloonInTriangle(px, py, ox - 83, ox + 53, oy - 61, oy - 27) || balloonInRect(px, py, ox - 83, ox + 83, oy - 34, oy + 76); break;
		case 10: hit = balloonInRect(px, py, ox - 52, ox + 34, oy - 63, oy + 63); break;
		case 11: hit = balloonInRect(px, py, ox - 24, ox + 16, oy - 57, oy + 87) || balloonInRect(px, py, ox - 24, ox - 12, oy - 90, oy - 58); break;
		case 12: hit = balloonInTriangle(px, py, ox - 41, ox + 41, oy - 80, oy - 54) || balloonInRect(px, py, ox - 36, ox + 36, oy - 55, oy + 81); break;
		case 13: hit = balloonInRect(px, py, ox - 39, ox + 39, oy - 71, oy + 71); break;
		case 14: hit = balloonInRect(px, py, ox - 43, ox + 43, oy - 63, oy + 63); break;
		case 15: hit = balloonInRect(px, py, ox - 43, ox + 43, oy - 42, oy + 86) || balloonInTriangle(px, py, ox - 43, ox + 43, oy - 85, oy - 41); break;
		case 16: hit = balloonInTriangle(px, py, ox - 47, ox + 47, oy - 82, oy - 27) || balloonInRect(px, py, ox - 24, ox + 24, oy - 31, oy + 81); break;
		case 17: hit = balloonInTriangle(px, py, ox - 32, ox + 34, oy - 78, oy + 24) || balloonInRect(px, py, ox - 31, ox + 31, oy + 22, oy + 80); break;
		case 18: hit = balloonInRect(px, py, ox - 62, ox + 62, oy - 48, oy + 48); break;
		case 19: hit = balloonInRect(px, py, ox - 17, ox + 17, oy - 86, oy + 86); break;
		default: break;
		}

		if (hit)
			return true;
	}

	return false;
}

// Flyers are tested by their points inside a box around the balloon
bool BalloonModifier::hitsFlyer(const Object &obj) const {
	const double fx = obj.x;
	const double fy = obj.y;

	auto anyIn = [&](const double points[][2], int count, double left, double right) {
		for (int i = 0; i < count; i++) {
			if (points[i][0] > left && points[i][0] < right && points[i][1] > _by - 50.0 && points[i][1] < _by + 50.0)
				return true;
		}
		return false;
	};

	switch (obj.type) {
	case 20: {
			const double points[4][2] = {{fx - 32, fy}, {fx + 33, fy - 11}, {fx - 9, fy - 10}, {fx - 16, fy + 12}};
			for (const auto &p : points) {
				if (p[0] > _bx - 25 && p[0] < _bx + 25 && p[1] > _by - 48 && p[1] < _by + 48)
					return true;
			}
			return false;
		}
	case 21: {
			const double points[4][2] = {{fx - 37, fy + 7}, {fx - 26, fy - 15}, {fx + 43, fy + 2}, {fx - 3, fy + 16}};
			for (const auto &p : points) {
				if (p[0] > _bx - 25 && p[0] < _bx + 25 && p[1] > _by - 48 && p[1] < _by + 48)
					return true;
			}
			return false;
		}
	case 22: {
			const double points[2][2] = {{fx - 11, fy + 5}, {fx + 12, fy + 5}};
			return anyIn(points, 2, _bx - 12, _bx + 32);
		}
	case 23: {
			if (!_boltOn)
				return false;
			const double bolt1[1][2] = {{fx - 50, fy + 60}};
			const double bolt2[2][2] = {{fx - 27, fy + 84}, {fx - 4, fy + 29}};
			return anyIn(bolt1, 1, _bx - 15, _bx + 35) || anyIn(bolt2, 2, _bx - 25, _bx + 25);
		}
	case 25: {
			const double points[2][2] = {{fx - 11, fy - 26}, {fx + 5, fy + 8}};
			return anyIn(points, 2, _bx - 13, _bx + 33);
		}
	case 26: {
			const double points[2][2] = {{fx - 6, fy - 28}, {fx - 1, fy - 5}};
			return anyIn(points, 2, _bx - 10, _bx + 30);
		}
	default:
		return false;
	}
}

void BalloonModifier::scrollGround(Runtime *runtime) {
	static const double kShowLimits[kListCount] = {690.0, 700.0, 710.0, 0.0, 650.0};

	for (uint list = 0; list < kListCount; list++) {
		if (list == kListFlyers)
			continue;

		Common::Array<Object> &objects = _lists[list];
		for (uint i = 0; i < objects.size(); i++) {
			Object &obj = objects[i];
			if (obj.x <= -90.0)
				continue;

			obj.x -= _speed;
			Structural *element = listElement(list, i);
			if (obj.x <= -90.0) {
				hide(runtime, element);
				obj.x = -300.0;
				continue;
			}
			if (obj.x >= kShowLimits[list])
				continue;
			if (list == kListKids && _kidTaken[i])
				continue;

			place(runtime, element, static_cast<int32>(obj.x), static_cast<int32>(obj.y));

			if (list == kListKids) {
				// Kids are picked up when the balloon reaches them
				if (obj.type == 28 && _flyState == kFlyNormal) {
					bool reached = false;
					for (double ky : {obj.y, obj.y - 12.0}) {
						if (obj.x > _bx - 25 && obj.x < _bx + 25 && ky > _by - 50 && ky < _by + 50)
							reached = true;
					}
					if (reached) {
						_kidTaken[i] = true;
						hide(runtime, element);
						_points += 100;
						_kids++;
						sendMessage(runtime, "UpdateScore");
						sendMessage(runtime, "PlaySFX4");
					}
				}
			} else if (_flyState == kFlyNormal && hitsGround(obj))
				crash();
		}
	}
}

void BalloonModifier::scrollFlyers(Runtime *runtime) {
	bool planeNear = false;
	bool heliNear = false;
	bool birdNear = false;

	Common::Array<Object> &flyers = _lists[kListFlyers];
	for (uint i = 0; i < flyers.size(); i++) {
		Object &obj = flyers[i];
		if (obj.x <= -90.0)
			continue;

		Structural *element = listElement(kListFlyers, i);
		obj.x -= (_flyState == kFlyOverPad) ? 0.0 : _speed;
		if (obj.x < 674.0) {
			if (obj.type == 20)
				obj.x -= 2.0;
			else if (obj.type == 21)
				obj.x -= 1.0;
		}

		if (obj.x <= -90.0) {
			hide(runtime, element);
			dlgMgrSetBool(runtime, this, element, "paused", true);
			obj.x = -300.0;
			continue;
		}
		if (obj.x >= 674.0)
			continue;

		if (!_flyerActive[i]) {
			_flyerActive[i] = true;
			dlgMgrSetBool(runtime, this, element, "paused", false);
		}

		if (obj.sub >= 1 && obj.sub <= 6 && !_motion[obj.sub].empty()) {
			const MotionStep &step = _motion[obj.sub][_flyerStep[i]];
			obj.x -= step.dx;
			obj.y += step.dy;
			_flyerStep[i] = (_flyerStep[i] + 1) % 7;
		}

		place(runtime, element, static_cast<int32>(obj.x), static_cast<int32>(obj.y));

		bool near = (obj.x > _bx - 70.0 && obj.x < _bx + 220.0);
		if (near) {
			if (obj.type == 20)
				planeNear = true;
			else if (obj.type == 21)
				heliNear = true;
			else if (obj.type == 22)
				birdNear = true;
		}

		// Storm clouds throw lightning on and off
		if (obj.type == 23) {
			if (--_boltTimer <= 0) {
				_boltOn = !_boltOn;
				_boltTimer = _boltOn ? runtime->getRandom()->getRandomNumberRng(9, 48) : runtime->getRandom()->getRandomNumberRng(30, 140);
				setSFX(runtime, _lightningSFX, _boltOn, "LightningSFXOn", "LightningSFXOff");
			}
			if (_boltOn)
				place(runtime, child(7), static_cast<int32>(obj.x) - 26, static_cast<int32>(obj.y) + 58);
			else
				hide(runtime, child(7));
		}

		if (_flyState != kFlyNormal || !hitsFlyer(obj))
			continue;

		if (obj.type == 25) {
			hide(runtime, element);
			obj.x = -300.0;
			_points += 50;
			sendMessage(runtime, "UpdateScore");
			sendMessage(runtime, "PlaySFX3");
		} else if (obj.type == 26) {
			if (_lives < 5) {
				hide(runtime, element);
				obj.x = -300.0;
				sendMessage(runtime, "PlaySFX2");
				_lives++;
				showLives(runtime);
			}
		} else
			crash();
	}

	setSFX(runtime, _planeSFX, planeNear && _flyState == kFlyNormal, "PlaneSFXOn", "PlaneSFXOff");
	setSFX(runtime, _heliSFX, heliNear && _flyState == kFlyNormal, "HelicopterSFXOn", "HelicopterSFXOff");

	if (birdNear && --_birdTimer <= 0) {
		_birdTimer = runtime->getRandom()->getRandomNumberRng(9, 30);
		sendMessage(runtime, "BirdSFX");
	}
}

void BalloonModifier::scrollMidground(Runtime *runtime) {
	static const int kFirst[3] = {1, 4, 7};
	static const int kLast[3] = {3, 6, 10};
	static const double kWrapAt[3] = {-210.0, -250.0, -133.0};
	static const double kWrapBy[3] = {1308.0, 1549.0, 1128.0};

	for (int i = kFirst[_tier]; i <= kLast[_tier]; i++) {
		Structural *piece = grandchild(9, i);
		if (!piece || !piece->isElement() || !static_cast<Element *>(piece)->isVisual())
			continue;

		_midgroundX[i] -= 2.0;
		if (_midgroundX[i] <= kWrapAt[_tier])
			_midgroundX[i] += kWrapBy[_tier];

		placeTopLeft(runtime, piece, static_cast<int32>(_midgroundX[i]), static_cast<VisualElement *>(piece)->getRelativeRect().top);
	}
}

void BalloonModifier::scrollPad(Runtime *runtime) {
	bool airship = (_pad.type == 31);
	Structural *padElement = child(airship ? 13 : 12);

	_pad.x -= _speed;
	if (_pad.x < 740.0) {
		if (!_padShown) {
			_padShown = true;
			dlgMgrSetBool(runtime, this, padElement, "paused", false);
		}
		place(runtime, padElement, static_cast<int32>(_pad.x), static_cast<int32>(_pad.y));
	}

	if (_flyState != kFlyNormal)
		return;

	if (_pad.x <= _bx) {
		_flyState = kFlyOverPad;
		_boltOn = false;
		hide(runtime, child(7));
		return;
	}

	// Coming in too low hits the side of the pad
	double basket = _by + 46.0;
	for (double px : {_bx + 20.0, _bx - 12.0}) {
		bool hit = airship ? (px > _pad.x - 93.0 && px < _pad.x + 105.0 && basket > _pad.y - 71.0 && basket < _pad.y + 83.0)
						   : (px > _pad.x - 98.0 && px < _pad.x + 98.0 && basket > _pad.y - 36.0 && basket < _pad.y + 60.0);
		if (hit) {
			crash();
			return;
		}
	}
}

Common::SharedPtr<Modifier> BalloonModifier::shallowClone() const {
	Common::SharedPtr<BalloonModifier> clone(new BalloonModifier(*this));
	clone->_scheduledEvent.reset();
	clone->_keySignaller.reset();
	clone->_running = false;
	clone->_initialized = false;
	return clone;
}

const char *BalloonModifier::getDefaultName() const {
	return "Balloon";
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
					// The source may be in a scene that isn't loaded, so sounds use the media name stored with the track.
					// Some titles kept stale names from the title they were copied from, in which case another track's
					// media name is the sound.
					asset.clear();
					if (track.trackType == Data::KW::DlgMgrTrack::kTrackTypeSound) {
						if (runtime->getProject()->isAssetStored(track.mediaName))
							asset.setString(track.mediaName);
						else {
							for (const DlgMgrClientModifier::Track &otherTrack : client->getTracks()) {
								if (!otherTrack.mediaName.empty() && runtime->getProject()->isAssetStored(otherTrack.mediaName)) {
									asset.setString(otherTrack.mediaName);
									break;
								}
							}
						}
					}
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

static Common::SharedPtr<Structural> dlgMgrFindDescendant(Structural *structural, uint32 guid) {
	for (const Common::SharedPtr<Structural> &child : structural->getChildren()) {
		if (child->getStaticGUID() == guid)
			return child;

		Common::SharedPtr<Structural> found = dlgMgrFindDescendant(child.get(), guid);
		if (found)
			return found;
	}

	return nullptr;
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

	if (current->getStaticGUID() == guid)
		return current->getSelfReference().lock().staticCast<Structural>();

	// Deeply nested targets store a truncated path, so look below where it ends
	return dlgMgrFindDescendant(current, guid);
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
	  _kwSmackerModifierFactory(this), _editCtrlModifierFactory(this), _gameModifierFactory(this), _whaleMazeModifierFactory(this), _eyefollowerModifierFactory(this), _smkJumpModifierFactory(this), _balloonModifierFactory(this) {
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
		"AntMaze", "Art", "LLava", "Lander", "RoverRace", "Sub", "SvTown"
	};
	for (const char *name : gameModifierNames)
		registrar->registerPlugInModifier(name, &_gameModifierFactory);

	registrar->registerPlugInModifier("WhaleMaze", &_whaleMazeModifierFactory);
	registrar->registerPlugInModifier("Eyefollower", &_eyefollowerModifierFactory);
	registrar->registerPlugInModifier("SmkJump", &_smkJumpModifierFactory);
	registrar->registerPlugInModifier("Balloon", &_balloonModifierFactory);
}

} // End of namespace KW

namespace PlugIns {

Common::SharedPtr<PlugIn> createKnowWonder() {
	return Common::SharedPtr<PlugIn>(new KW::KnowWonderPlugIn());
}

} // End of namespace PlugIns

} // End of namespace MTropolis
