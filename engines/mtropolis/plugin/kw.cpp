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
#include "mtropolis/actions.h"
#include "mtropolis/plugins.h"
#include "mtropolis/miniscript.h"

#include "common/endian.h"
#include "common/file.h"
#include "common/random.h"

#include "graphics/managed_surface.h"
#include "graphics/fontman.h"
#include "graphics/font.h"

#include "video/avi_decoder.h"
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

EditCtrlModifier::EditCtrlModifier() : _textLimit(16), _runtime(nullptr), _isShown(false), _isEnabled(true) {
}

EditCtrlModifier::EditCtrlModifier(const EditCtrlModifier &other)
	: Modifier(other), _text(other._text), _textLimit(other._textLimit), _runtime(nullptr), _isShown(false), _isEnabled(true) {
}

EditCtrlModifier::~EditCtrlModifier() {
	hide();
}

bool EditCtrlModifier::load(const PlugInModifierLoaderContext &context, const Data::KW::EditCtrlModifier &data) {
	return true;
}

// The original's edit window is live from the start of the scene, so scripts only enable it
// again after disabling it for a dialog
bool EditCtrlModifier::respondsToEvent(const Event &evt) const {
	return evt.eventType == EventIDs::kParentEnabled || evt.eventType == EventIDs::kParentDisabled;
}

VThreadState EditCtrlModifier::consumeMessage(Runtime *runtime, const Common::SharedPtr<MessageProperties> &msg) {
	if (msg->getEvent().eventType == EventIDs::kParentEnabled) {
		_isEnabled = true;
		show(runtime);
	} else if (msg->getEvent().eventType == EventIDs::kParentDisabled)
		hide();

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
	// Older versions of the plug-in send none of these, and their titles have no such messages
	uint32 messageID = 0;
	if (!runtime->getProject()->findAuthorMessageID(messageName, messageID)) {
		debug(2, "EditCtrl: No author message named '%s'", messageName);
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
	if (keyEvt.getKeyEventType() != Common::EVENT_KEYDOWN || !_isEnabled)
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

	// Windows draws a disabled edit control as grey text on the 3D face colour
	const ColorRGB8 backRGB = _isEnabled ? ColorRGB8(255, 255, 255) : ColorRGB8(192, 192, 192);
	const ColorRGB8 textRGB = _isEnabled ? ColorRGB8(0, 0, 0) : ColorRGB8(128, 128, 128);

	uint32 white, black;
	if (surface.format.isCLUT8()) {
		const byte *pal = _runtime->getGlobalPalette().getPalette();
		uint bestBack = 0, bestText = 0;
		int bestBackDist = INT_MAX, bestTextDist = INT_MAX;
		for (uint i = 0; i < 256; i++) {
			int backDist = ABS(pal[i * 3] - backRGB.r) + ABS(pal[i * 3 + 1] - backRGB.g) + ABS(pal[i * 3 + 2] - backRGB.b);
			int textDist = ABS(pal[i * 3] - textRGB.r) + ABS(pal[i * 3 + 1] - textRGB.g) + ABS(pal[i * 3 + 2] - textRGB.b);
			if (backDist < bestBackDist) {
				bestBackDist = backDist;
				bestBack = i;
			}
			if (textDist < bestTextDist) {
				bestTextDist = textDist;
				bestText = i;
			}
		}
		white = bestBack;
		black = bestText;
	} else {
		white = surface.format.RGBToColor(backRGB.r, backRGB.g, backRGB.b);
		black = surface.format.RGBToColor(textRGB.r, textRGB.g, textRGB.b);
	}

	surface.fillRect(rect, white);

	const Graphics::Font *font = FontMan.getFontByUsage(Graphics::FontManager::kBigGUIFont);
	int y = rect.top + (rect.height() - font->getFontHeight()) / 2;
	font->drawString(&surface, _text, rect.left + 2, y, rect.width() - 4, black);

	// Caret
	if (_isEnabled && (_runtime->getRealTime() / 500) % 2 == 0) {
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
			// Only typing is limited, as with a Windows edit control
			runtime->setSceneGraphDirty();
		} break;
	case kAttribTextLimit: {
			// The original uses the low byte, and 0 is the Windows maximum
			int32 limit = 0;
			if (value.roundToInt(limit))
				_textLimit = (limit & 0xff) ? (limit & 0xff) : 0x7ffe;
		} break;
	case kAttribEnable:
		_isEnabled = true;
		show(runtime);
		break;
	case kAttribDisable:
		_isEnabled = false;
		runtime->setSceneGraphDirty();
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

FillModModifier::Fill::Fill() : r(0), g(0), b(0) {
}

FillModModifier::FillModModifier() : _color(0xffffff), _runtime(nullptr), _isActive(false) {
}

FillModModifier::FillModModifier(const FillModModifier &other)
	: Modifier(other), _enableWhen(other._enableWhen), _disableWhen(other._disableWhen), _fills(other._fills),
	  _color(other._color), _runtime(nullptr), _isActive(false) {
}

FillModModifier::~FillModModifier() {
	deactivate();
}

bool FillModModifier::load(const PlugInModifierLoaderContext &context, const Data::KW::FillModModifier &data) {
	return loadDlgMgrEvent(_enableWhen, data.enableWhen) && loadDlgMgrEvent(_disableWhen, data.disableWhen);
}

bool FillModModifier::respondsToEvent(const Event &evt) const {
	return _enableWhen.respondsTo(evt) || _disableWhen.respondsTo(evt) || evt.eventType == EventIDs::kParentDisabled
		|| evt.eventType == EventIDs::kMouseDown;
}

VThreadState FillModModifier::consumeMessage(Runtime *runtime, const Common::SharedPtr<MessageProperties> &msg) {
	const Event &evt = msg->getEvent();

	if (_enableWhen.respondsTo(evt)) {
		activate(runtime);
		return kVThreadReturn;
	}

	if (_disableWhen.respondsTo(evt) || evt.eventType == EventIDs::kParentDisabled) {
		deactivate();
		return kVThreadReturn;
	}

	// Clicks are recorded even while inactive, they just aren't drawn
	if (evt.eventType == EventIDs::kMouseDown && msg->getValue().getType() == DynamicValueTypes::kPoint) {
		Fill fill;
		fill.point = msg->getValue().getPoint();
		fill.r = (_color >> 16) & 0xff;
		fill.g = (_color >> 8) & 0xff;
		fill.b = _color & 0xff;
		_fills.push_back(fill);

		debug(2, "FillMod: Fill %u at (%i,%i) with %06x", _fills.size(), fill.point.x, fill.point.y, _color);
		runtime->setSceneGraphDirty();
	}

	return kVThreadReturn;
}

void FillModModifier::disable(Runtime *runtime) {
	deactivate();
}

void FillModModifier::activate(Runtime *runtime) {
	if (_isActive)
		return;

	Structural *owner = findStructuralOwner();
	if (!owner || !owner->isElement() || !static_cast<Element *>(owner)->isVisual())
		return;

	static_cast<VisualElement *>(owner)->addPostRenderHook(this);
	_hookedElement = owner->getSelfReference();
	_runtime = runtime;
	_isActive = true;
	runtime->setSceneGraphDirty();
}

void FillModModifier::deactivate() {
	if (!_isActive)
		return;

	_isActive = false;

	Common::SharedPtr<RuntimeObject> element = _hookedElement.lock();
	if (element)
		static_cast<VisualElement *>(element.get())->removePostRenderHook(this);
	_hookedElement.reset();

	if (_runtime)
		_runtime->setSceneGraphDirty();
}

bool FillModModifier::readAttribute(MiniscriptThread *thread, DynamicValue &result, const Common::String &attrib) {
	if (attrib == "numfills") {
		result.setInt(_fills.size());
		return true;
	}
	if (attrib == "color") {
		result.setInt(_color);
		return true;
	}

	return Modifier::readAttribute(thread, result, attrib);
}

MiniscriptInstructionOutcome FillModModifier::writeRefAttribute(MiniscriptThread *thread, DynamicValueWriteProxy &result, const Common::String &attrib) {
	static const struct {
		const char *name;
		WritableAttribute attrib;
	} kAttribs[] = {
		{"color", kAttribColor},
		{"undo", kAttribUndo},
		{"clear", kAttribClear},
		{"save", kAttribSave},
		{"restore", kAttribRestore},
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

MiniscriptInstructionOutcome FillModModifier::WriteProxy::write(MiniscriptThread *thread, const DynamicValue &value, void *objectRef, uintptr ptrOrOffset) {
	return static_cast<FillModModifier *>(objectRef)->writeAttribute(thread, static_cast<WritableAttribute>(ptrOrOffset), value);
}

MiniscriptInstructionOutcome FillModModifier::WriteProxy::refAttrib(MiniscriptThread *thread, DynamicValueWriteProxy &proxy, void *objectRef, uintptr ptrOrOffset, const Common::String &attrib) {
	return kMiniscriptInstructionOutcomeFailed;
}

MiniscriptInstructionOutcome FillModModifier::WriteProxy::refAttribIndexed(MiniscriptThread *thread, DynamicValueWriteProxy &proxy, void *objectRef, uintptr ptrOrOffset, const Common::String &attrib, const DynamicValue &index) {
	return kMiniscriptInstructionOutcomeFailed;
}

MiniscriptInstructionOutcome FillModModifier::writeAttribute(MiniscriptThread *thread, WritableAttribute attrib, const DynamicValue &valueIn) {
	Runtime *runtime = thread->getRuntime();

	switch (attrib) {
	case kAttribColor: {
			// mTropolis 2.0 passes variables by reference
			DynamicValue value = valueIn;
			if (value.getType() == DynamicValueTypes::kObject) {
				Common::SharedPtr<RuntimeObject> obj = value.getObject().object.lock();
				if (obj && obj->isModifier() && static_cast<Modifier *>(obj.get())->isVariable())
					static_cast<VariableModifier *>(obj.get())->varGetValue(value);
			}

			int32 color = 0;
			if (!value.roundToInt(color))
				return kMiniscriptInstructionOutcomeFailed;

			_color = static_cast<uint32>(color) & 0xffffff;
		} break;
	case kAttribUndo:
	case kAttribClear:
		// The original fails on an empty list, but EcoPost checks numfills first
		if (_fills.empty())
			break;

		if (attrib == kAttribUndo)
			_fills.pop_back();
		else
			_fills.clear();
		runtime->setSceneGraphDirty();
		break;
	case kAttribSave:
	case kAttribRestore: {
			Modifier *fileMod = resolveFileModifier(valueIn);
			if (!fileMod) {
				warning("FillMod: %s needs a file modifier", attrib == kAttribSave ? "save" : "restore");
				return kMiniscriptInstructionOutcomeFailed;
			}

			if (attrib == kAttribSave)
				saveFills(thread, fileMod);
			else
				restoreFills(thread, fileMod);
		} break;
	}

	return kMiniscriptInstructionOutcomeContinue;
}

Modifier *FillModModifier::resolveFileModifier(const DynamicValue &value) {
	if (value.getType() != DynamicValueTypes::kObject)
		return nullptr;

	Common::SharedPtr<RuntimeObject> obj = value.getObject().object.lock();
	if (!obj || !obj->isModifier())
		return nullptr;

	Modifier *modifier = static_cast<Modifier *>(obj.get());
	if (modifier->isVariable()) {
		DynamicValue varValue;
		static_cast<VariableModifier *>(modifier)->varGetValue(varValue);
		if (varValue.getType() != DynamicValueTypes::kObject)
			return nullptr;

		obj = varValue.getObject().object.lock();
		if (!obj || !obj->isModifier())
			return nullptr;
		modifier = static_cast<Modifier *>(obj.get());
	}

	return modifier;
}

void FillModModifier::saveFills(MiniscriptThread *thread, Modifier *fileMod) const {
	// Written through the file modifier's write attribute, like the original.
	// Lines end in CR LF (the original wrote LF) so that EcoPost's CR chunk
	// delimiter reads them back one line at a time.
	Common::String text = Common::String::format("%u\r\n", _fills.size());
	for (const Fill &fill : _fills)
		text += Common::String::format("%u %u %u %u %u\r\n", static_cast<uint16>(fill.point.x), static_cast<uint16>(fill.point.y), fill.r, fill.g, fill.b);

	DynamicValueWriteProxy proxy;
	if (fileMod->writeRefAttribute(thread, proxy, "write") != kMiniscriptInstructionOutcomeContinue) {
		warning("FillMod: Couldn't write to '%s'", fileMod->getName().c_str());
		return;
	}

	DynamicValue str;
	str.setString(text);
	proxy.pod.ifc->write(thread, str, proxy.pod.objectRef, proxy.pod.ptrOrOffset);

	debug(2, "FillMod: Saved %u fills", _fills.size());
}

void FillModModifier::restoreFills(MiniscriptThread *thread, Modifier *fileMod) {
	// A chunk may hold several lines (files with LF lines read with a CR
	// delimiter), so chunks are split into lines before parsing
	Common::Array<Common::String> lines;
	uint nextLine = 0;

	auto readLine = [&](Common::String &outLine) -> bool {
		for (;;) {
			while (nextLine < lines.size()) {
				outLine = lines[nextLine++];
				outLine.trim();
				if (!outLine.empty())
					return true;
			}

			DynamicValue chunk;
			if (!fileMod->readAttribute(thread, chunk, "readchunk") || chunk.getType() != DynamicValueTypes::kString)
				return false;

			DynamicValue error;
			if (chunk.getString().empty() && fileMod->readAttribute(thread, error, "error") && error.getType() == DynamicValueTypes::kInteger && error.getInt() != 0)
				return false;

			lines.clear();
			nextLine = 0;
			Common::String line;
			for (char c : chunk.getString()) {
				if (c == '\r' || c == '\n') {
					lines.push_back(line);
					line.clear();
				} else
					line += c;
			}
			lines.push_back(line);
		}
	};

	Common::String line;
	if (!readLine(line)) {
		warning("FillMod: No fill count to restore");
		return;
	}

	uint count = strtoul(line.c_str(), nullptr, 10);
	uint restored = 0;

	// Records are appended without clearing, like the original
	for (uint i = 0; i < count; i++) {
		if (!readLine(line))
			break;

		uint values[5] = {0, 0, 0, 0, 0};
		uint numValues = 0;
		const char *p = line.c_str();
		while (*p && numValues < 5) {
			while (*p == ' ' || *p == '\t')
				p++;
			if (!*p)
				break;
			char *end = nullptr;
			values[numValues++] = strtoul(p, &end, 10);
			if (end == p)
				break;
			p = end;
		}

		if (numValues < 5)
			continue;

		Fill fill;
		fill.point = Common::Point(static_cast<int16>(values[0]), static_cast<int16>(values[1]));
		fill.r = values[2] & 0xff;
		fill.g = values[3] & 0xff;
		fill.b = values[4] & 0xff;
		_fills.push_back(fill);
		restored++;
	}

	debug(2, "FillMod: Restored %u of %u fills", restored, count);
}

void FillModModifier::renderElementPostEffect(VisualElement *element, Window *window) {
	if (_fills.empty())
		return;

	Graphics::ManagedSurface &surface = *window->getSurface();
	const Common::Point &origin = element->getCachedAbsoluteOrigin();
	const Common::Rect &relRect = element->getRelativeRect();

	// The original only clips to the frame buffer. Clipping to the element
	// keeps a fill in an open region from flooding the whole stage.
	Common::Rect clipRect(origin.x, origin.y, origin.x + relRect.width(), origin.y + relRect.height());
	clipRect.clip(Common::Rect(0, 0, surface.w, surface.h));
	if (clipRect.isEmpty())
		return;

	for (const Fill &fill : _fills)
		floodFill(surface, clipRect, fill);
}

void FillModModifier::floodFill(Graphics::ManagedSurface &surface, const Common::Rect &clipRect, const Fill &fill) const {
	if (!clipRect.contains(fill.point))
		return;

	const Graphics::PixelFormat &format = surface.format;
	const uint bpp = format.bytesPerPixel;
	if (bpp != 1 && bpp != 2 && bpp != 4)
		return;

	// Like ExtFloodFill with FLOODFILLBORDER: fill until black. On a palette
	// display, black is the palette entry closest to it, and the brush is the
	// closest entry to the fill colour.
	bool isBorderIndex[256];
	uint32 fillColor = 0;
	if (format.isCLUT8()) {
		const byte *pal = _runtime->getGlobalPalette().getPalette();
		uint bestBlack = 0, bestFill = 0;
		int bestBlackDist = INT_MAX, bestFillDist = INT_MAX;
		for (uint i = 0; i < 256; i++) {
			int blackDist = pal[i * 3] + pal[i * 3 + 1] + pal[i * 3 + 2];
			if (blackDist < bestBlackDist) {
				bestBlackDist = blackDist;
				bestBlack = i;
			}

			int dr = pal[i * 3] - fill.r;
			int dg = pal[i * 3 + 1] - fill.g;
			int db = pal[i * 3 + 2] - fill.b;
			int fillDist = dr * dr + dg * dg + db * db;
			if (fillDist < bestFillDist) {
				bestFillDist = fillDist;
				bestFill = i;
			}
		}

		for (uint i = 0; i < 256; i++)
			isBorderIndex[i] = (memcmp(pal + i * 3, pal + bestBlack * 3, 3) == 0);
		fillColor = bestFill;
	} else
		fillColor = format.RGBToColor(fill.r, fill.g, fill.b);

	Graphics::Surface &raw = *surface.surfacePtr();

	auto getPixel = [&](int x, int y) -> uint32 {
		const byte *ptr = static_cast<const byte *>(raw.getBasePtr(x, y));
		if (bpp == 1)
			return *ptr;
		if (bpp == 2)
			return *reinterpret_cast<const uint16 *>(ptr);
		return *reinterpret_cast<const uint32 *>(ptr);
	};

	auto setPixel = [&](int x, int y) {
		byte *ptr = static_cast<byte *>(raw.getBasePtr(x, y));
		if (bpp == 1)
			*ptr = fillColor;
		else if (bpp == 2)
			*reinterpret_cast<uint16 *>(ptr) = fillColor;
		else
			*reinterpret_cast<uint32 *>(ptr) = fillColor;
	};

	auto isBorder = [&](uint32 pixel) -> bool {
		if (bpp == 1)
			return isBorderIndex[pixel];

		uint8 r, g, b;
		format.colorToRGB(pixel, r, g, b);
		return r == 0 && g == 0 && b == 0;
	};

	const int width = clipRect.width();
	const int height = clipRect.height();

	// Visited pixels, so that a fill colour that maps to the region's own
	// colour can't loop forever
	Common::Array<bool> visited;
	visited.resize(width * height);
	for (uint i = 0; i < visited.size(); i++)
		visited[i] = false;

	auto canFill = [&](int x, int y) -> bool {
		return !visited[(y - clipRect.top) * width + (x - clipRect.left)] && !isBorder(getPixel(x, y));
	};

	// 4-connected scanline fill
	Common::Array<Common::Point> stack;
	stack.push_back(fill.point);

	while (!stack.empty()) {
		Common::Point pt = stack.back();
		stack.pop_back();

		if (!canFill(pt.x, pt.y))
			continue;

		int left = pt.x;
		while (left > clipRect.left && canFill(left - 1, pt.y))
			left--;
		int right = pt.x;
		while (right < clipRect.right - 1 && canFill(right + 1, pt.y))
			right++;

		for (int x = left; x <= right; x++) {
			visited[(pt.y - clipRect.top) * width + (x - clipRect.left)] = true;
			setPixel(x, pt.y);
		}

		for (int dy = -1; dy <= 1; dy += 2) {
			int y = pt.y + dy;
			if (y < clipRect.top || y >= clipRect.top + height)
				continue;

			bool inRun = false;
			for (int x = left; x <= right; x++) {
				bool fillable = canFill(x, y);
				if (fillable && !inRun)
					stack.push_back(Common::Point(x, y));
				inRun = fillable;
			}
		}
	}
}

#ifdef MTROPOLIS_DEBUG_ENABLE
void FillModModifier::debugInspect(IDebugInspectionReport *report) const {
	Modifier::debugInspect(report);

	report->declareDynamic("numFills", Common::String::format("%u", _fills.size()));
	report->declareDynamic("color", Common::String::format("%06x", _color));
	report->declareDynamic("active", _isActive ? "true" : "false");
}
#endif

Common::SharedPtr<Modifier> FillModModifier::shallowClone() const {
	return Common::SharedPtr<Modifier>(new FillModModifier(*this));
}

const char *FillModModifier::getDefaultName() const {
	return "FillMod";
}

AVICtrlModifier::VideoWindow::VideoWindow(const WindowParameters &windowParams, AVICtrlModifier *owner) : Window(windowParams), _owner(owner) {
}

void AVICtrlModifier::VideoWindow::onMouseDown(int32 x, int32 y, int mouseButton) {
	// The original's video window flags the click and its task sends the
	// message later, so the message is sent from playMedia here
	if (_owner && mouseButton == Actions::kMouseButtonLeft)
		_owner->_clicked = true;
}

void AVICtrlModifier::VideoWindow::detachOwner() {
	_owner = nullptr;
}

AVICtrlModifier::AVICtrlModifier() : _ignorePalette(false), _runtime(nullptr), _isPlaying(false), _clicked(false) {
}

AVICtrlModifier::AVICtrlModifier(const AVICtrlModifier &other)
	: Modifier(other), _playWhen(other._playWhen), _stopWhen(other._stopWhen), _sendOnClick(other._sendOnClick),
	  _sendWhenDone(other._sendWhenDone), _moviePath(other._moviePath), _ignorePalette(other._ignorePalette),
	  _runtime(nullptr), _isPlaying(false), _clicked(false) {
}

AVICtrlModifier::~AVICtrlModifier() {
	stopPlaying();
}

bool AVICtrlModifier::load(const PlugInModifierLoaderContext &context, const Data::KW::AVICtrlModifier &data) {
	if (!loadDlgMgrEvent(_playWhen, data.playWhen) || !loadDlgMgrEvent(_stopWhen, data.stopWhen))
		return false;

	// Destinations are stored as labels, with the destination in the label ID
	if (data.clickDest.type != Data::PlugInTypeTaggedValue::kLabel || data.doneDest.type != Data::PlugInTypeTaggedValue::kLabel)
		return false;

	Data::PlugInTypeTaggedValue with;
	with.type = Data::PlugInTypeTaggedValue::kNull;

	if (!_sendOnClick.load(data.clickEvent, MessageFlags(), with, data.clickDest.value.asLabel.labelID))
		return false;
	if (!_sendWhenDone.load(data.doneEvent, MessageFlags(), with, data.doneDest.value.asLabel.labelID))
		return false;

	_moviePath = dlgMgrString(data.moviePath);

	return true;
}

bool AVICtrlModifier::respondsToEvent(const Event &evt) const {
	return _playWhen.respondsTo(evt) || _stopWhen.respondsTo(evt) || evt.eventType == EventIDs::kParentDisabled;
}

VThreadState AVICtrlModifier::consumeMessage(Runtime *runtime, const Common::SharedPtr<MessageProperties> &msg) {
	const Event &evt = msg->getEvent();

	if (_playWhen.respondsTo(evt)) {
		startPlaying(runtime);
	} else if (_stopWhen.respondsTo(evt)) {
		// The original sends the done message on every stop, even when nothing
		// plays. Bus sends stop to all ten movies when a transition is
		// cancelled, and moves on by itself, so only a playing movie reports.
		if (_isPlaying) {
			stopPlaying();
			sendMessage(runtime, _sendWhenDone);
		}
	} else if (evt.eventType == EventIDs::kParentDisabled) {
		// Don't send messages into a scene that is going away
		stopPlaying();
	}

	return kVThreadReturn;
}

void AVICtrlModifier::disable(Runtime *runtime) {
	stopPlaying();
}

bool AVICtrlModifier::readAttribute(MiniscriptThread *thread, DynamicValue &result, const Common::String &attrib) {
	if (attrib == "moviepath") {
		result.setString(_moviePath);
		return true;
	}

	return Modifier::readAttribute(thread, result, attrib);
}

MiniscriptInstructionOutcome AVICtrlModifier::writeRefAttribute(MiniscriptThread *thread, DynamicValueWriteProxy &result, const Common::String &attrib) {
	if (attrib == "moviepath") {
		DynamicValueWriteStringHelper::create(&_moviePath, result);
		return kMiniscriptInstructionOutcomeContinue;
	}
	if (attrib == "ignorepalette") {
		DynamicValueWriteBoolHelper::create(&_ignorePalette, result);
		return kMiniscriptInstructionOutcomeContinue;
	}

	// play and stop are declared but do nothing in the original. init reopens
	// the movie, which here is opened on each play anyway.
	if (attrib == "play" || attrib == "stop" || attrib == "init") {
		DynamicValueWriteDiscardHelper::create(result);
		return kMiniscriptInstructionOutcomeContinue;
	}

	return Modifier::writeRefAttribute(thread, result, attrib);
}

void AVICtrlModifier::startPlaying(Runtime *runtime) {
	stopPlaying();

	Common::SharedPtr<Window> mainWindow = runtime->getMainWindow().lock();
	Structural *owner = findStructuralOwner();
	if (!mainWindow || !owner || !owner->isElement() || !static_cast<Element *>(owner)->isVisual())
		return;

	Common::SeekableReadStream *stream = runtime->getProject()->createReadStreamForProjectPath(_moviePath);
	if (!stream) {
		// The original would never finish, which would leave the title stuck
		warning("AVICtrl: Couldn't open movie '%s'", _moviePath.c_str());
		sendMessage(runtime, _sendWhenDone);
		return;
	}

	Common::SharedPtr<Video::AVIDecoder> decoder(new Video::AVIDecoder());
	if (!decoder->loadStream(stream) || decoder->getWidth() == 0 || decoder->getHeight() == 0) {
		warning("AVICtrl: Couldn't load movie '%s'", _moviePath.c_str());
		sendMessage(runtime, _sendWhenDone);
		return;
	}

	const Graphics::PixelFormat &format = mainWindow->getPixelFormat();
	if (!format.isCLUT8())
		decoder->setOutputPixelFormat(format);

	// The movie plays at its own size with its top-left on the element
	const Common::Point pos = static_cast<VisualElement *>(owner)->getGlobalPosition();

	_runtime = runtime;
	_decoder = decoder;
	_window.reset(new VideoWindow(WindowParameters(runtime, mainWindow->getX() + pos.x, mainWindow->getY() + pos.y, decoder->getWidth(), decoder->getHeight(), format), this));
	_window->setStrata(1);
	_window->getSurface()->fillRect(Common::Rect(0, 0, decoder->getWidth(), decoder->getHeight()), 0);
	_isPlaying = true;
	_clicked = false;

	debug(2, "AVICtrl: Playing '%s' (%ix%i) at (%i,%i)", _moviePath.c_str(), decoder->getWidth(), decoder->getHeight(), pos.x, pos.y);

	decoder->start();
	runtime->addWindow(_window);
	_playMediaSignaller = runtime->getProject()->notifyOnPlayMedia(this);
}

void AVICtrlModifier::stopPlaying() {
	if (!_isPlaying)
		return;

	_isPlaying = false;
	_clicked = false;

	if (_playMediaSignaller) {
		_playMediaSignaller->removeReceiver(this);
		_playMediaSignaller.reset();
	}

	if (_window) {
		_window->detachOwner();
		if (_runtime)
			_runtime->removeWindow(_window.get());
		_window.reset();
	}

	_decoder.reset();
}

void AVICtrlModifier::sendMessage(Runtime *runtime, const MessengerSendSpec &spec) {
	// Event ID 1 means no message
	if (static_cast<uint32>(spec.send.eventType) == 1 || spec.send.eventType == EventIDs::kNothing)
		return;

	spec.sendFromMessenger(runtime, this, nullptr, DynamicValue(), nullptr);
}

void AVICtrlModifier::drawFrame(const Graphics::Surface &frame) {
	Graphics::ManagedSurface &dest = *_window->getSurface();

	if (frame.format == dest.format) {
		dest.blitFrom(frame);
		return;
	}

	if (!dest.format.isCLUT8()) {
		Graphics::Surface *converted = frame.convertTo(dest.format, _decoder->getPalette());
		dest.blitFrom(*converted);
		converted->free();
		delete converted;
		return;
	}

	// On an 8-bit display, map the true colour frame to the nearest colours
	// of the global palette, through a 15-bit lookup filled in as needed
	const byte *pal = _runtime->getGlobalPalette().getPalette();
	if (_colorMap.size() != 32768 * 2) {
		_colorMap.resize(32768 * 2);
		for (uint i = 0; i < 32768; i++)
			_colorMap[i * 2 + 1] = 0;
	}

	const int w = MIN<int>(frame.w, dest.w);
	const int h = MIN<int>(frame.h, dest.h);
	for (int y = 0; y < h; y++) {
		byte *destRow = static_cast<byte *>(dest.getBasePtr(0, y));
		for (int x = 0; x < w; x++) {
			uint8 r, g, b;
			frame.format.colorToRGB(frame.getPixel(x, y), r, g, b);

			uint key = ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3);
			if (!_colorMap[key * 2 + 1]) {
				int bestDist = INT_MAX;
				uint best = 0;
				for (uint i = 0; i < 256; i++) {
					int dr = pal[i * 3] - r;
					int dg = pal[i * 3 + 1] - g;
					int db = pal[i * 3 + 2] - b;
					int dist = dr * dr + dg * dg + db * db;
					if (dist < bestDist) {
						bestDist = dist;
						best = i;
					}
				}
				_colorMap[key * 2] = best;
				_colorMap[key * 2 + 1] = 1;
			}

			destRow[x] = _colorMap[key * 2];
		}
	}
}

void AVICtrlModifier::playMedia(Runtime *runtime, Project *project) {
	if (!_isPlaying)
		return;

	// A child window without a class cursor keeps the stage's cursor, which
	// Bugs hides during transitions
	Common::SharedPtr<Window> mainWindow = runtime->getMainWindow().lock();
	if (mainWindow) {
		_window->setCursorGraphic(mainWindow->getCursorGraphic());
		_window->setMouseVisible(mainWindow->getMouseVisible());
	}

	while (_isPlaying && _decoder->needsUpdate()) {
		const Graphics::Surface *frame = _decoder->decodeNextFrame();
		if (!frame)
			break;

		drawFrame(*frame);
	}

	// A click doesn't stop the movie, the title decides what to do
	if (_clicked) {
		_clicked = false;
		debug(2, "AVICtrl: '%s' clicked", _moviePath.c_str());
		sendMessage(runtime, _sendOnClick);
	}

	if (_isPlaying && _decoder->endOfVideo()) {
		debug(2, "AVICtrl: '%s' finished", _moviePath.c_str());
		stopPlaying();
		sendMessage(runtime, _sendWhenDone);
	}
}

#ifdef MTROPOLIS_DEBUG_ENABLE
void AVICtrlModifier::debugInspect(IDebugInspectionReport *report) const {
	Modifier::debugInspect(report);

	report->declareDynamic("moviePath", _moviePath);
	report->declareDynamic("isPlaying", _isPlaying ? "true" : "false");
}
#endif

Common::SharedPtr<Modifier> AVICtrlModifier::shallowClone() const {
	return Common::SharedPtr<Modifier>(new AVICtrlModifier(*this));
}

const char *AVICtrlModifier::getDefaultName() const {
	return "AVICtrl";
}

void AVICtrlModifier::linkInternalReferences(ObjectLinkingScope *scope) {
	Modifier::linkInternalReferences(scope);
	_sendOnClick.linkInternalReferences(scope);
	_sendWhenDone.linkInternalReferences(scope);
}

void AVICtrlModifier::visitInternalReferences(IStructuralReferenceVisitor *visitor) {
	Modifier::visitInternalReferences(visitor);
	_sendOnClick.visitInternalReferences(visitor);
	_sendWhenDone.visitInternalReferences(visitor);
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

// Wall tile cels by the mask of open neighbours: 0x01 up-left, 0x02 up,
// 0x04 up-right, 0x08 left, 0x10 right, 0x20 down-left, 0x40 down, 0x80 down-right
static const byte kAntMazeWallCels[256] = {
	46, 30, 23, 23, 29, 33, 23, 23, 15, 15, 3, 3, 18, 18, 3, 3,
	19, 21, 5, 5, 19, 21, 5, 5, 1, 1, 44, 44, 1, 1, 44, 44,
	28, 36, 25, 25, 32, 38, 25, 25, 15, 15, 3, 3, 18, 18, 3, 3,
	22, 20, 6, 6, 22, 20, 6, 6, 1, 1, 44, 44, 1, 1, 44, 44,
	11, 14, 2, 2, 13, 12, 2, 2, 7, 7, 43, 43, 8, 8, 43, 43,
	9, 10, 41, 41, 9, 10, 41, 41, 42, 42, 45, 45, 42, 42, 45, 45,
	11, 14, 2, 2, 13, 12, 2, 2, 7, 7, 43, 43, 8, 8, 43, 43,
	9, 10, 41, 41, 9, 10, 41, 41, 42, 42, 45, 45, 42, 42, 45, 45,
	27, 31, 26, 26, 34, 38, 26, 26, 17, 17, 4, 4, 16, 16, 4, 4,
	19, 21, 5, 5, 19, 21, 5, 5, 1, 1, 44, 44, 1, 1, 44, 44,
	35, 40, 24, 24, 39, 38, 24, 24, 17, 17, 4, 4, 16, 16, 4, 4,
	22, 20, 6, 6, 22, 20, 6, 6, 1, 1, 44, 44, 1, 1, 44, 44,
	11, 14, 2, 2, 13, 12, 2, 2, 7, 7, 43, 43, 8, 8, 43, 43,
	9, 10, 41, 41, 9, 10, 41, 41, 42, 42, 45, 45, 42, 42, 45, 45,
	11, 14, 2, 2, 13, 12, 2, 2, 7, 7, 43, 43, 8, 8, 43, 43,
	9, 10, 41, 41, 9, 10, 41, 41, 42, 42, 45, 45, 42, 42, 45, 45,
};

// Cels of the ant and the enemies by direction
static const int kAntMazeCelRight = 1;
static const int kAntMazeCelLeft = 4;
static const int kAntMazeCelDown = 7;
static const int kAntMazeCelUp = 10;
static const int kAntMazeCarryOffset = 19;
static const int kAntMazeMunchCel = 37;
static const int kAntMazeMunchTicks = 22;
static const int kAntMazeOpenCel = 47;
static const int kAntMazeTileSize = 30;
static const int kAntMazeHidden = 500;

AntMazeModifier::Enemy::Enemy() : startTick(-1), loop(0), idx(0), reversing(false), cel(kAntMazeCelRight) {
}

AntMazeModifier::AntMazeModifier()
	: _levelsLoaded(false), _gameLevel(0), _paused(false), _running(false), _levelReady(false), _tickCount(0), _antCel(kAntMazeCelRight),
	  _munchTicks(0), _foodCount(0), _foodLeft(0), _carried(0), _warpCount(0), _enemyCount(0), _antLionCount(0), _switchCount(0),
	  _munchCount(0), _tokenCount(0), _runtime(nullptr) {
	memset(_map, '*', sizeof(_map));
}

AntMazeModifier::AntMazeModifier(const AntMazeModifier &other)
	: Modifier(other), _startWhen(other._startWhen), _stopWhen(other._stopWhen), _levels(other._levels), _levelsLoaded(other._levelsLoaded),
	  _gameLevel(0), _paused(false), _running(false), _levelReady(false), _tickCount(0), _antCel(kAntMazeCelRight), _munchTicks(0),
	  _foodCount(0), _foodLeft(0), _carried(0), _warpCount(0), _enemyCount(0), _antLionCount(0), _switchCount(0), _munchCount(0),
	  _tokenCount(0), _runtime(nullptr) {
	memset(_map, '*', sizeof(_map));
}

AntMazeModifier::~AntMazeModifier() {
	stopGame();
}

bool AntMazeModifier::load(const PlugInModifierLoaderContext &context, const Data::KW::GameModifier &data) {
	if (!loadDlgMgrEvent(_startWhen, data.enableWhen) || !loadDlgMgrEvent(_stopWhen, data.disableWhen))
		return false;

	return true;
}

// The levels are records in the plug-in's data section pointing at the map
// and enemy route strings
bool AntMazeModifier::loadLevels() {
	if (_levelsLoaded)
		return !_levels.empty();

	_levelsLoaded = true;

	Common::File f;
	if (!f.open(Common::Path("mTrop_Resources/AntMaze.r32")) && !f.open(Common::Path("AntMaze.r32"))) {
		warning("AntMaze: AntMaze.r32 from the installed game is needed for the levels");
		return false;
	}

	Common::Array<byte> image;
	image.resize(f.size());
	f.read(&image[0], image.size());

	bool failed = false;
	auto readS16 = [&](uint32 va) -> int16 {
		uint32 offset = va - 0x10000000;
		if (offset + 2 > image.size()) {
			failed = true;
			return 0;
		}
		return static_cast<int16>(READ_LE_UINT16(&image[offset]));
	};
	auto readU32 = [&](uint32 va) -> uint32 {
		uint32 offset = va - 0x10000000;
		if (offset + 4 > image.size()) {
			failed = true;
			return 0;
		}
		return READ_LE_UINT32(&image[offset]);
	};
	auto readString = [&](uint32 va) -> Common::String {
		Common::String str;
		for (uint32 offset = va - 0x10000000; offset < image.size(); offset++) {
			if (!image[offset])
				return str;
			str += static_cast<char>(image[offset]);
		}
		failed = true;
		return str;
	};

	Common::Array<Level> levels;
	levels.resize(kNumLevels + 1);
	for (uint l = 1; l <= kNumLevels; l++) {
		Level &level = levels[l];
		uint32 base = 0x10009f82 + l * 0xae;

		level.map = readString(readU32(base));
		if (level.map.size() != kCols * kRows)
			failed = true;

		for (int i = 0; i < kMaxWarps; i++) {
			uint32 warp = base + 4 + i * 8;
			level.warps[i].pos = Common::Point(readS16(warp + 2), readS16(warp));
			level.warps[i].dest = Common::Point(readS16(warp + 6), readS16(warp + 4));
		}

		for (int i = 0; i < kMaxEnemies; i++) {
			uint32 enemy = base + 0x64 + i * 8;
			level.enemies[i].startTick = readS16(enemy);
			level.enemies[i].path = readString(readU32(enemy + 2));
			level.enemies[i].loop = readS16(enemy + 6);
		}

		for (int i = 0; i < kMaxSwitches; i++) {
			uint32 sw = base + 0x7c + i * 10;
			level.switches[i].pos = Common::Point(readS16(sw + 2), readS16(sw));
			level.switches[i].target = Common::Point(readS16(sw + 6), readS16(sw + 4));
			level.switches[i].newChar = static_cast<char>(readS16(sw + 8));
		}
	}

	// Level 1 has its nest in column 7 and one enemy leaving after 180 ticks
	if (failed || !levels[1].map.hasPrefix("*******E*") || levels[1].enemies[0].startTick != 180 || levels[1].enemies[0].loop != 2
		|| !levels[1].enemies[0].path.hasSuffix("!") || levels[1].enemies[1].startTick != -1) {
		warning("AntMaze: AntMaze.r32 isn't the expected version");
		return false;
	}

	_levels.swap(levels);
	return true;
}

bool AntMazeModifier::respondsToEvent(const Event &evt) const {
	return _startWhen.respondsTo(evt) || _stopWhen.respondsTo(evt) || evt.eventType == EventIDs::kParentDisabled || evt.eventType == EventIDs::kMouseDown;
}

VThreadState AntMazeModifier::consumeMessage(Runtime *runtime, const Common::SharedPtr<MessageProperties> &msg) {
	const Event &evt = msg->getEvent();

	if (_startWhen.respondsTo(evt)) {
		_runtime = runtime;
		if (!_running) {
			_running = true;
			_keySignaller = runtime->getProject()->notifyOnKeyboardEvent(this);
			scheduleTick(runtime);
		}
		_paused = false;
		return kVThreadReturn;
	}

	if (_stopWhen.respondsTo(evt) || evt.eventType == EventIDs::kParentDisabled) {
		stopGame();
		return kVThreadReturn;
	}

	if (evt.eventType == EventIDs::kMouseDown) {
		const DynamicValue &value = msg->getValue();
		handleClick(runtime, (value.getType() == DynamicValueTypes::kPoint) ? value.getPoint() : runtime->getCachedMousePosition());
	}

	return kVThreadReturn;
}

void AntMazeModifier::onKeyboardEvent(Runtime *runtime, const KeyboardInputEvent &keyEvt) {
	Common::EventType type = keyEvt.getKeyEventType();
	if (!_running || _munchTicks > 0 || (type != Common::EVENT_KEYDOWN && type != Common::EVENT_KEYUP))
		return;

	// Any key, up or down, while paused
	if (_paused) {
		sendMessage(runtime, "PausedClick");
		return;
	}

	// Moves repeat with the key
	if (type != Common::EVENT_KEYDOWN || !_levelReady)
		return;

	switch (keyEvt.getKeyState().keycode) {
	case Common::KEYCODE_UP:
	case Common::KEYCODE_KP8:
		if (_ant.y < 1 || mapAt(_ant.x, _ant.y - 1) == '*')
			return;
		_ant.y--;
		_antCel = kAntMazeCelUp;
		break;
	case Common::KEYCODE_LEFT:
	case Common::KEYCODE_KP4:
		if (_ant.x < 1 || mapAt(_ant.x - 1, _ant.y) == '*')
			return;
		_ant.x--;
		_antCel = kAntMazeCelLeft;
		break;
	case Common::KEYCODE_RIGHT:
	case Common::KEYCODE_KP6:
		if (_ant.x > kCols - 2 || mapAt(_ant.x + 1, _ant.y) == '*')
			return;
		_ant.x++;
		_antCel = kAntMazeCelRight;
		break;
	case Common::KEYCODE_DOWN:
	case Common::KEYCODE_KP2:
		if (_ant.y > kRows - 2 || mapAt(_ant.x, _ant.y + 1) == '*')
			return;
		_ant.y++;
		_antCel = kAntMazeCelDown;
		break;
	default:
		return;
	}

	arrive(runtime);
}

// A click one or two squares away in a straight line moves the ant one square
void AntMazeModifier::handleClick(Runtime *runtime, const Common::Point &mousePos) {
	if (_munchTicks > 0)
		return;

	if (_paused) {
		sendMessage(runtime, "PausedClick");
		return;
	}

	if (!_levelReady)
		return;

	// Truncates toward zero like the original
	int col = (mousePos.x - _origin.x) / kAntMazeTileSize;
	int row = (mousePos.y - _origin.y) / kAntMazeTileSize;
	if (col < 1 || col > kCols - 2 || row < 0 || row > kRows - 1)
		return;

	if (col == _ant.x) {
		if ((row == _ant.y + 1 || row == _ant.y + 2) && mapAt(_ant.x, _ant.y + 1) != '*') {
			_ant.y++;
			_antCel = kAntMazeCelDown;
		} else if ((row == _ant.y - 1 || row == _ant.y - 2) && mapAt(_ant.x, _ant.y - 1) != '*') {
			_ant.y--;
			_antCel = kAntMazeCelUp;
		} else
			return;
	} else if (row == _ant.y) {
		if ((col == _ant.x + 1 || col == _ant.x + 2) && mapAt(_ant.x + 1, _ant.y) != '*') {
			_ant.x++;
			_antCel = kAntMazeCelRight;
		} else if ((col == _ant.x - 1 || col == _ant.x - 2) && mapAt(_ant.x - 1, _ant.y) != '*') {
			_ant.x--;
			_antCel = kAntMazeCelLeft;
		} else
			return;
	} else
		return;

	arrive(runtime);
}

void AntMazeModifier::disable(Runtime *runtime) {
	stopGame();
}

void AntMazeModifier::stopGame() {
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

void AntMazeModifier::scheduleTick(Runtime *runtime) {
	// The original ticks every 83 milliseconds
	_scheduledEvent = runtime->getScheduler().scheduleMethod<AntMazeModifier, &AntMazeModifier::tick>(runtime->getPlayTime() + 83, this);
}

bool AntMazeModifier::readAttribute(MiniscriptThread *thread, DynamicValue &result, const Common::String &attrib) {
	if (attrib == "paused") {
		result.setBool(_paused);
		return true;
	} else if (attrib == "gamelevel") {
		result.setInt(_gameLevel);
		return true;
	}

	return Modifier::readAttribute(thread, result, attrib);
}

MiniscriptInstructionOutcome AntMazeModifier::writeRefAttribute(MiniscriptThread *thread, DynamicValueWriteProxy &result, const Common::String &attrib) {
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

MiniscriptInstructionOutcome AntMazeModifier::WriteProxy::write(MiniscriptThread *thread, const DynamicValue &assigned, void *objectRef, uintptr ptrOrOffset) {
	AntMazeModifier *self = static_cast<AntMazeModifier *>(objectRef);

	// Variables are passed by reference
	DynamicValue value = assigned;
	if (value.getType() == DynamicValueTypes::kObject) {
		Common::SharedPtr<RuntimeObject> obj = value.getObject().object.lock();
		if (obj && obj->isModifier() && static_cast<Modifier *>(obj.get())->isVariable())
			static_cast<VariableModifier *>(obj.get())->varGetValue(value);
	}

	if (ptrOrOffset == kAttribPaused) {
		if (value.getType() == DynamicValueTypes::kBoolean)
			self->_paused = value.getBool();
		else if (value.getType() == DynamicValueTypes::kInteger)
			self->_paused = (value.getInt() != 0);
		else
			return kMiniscriptInstructionOutcomeFailed;
	} else {
		int32 level = 0;
		if (!value.roundToInt(level))
			return kMiniscriptInstructionOutcomeFailed;

		// Levels out of range are kept but set nothing up
		self->_gameLevel = level;
		self->setupLevel(thread->getRuntime());
	}

	return kMiniscriptInstructionOutcomeContinue;
}

MiniscriptInstructionOutcome AntMazeModifier::WriteProxy::refAttrib(MiniscriptThread *thread, DynamicValueWriteProxy &proxy, void *objectRef, uintptr ptrOrOffset, const Common::String &attrib) {
	return kMiniscriptInstructionOutcomeFailed;
}

MiniscriptInstructionOutcome AntMazeModifier::WriteProxy::refAttribIndexed(MiniscriptThread *thread, DynamicValueWriteProxy &proxy, void *objectRef, uintptr ptrOrOffset, const Common::String &attrib, const DynamicValue &index) {
	return kMiniscriptInstructionOutcomeFailed;
}

// Children of the owner element: 1 tiles, 2 ant, 3 food, 4 queen, 5 warp pads,
// 6-8 enemies, 9 ant lions, 10 switches, 11 munch tunnels, 12 chance tokens,
// 13 nest entrance
Structural *AntMazeModifier::child(int index) const {
	Structural *owner = findStructuralOwner();
	if (!owner || index < 1 || index > static_cast<int>(owner->getChildren().size()))
		return nullptr;
	return owner->getChildren()[index - 1].get();
}

Structural *AntMazeModifier::grandchild(int index, int subIndex) const {
	Structural *container = child(index);
	if (!container || subIndex < 1 || subIndex > static_cast<int>(container->getChildren().size()))
		return nullptr;
	return container->getChildren()[subIndex - 1].get();
}

void AntMazeModifier::placeCentered(Runtime *runtime, Structural *element, int32 x, int32 y) {
	if (!element || !element->isElement() || !static_cast<Element *>(element)->isVisual())
		return;

	const Common::Rect &rect = static_cast<VisualElement *>(element)->getRelativeRect();
	DynamicValue pos;
	pos.setPoint(Common::Point(x - rect.width() / 2, y - rect.height() / 2));
	dlgMgrWriteAttribute(runtime, this, element, "position", pos);
}

void AntMazeModifier::hide(Runtime *runtime, Structural *element) {
	placeCentered(runtime, element, kAntMazeHidden, kAntMazeHidden);
}

void AntMazeModifier::setCel(Runtime *runtime, Structural *element, int32 cel) {
	dlgMgrSetInt(runtime, this, element, "cel", cel);
}

void AntMazeModifier::sendMessage(Runtime *runtime, const char *messageName) {
	uint32 messageID = 0;
	if (!runtime->getProject()->findAuthorMessageID(messageName, messageID))
		return;

	Structural *owner = findStructuralOwner();
	if (!owner)
		return;

	debug(2, "AntMaze: Sending %s", messageName);

	Common::SharedPtr<MessageProperties> props(new MessageProperties(Event(EventIDs::kAuthorMessage, messageID), DynamicValue(), getSelfReference()));
	Common::SharedPtr<MessageDispatch> dispatch(new MessageDispatch(props, owner, true, true, false));
	runtime->queueMessage(dispatch);
}

char AntMazeModifier::mapAt(int col, int row) const {
	if (col < 0 || col >= kCols || row < 0 || row >= kRows)
		return '*';
	return _map[col][row];
}

const Common::Point *AntMazeModifier::findWarp(const Common::Point &pos) const {
	for (int i = 0; i < _warpCount; i++) {
		if (_warps[i] == pos)
			return &_warpDests[i];
	}
	return nullptr;
}

Common::Point AntMazeModifier::tileCenter(int col, int row) const {
	Structural *tile = grandchild(1, row * kCols + col + 1);
	if (!tile || !tile->isElement() || !static_cast<Element *>(tile)->isVisual())
		return Common::Point(0, 0);

	// As VisualElement::getCenterPosition()
	const Common::Rect &rect = static_cast<VisualElement *>(tile)->getRelativeRect();
	return Common::Point((rect.left + rect.right) / 2, (rect.top + rect.bottom) / 2);
}

void AntMazeModifier::placeAnt(Runtime *runtime) {
	Common::Point center = tileCenter(_ant.x, _ant.y);
	setCel(runtime, child(2), _antCel);
	placeCentered(runtime, child(2), center.x, center.y);
}

// Picks a wall shape from the open squares around it
void AntMazeModifier::refreshTile(Runtime *runtime, int col, int row) {
	if (col < 0 || col >= kCols || row < 0 || row >= kRows)
		return;

	int cel = kAntMazeOpenCel;
	if (_map[col][row] == '*') {
		static const int kNeighbours[8][2] = {{-1, -1}, {0, -1}, {1, -1}, {-1, 0}, {1, 0}, {-1, 1}, {0, 1}, {1, 1}};
		uint mask = 0;
		for (uint i = 0; i < 8; i++) {
			int c = col + kNeighbours[i][0];
			int r = row + kNeighbours[i][1];
			if (c >= 0 && c < kCols && r >= 0 && r < kRows && _map[c][r] != '*')
				mask |= (1 << i);
		}
		cel = kAntMazeWallCels[mask];
	}

	setCel(runtime, grandchild(1, row * kCols + col + 1), cel);
}

void AntMazeModifier::setupLevel(Runtime *runtime) {
	_runtime = runtime;
	if (_gameLevel < 1 || _gameLevel > kNumLevels || !findStructuralOwner() || !loadLevels())
		return;

	const Level &level = _levels[_gameLevel];

	for (int r = 0; r < kRows; r++) {
		for (int c = 0; c < kCols; c++)
			_map[c][r] = level.map[r * kCols + c];
	}

	for (int k = 1; k <= kMaxFood; k++)
		hide(runtime, grandchild(3, k));
	_foodCount = _foodLeft = _carried = 0;

	for (int k = 1; k <= kMaxWarps; k++)
		hide(runtime, grandchild(5, k));
	_warpCount = 0;

	for (int i = 0; i < kMaxEnemies; i++)
		hide(runtime, child(6 + i));
	_enemyCount = 0;

	for (int k = 1; k <= kMaxAntLions; k++)
		hide(runtime, grandchild(9, k));
	_antLionCount = 0;

	for (int k = 1; k <= kMaxSwitches; k++) {
		hide(runtime, grandchild(10, k));
		setCel(runtime, grandchild(10, k), 1);
	}
	_switchCount = 0;

	for (int k = 1; k <= kMaxMunch; k++)
		hide(runtime, grandchild(11, k));
	_munchCount = 0;

	for (int k = 1; k <= kMaxTokens; k++)
		hide(runtime, grandchild(12, k));
	_tokenCount = 0;

	for (int r = 0; r < kRows; r++) {
		for (int c = 0; c < kCols; c++) {
			const Common::Point pos(c, r);
			dlgMgrSetBool(runtime, this, grandchild(1, r * kCols + c + 1), "paused", true);
			refreshTile(runtime, c, r);

			const Common::Point center = tileCenter(c, r);
			switch (_map[c][r]) {
			case 'A':
				if (_antLionCount < kMaxAntLions) {
					_antLions[_antLionCount++] = pos;
					placeCentered(runtime, grandchild(9, _antLionCount), center.x, center.y);
				}
				break;
			case 'C':
				if (_tokenCount < kMaxTokens) {
					_tokens[_tokenCount++] = pos;
					placeCentered(runtime, grandchild(12, _tokenCount), center.x, center.y);
				}
				break;
			case 'E':
				for (Enemy &enemy : _enemies)
					enemy.pos = pos;
				placeCentered(runtime, child(13), center.x - 3, center.y - 28);
				break;
			case 'F':
				if (_foodCount < kMaxFood) {
					_food[_foodCount++] = pos;
					placeCentered(runtime, grandchild(3, _foodCount), center.x, center.y);
					_foodLeft++;
				}
				break;
			case 'M':
				if (_munchCount < kMaxMunch) {
					_munch[_munchCount++] = pos;
					placeCentered(runtime, grandchild(11, _munchCount), center.x, center.y);
				}
				break;
			case 'Q':
				setCel(runtime, child(4), 1);
				placeCentered(runtime, child(4), center.x + 8, center.y);
				break;
			case 'q':
				setCel(runtime, child(4), 4);
				placeCentered(runtime, child(4), center.x - 8, center.y);
				break;
			case 'S':
				_ant = _antStart = pos;
				_antCel = kAntMazeCelRight;
				_munchTicks = 0;
				placeCentered(runtime, child(2), center.x, center.y);
				setCel(runtime, child(2), _antCel);
				break;
			case 'T':
				// One square can throw several switches
				for (const SwitchRecord &record : level.switches) {
					if (record.pos != pos || _switchCount >= kMaxSwitches)
						continue;
					Switch &sw = _switches[_switchCount++];
					sw.pos = pos;
					sw.target = record.target;
					sw.newChar = record.newChar;
					sw.used = false;
					placeCentered(runtime, grandchild(10, _switchCount), center.x, center.y);
				}
				break;
			case 'W':
				if (_warpCount < kMaxWarps) {
					_warps[_warpCount] = pos;
					for (const WarpRecord &record : level.warps) {
						if (record.pos == pos) {
							_warpDests[_warpCount] = record.dest;
							break;
						}
					}
					_warpCount++;
					placeCentered(runtime, grandchild(5, _warpCount), center.x, center.y + 6);
				}
				break;
			default:
				break;
			}
		}
	}

	for (int i = 0; i < kMaxEnemies; i++) {
		Enemy &enemy = _enemies[i];
		enemy.startTick = level.enemies[i].startTick;
		enemy.path = level.enemies[i].path;
		enemy.loop = level.enemies[i].loop;
		enemy.idx = 0;
		enemy.reversing = false;
		if (enemy.startTick > 0)
			_enemyCount++;
	}

	Structural *firstTile = grandchild(1, 1);
	if (firstTile && firstTile->isElement() && static_cast<Element *>(firstTile)->isVisual()) {
		const Common::Rect &rect = static_cast<VisualElement *>(firstTile)->getRelativeRect();
		_origin = Common::Point(rect.left, rect.top);
	}

	_tickCount = 0;
	_levelReady = true;

	debug(2, "AntMaze: Level %i with %i food, %i enemies", static_cast<int>(_gameLevel), _foodCount, _enemyCount);
}

// Effects of the square the ant just stepped onto
void AntMazeModifier::arrive(Runtime *runtime) {
	int sfx = 0;

	switch (mapAt(_ant.x, _ant.y)) {
	case 'C':
		for (int i = 0; i < _tokenCount; i++) {
			if (_tokens[i] == _ant) {
				hide(runtime, grandchild(12, i + 1));
				_tokens[i] = Common::Point(-1, -1);
				sendMessage(runtime, "ExtraChance");
			}
		}
		break;
	case 'F':
		for (int i = 0; i < _foodCount; i++) {
			if (_food[i] == _ant) {
				hide(runtime, grandchild(3, i + 1));
				_food[i] = Common::Point(-1, -1);
				_foodLeft--;
				_carried++;
				sfx = 1;
			}
		}
		break;
	case 'M':
		for (int i = 0; i < _munchCount; i++) {
			if (_munch[i] == _ant) {
				hide(runtime, grandchild(11, i + 1));
				_munch[i] = Common::Point(-1, -1);
				_munchTicks = kAntMazeMunchTicks;
				_antCel = kAntMazeMunchCel;
				sfx = 5;
			}
		}
		break;
	case 'Q':
	case 'q':
	case 'x':
		// The win only needs all the food picked up, not delivered
		if (_foodLeft < 1) {
			sendMessage(runtime, "YouWin");
			sfx = 2;
		} else if (_carried > 0)
			sendMessage(runtime, "GotFood");
		_carried = 0;
		break;
	case 'T':
		for (int i = 0; i < _switchCount; i++) {
			Switch &sw = _switches[i];
			if (sw.pos != _ant)
				continue;

			if (sw.target.x >= 0 && sw.target.x < kCols && sw.target.y >= 0 && sw.target.y < kRows)
				_map[sw.target.x][sw.target.y] = sw.newChar;

			// The original skips row 0 here
			for (int c = sw.target.x - 1; c <= sw.target.x + 1; c++) {
				for (int r = sw.target.y - 1; r <= sw.target.y + 1; r++) {
					if (r > 0 && r < kRows)
						refreshTile(runtime, c, r);
				}
			}

			setCel(runtime, grandchild(10, i + 1), 2);
			if (!sw.used) {
				sfx = 4;
				sw.used = true;
			}
		}
		break;
	case 'W':
		if (const Common::Point *dest = findWarp(_ant)) {
			sfx = 3;
			_ant = *dest;
		}
		break;
	default:
		break;
	}

	if (_munchTicks == 0 && _carried > 0)
		_antCel += kAntMazeCarryOffset;
	placeAnt(runtime);

	bool hit = false;
	for (int i = 0; i < _enemyCount; i++) {
		if (_enemies[i].pos == _ant && _enemies[i].startTick < _tickCount)
			hit = true;
	}
	for (int i = 0; i < _antLionCount; i++) {
		if (_antLions[i] == _ant)
			hit = true;
	}

	if (hit) {
		loseChance(runtime);
		return;
	}

	if (sfx > 0)
		sendMessage(runtime, Common::String::format("PlaySFX%i", sfx).c_str());
}

// The title counts the lives; the ant just goes back to the start with its load
void AntMazeModifier::loseChance(Runtime *runtime) {
	sendMessage(runtime, "GameOver");
	_ant = _antStart;
	_munchTicks = 0;
	_antCel = (_carried > 0) ? kAntMazeCelRight + kAntMazeCarryOffset : kAntMazeCelRight;
	placeAnt(runtime);
}

// Steps back over a route character
void AntMazeModifier::undoEnemyStep(Enemy &enemy, char ch, bool &hit) {
	switch (ch) {
	case 'D':
	case 'd':
		enemy.pos.y--;
		enemy.cel = kAntMazeCelUp;
		break;
	case 'L':
	case 'l':
		enemy.pos.x++;
		enemy.cel = kAntMazeCelRight;
		break;
	case 'R':
	case 'r':
		enemy.pos.x--;
		enemy.cel = kAntMazeCelLeft;
		break;
	case 'U':
	case 'u':
		enemy.pos.y++;
		enemy.cel = kAntMazeCelDown;
		break;
	default:
		break;
	}

	if (const Common::Point *dest = findWarp(enemy.pos)) {
		if (enemy.pos == _ant)
			hit = true;
		enemy.pos = *dest;
	}
}

// Uppercase route steps are trusted; lowercase ones warp and bounce off walls
// and uneaten munch tunnels, replaying the route backwards to its loop point
void AntMazeModifier::stepEnemy(Runtime *runtime, int index, bool &hit) {
	Enemy &enemy = _enemies[index];
	auto pathChar = [&enemy](int idx) -> char {
		return (idx >= 0 && idx < static_cast<int>(enemy.path.size())) ? enemy.path[idx] : '\0';
	};

	char ch = pathChar(enemy.idx);
	if (ch == '!') {
		enemy.idx = enemy.loop;
		ch = pathChar(enemy.idx);
	}

	if (!enemy.reversing) {
		bool checked = false;
		switch (ch) {
		case 'd':
			checked = true;
			// fall through
		case 'D':
			enemy.pos.y++;
			enemy.cel = kAntMazeCelDown;
			break;
		case 'l':
			checked = true;
			// fall through
		case 'L':
			enemy.pos.x--;
			enemy.cel = kAntMazeCelLeft;
			break;
		case 'r':
			checked = true;
			// fall through
		case 'R':
			enemy.pos.x++;
			enemy.cel = kAntMazeCelRight;
			break;
		case 'u':
			checked = true;
			// fall through
		case 'U':
			enemy.pos.y--;
			enemy.cel = kAntMazeCelUp;
			break;
		default:
			break;
		}

		if (checked) {
			if (enemy.pos == _ant)
				hit = true;
			if (const Common::Point *dest = findWarp(enemy.pos))
				enemy.pos = *dest;
			for (int i = 0; i < _munchCount; i++) {
				if (_munch[i] == enemy.pos)
					enemy.reversing = true;
			}
			if (mapAt(enemy.pos.x, enemy.pos.y) == '*')
				enemy.reversing = true;
		}

		if (enemy.reversing)
			undoEnemyStep(enemy, ch, hit);
	} else
		undoEnemyStep(enemy, ch, hit);

	if (enemy.pos.x >= 0 && enemy.pos.x < kCols && enemy.pos.y >= 0 && enemy.pos.y < kRows) {
		Common::Point center = tileCenter(enemy.pos.x, enemy.pos.y);
		placeCentered(runtime, child(6 + index), center.x, center.y);
		setCel(runtime, child(6 + index), enemy.cel);
		if (enemy.pos == _ant)
			hit = true;
	}

	if (enemy.reversing) {
		enemy.idx--;
		if (enemy.idx > enemy.loop)
			return;
		enemy.reversing = false;
	}
	enemy.idx++;
}

void AntMazeModifier::tick(Runtime *runtime) {
	_scheduledEvent.reset();
	if (!_running)
		return;

	scheduleTick(runtime);

	if (_paused || _gameLevel <= 0 || !_levelReady)
		return;

	_tickCount++;

	if (_munchTicks > 0) {
		_antCel++;
		if (--_munchTicks == 0)
			_antCel = (_carried > 0) ? kAntMazeCelRight + kAntMazeCarryOffset : kAntMazeCelRight;
		setCel(runtime, child(2), _antCel);
	}

	bool hit = false;
	for (int i = 0; i < _enemyCount; i++) {
		const Enemy &enemy = _enemies[i];

		// Comes out of the nest
		if (_tickCount == enemy.startTick) {
			Common::Point center = tileCenter(enemy.pos.x, enemy.pos.y);
			placeCentered(runtime, child(6 + i), center.x, center.y);
			continue;
		}

		if (_tickCount < enemy.startTick || _tickCount % 9 != 0)
			continue;

		stepEnemy(runtime, i, hit);
	}

	if (hit)
		loseChance(runtime);
}

Common::SharedPtr<Modifier> AntMazeModifier::shallowClone() const {
	return Common::SharedPtr<Modifier>(new AntMazeModifier(*this));
}

const char *AntMazeModifier::getDefaultName() const {
	return "AntMaze";
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

// Item number (the container's index under the background) to the last cel of
// its idle animation, and the offset from the click point it is placed at
static const struct ArtItem {
	const char *countAttrib;
	int32 maxCel;
	int16 dx;
	int16 dy;
} kArtItems[] = {
	{"ibamboo", 24, 20, -5},
	{"iberrybush", 26, 25, 0},
	{"icrocus", 35, 20, 5},
	{"iginger", 22, 10, 0},
	{"iindpaintbrush", 39, -5, -15},
	{"iiris", 37, -10, -15},
	{"ilupine", 29, 30, 0},
	{"imarigold", 21, 10, -5},
	{"ipampasgrass", 31, -15, 0},
	{"itulip", 29, 30, 7},
	{"ibirch", 27, -15, -20},
	{"icatalpa", 40, 0, 0},
	{"imagnolia", 25, 0, 0},
	{"imaple", 50, 15, 0},
	{"ipine", 27, 15, 0},
	{"igeneralstore", 28, 15, 5},
	{"igreenhouse", 56, 0, 0},
	{"ilogcabin", 39, 0, -12},
	{"iyellowhouse", 55, -2, -15},
	{"iouthouse", 64, -5, 0},
};

static const int32 kArtOutHouse = 21;
static const int32 kArtGrowCels = 8;
static const int32 kArtGrowTicks = 22;
static const int32 kArtIdlePause = 150;
static const int32 kArtMaxGrassCel = 14;

ArtModifier::Slot::Slot()
	: grown(false), growing(false), growCel(1), animCel(9), growCountdown(1), animCountdown(1), idleDelay(0), maxCel(0), itemType(0) {
}

ArtModifier::ArtModifier()
	: _running(false), _objItem(0), _objType(0), _grassThreshold(4), _objCount(0), _grassCel(1), _erupting(false), _clickValid(false),
	  _objCloned(false), _outhouseGate(true), _segStart(1), _segEnd(1), _restoreFlag(false), _grassCelAttr(1) {
	for (int32 &count : _counts)
		count = 0;
	_slots.resize(kNumSlots);
}

ArtModifier::~ArtModifier() {
	stop();
}

bool ArtModifier::load(const PlugInModifierLoaderContext &context, const Data::KW::GameModifier &data) {
	return loadDlgMgrEvent(_startWhen, data.enableWhen) && loadDlgMgrEvent(_stopWhen, data.disableWhen);
}

bool ArtModifier::respondsToEvent(const Event &evt) const {
	return _startWhen.respondsTo(evt) || _stopWhen.respondsTo(evt) || evt.eventType == EventIDs::kParentDisabled || evt.eventType == EventIDs::kMouseUp;
}

VThreadState ArtModifier::consumeMessage(Runtime *runtime, const Common::SharedPtr<MessageProperties> &msg) {
	const Event &evt = msg->getEvent();

	if (_startWhen.respondsTo(evt)) {
		reset(runtime);
		if (!_running) {
			_running = true;
			_scheduledEvent = runtime->getScheduler().scheduleMethod<ArtModifier, &ArtModifier::tick>(runtime->getPlayTime() + 83, this);
		}
	} else if (_stopWhen.respondsTo(evt) || evt.eventType == EventIDs::kParentDisabled) {
		stop();
	} else if (evt.eventType == EventIDs::kMouseUp) {
		// Clicks on the background mark where the next item goes
		if (_clickValid)
			_mousePt = runtime->getCachedMousePosition();
	}

	return kVThreadReturn;
}

void ArtModifier::disable(Runtime *runtime) {
	stop();
}

void ArtModifier::stop() {
	_running = false;
	if (_scheduledEvent) {
		_scheduledEvent->cancel();
		_scheduledEvent.reset();
	}
}

void ArtModifier::reset(Runtime *runtime) {
	_objItem = 0;
	_objType = 0;
	_objCount = 0;
	_grassThreshold = 4;
	_grassCel = 1;
	_erupting = false;
	_clickValid = false;
	_outhouseGate = true;
	_segStart = 1;
	_segEnd = 1;
	_restoreFlag = false;
	for (int32 &count : _counts)
		count = 0;
	_grassCelAttr = 1;

	for (Slot &slot : _slots) {
		slot = Slot();
		slot.idleDelay = runtime->getRandom()->getRandomNumberRng(100, 130);
	}
}

Structural *ArtModifier::child(Structural *parent, int index) const {
	if (!parent || index < 1 || index > static_cast<int>(parent->getChildren().size()))
		return nullptr;
	return parent->getChildren()[index - 1].get();
}

void ArtModifier::setCel(Runtime *runtime, const Common::WeakPtr<RuntimeObject> &element, int32 cel) {
	Common::SharedPtr<RuntimeObject> obj = element.lock();
	if (obj)
		dlgMgrSetInt(runtime, this, obj.get(), "cel", cel);
}

void ArtModifier::tick(Runtime *runtime) {
	_scheduledEvent = runtime->getScheduler().scheduleMethod<ArtModifier, &ArtModifier::tick>(runtime->getPlayTime() + 83, this);

	if (_mousePt.x > 15 && _mousePt.x < 620 && _mousePt.y > 15 && _mousePt.y < 422 && _objCloned && _clickValid) {
		placeItem(runtime);
		_objCloned = false;
	}

	if (_restoreFlag) {
		restoreItems(runtime);
		_grassCel = _grassCelAttr;
		_grassThreshold += _objCount;
	}

	// Items grow through cels 1 to 8, even during an eruption
	for (Slot &slot : _slots) {
		if (!slot.growing)
			continue;

		slot.growCountdown--;
		if (slot.growCel < kArtGrowCels && slot.growCountdown == 0) {
			slot.growCel++;
			setCel(runtime, slot.element, slot.growCel);
			slot.growCountdown = kArtGrowTicks;
			if (slot.growCel == kArtGrowCels) {
				slot.growing = false;
				slot.grown = true;
			}
		}
	}

	// Grown items play their idle animation every so often
	if (!_erupting) {
		for (Slot &slot : _slots) {
			if (!slot.grown || slot.element.expired())
				continue;

			if (slot.idleDelay != 0) {
				slot.idleDelay--;
				continue;
			}

			slot.animCountdown--;
			if (slot.animCel >= slot.maxCel || slot.animCountdown != 0)
				continue;

			if (slot.itemType == kArtOutHouse) {
				// The outhouse plays one of four segments
				if (_outhouseGate) {
					static const int32 kSegments[4][2] = {{9, 22}, {22, 37}, {37, 50}, {50, 64}};
					_outhouseGate = false;
					uint segment = runtime->getRandom()->getRandomNumber(1) * 2 + runtime->getRandom()->getRandomNumber(1);
					_segStart = kSegments[segment][0];
					_segEnd = kSegments[segment][1];
					slot.animCel = _segStart;
				}

				slot.animCel++;
				setCel(runtime, slot.element, slot.animCel);
				slot.animCountdown = 2;
				if (slot.animCel == _segEnd) {
					slot.idleDelay = kArtIdlePause;
					slot.animCel = 9;
					_outhouseGate = true;
				}
			} else {
				slot.animCel++;
				setCel(runtime, slot.element, slot.animCel);
				slot.animCountdown = 1;
				if (slot.animCel == slot.maxCel) {
					slot.idleDelay = kArtIdlePause;
					slot.animCel = 9;
				}
			}
		}
	}

	// The grass gets greener every 4 items
	if (_objCount == _grassThreshold && _grassCel < kArtMaxGrassCel && !_restoreFlag) {
		Structural *grass = child(child(findStructuralOwner(), 1), 1);
		if (grass)
			dlgMgrSetInt(runtime, this, grass, "cel", _grassCel);
		_grassCel++;
		_grassThreshold += 4;
	}
}

void ArtModifier::placeItem(Runtime *runtime) {
	Slot *slot = nullptr;
	for (Slot &candidate : _slots) {
		if (candidate.element.expired()) {
			slot = &candidate;
			break;
		}
	}
	if (!slot)
		return;

	slot->growing = true;
	slot->grown = false;
	slot->growCel = 1;
	slot->growCountdown = 1;

	Structural *container = child(findStructuralOwner(), _objItem);
	if (!container) {
		debug(1, "Art: Couldn't get container %i", static_cast<int>(_objItem));
		return;
	}

	// The original takes the second child, as mTropolis put each clone right after the
	// original, but clones are added at the end here, so the newest one is the last child
	int numChildren = container->getChildren().size();
	Structural *item = (numChildren >= 2) ? child(container, numChildren) : nullptr;
	if (!item) {
		debug(1, "Art: Couldn't get item %i", static_cast<int>(_objItem));
		return;
	}

	slot->element = item->getSelfReference();
	debug(2, "Art: Placing '%s' at (%i,%i)", item->getName().c_str(), _mousePt.x, _mousePt.y);

	int32 x = _mousePt.x;
	int32 y = (_mousePt.y >= 410) ? 385 : _mousePt.y;
	const ArtItem *itemDef = (_objItem >= kFirstItem && _objItem <= kLastItem) ? &kArtItems[_objItem - kFirstItem] : nullptr;
	if (itemDef) {
		x += itemDef->dx;
		y += itemDef->dy;
	}

	// Placed centered on the point
	if (item->isElement() && static_cast<Element *>(item)->isVisual()) {
		const Common::Rect &rect = static_cast<VisualElement *>(item)->getRelativeRect();
		DynamicValue pos;
		pos.setPoint(Common::Point(x - rect.width() / 2, y - rect.height() / 2));
		dlgMgrWriteAttribute(runtime, this, item, "position", pos);
	}

	_objCount++;
	slot->itemType = _objItem;
	if (itemDef)
		slot->maxCel = itemDef->maxCel;
	else
		debug(1, "Art: Couldn't get item type %i", static_cast<int>(_objItem));
}

// After a picture is loaded, every clone is taken on as a grown item
void ArtModifier::restoreItems(Runtime *runtime) {
	uint slotIndex = 0;

	for (int32 itemType = 1; itemType <= kLastItem; itemType++) {
		Structural *container = child(findStructuralOwner(), itemType);
		if (!container || !child(container, 2))
			continue;

		int32 count = _counts[itemType];
		for (int32 i = 2; i <= count + 1 && slotIndex < _slots.size(); i++) {
			Slot &slot = _slots[slotIndex++];
			Structural *item = child(container, i);
			if (!item)
				continue;

			slot.element = item->getSelfReference();
			slot.growing = false;
			slot.grown = true;
			slot.growCel = kArtGrowCels;
			slot.growCountdown = 1;
			slot.idleDelay = runtime->getRandom()->getRandomNumberRng(100, 130);
			slot.itemType = itemType;
			slot.maxCel = (itemType >= kFirstItem) ? kArtItems[itemType - kFirstItem].maxCel : 0;
			_objCount++;
		}
	}

	_restoreFlag = false;
}

// The eraser forgets the newest item; the title kills its clone
void ArtModifier::eraseLast() {
	uint firstFree = 0;
	while (firstFree < _slots.size() && !_slots[firstFree].element.expired())
		firstFree++;

	if (firstFree == 0 || firstFree == _slots.size())
		return;

	Slot &slot = _slots[firstFree - 1];
	slot = Slot();
	slot.idleDelay = kArtIdlePause;
	_objCount--;
}

MiniscriptInstructionOutcome ArtModifier::writeRefAttribute(MiniscriptThread *thread, DynamicValueWriteProxy &result, const Common::String &attrib) {
	static const struct {
		const char *name;
		WritableAttribute attrib;
	} kAttribs[] = {
		{"objecttype", kAttribObjectType},
		{"objitem", kAttribObjItem},
		{"objerase", kAttribObjErase},
		{"eruption", kAttribEruption},
		{"erupting", kAttribErupting},
		{"clickvalid", kAttribClickValid},
		{"objcloned", kAttribObjCloned},
		{"restore", kAttribRestore},
		{"grasscel", kAttribGrassCel},
		{"paused", kAttribIgnored},
		{"iobjcount", kAttribIgnored},
		{"ptrestore", kAttribIgnored},
	};

	uintptr writable = 0;
	bool found = false;
	for (const auto &entry : kAttribs) {
		if (attrib == entry.name) {
			writable = entry.attrib;
			found = true;
			break;
		}
	}

	for (int32 i = kFirstItem; !found && i <= kLastItem; i++) {
		if (attrib == kArtItems[i - kFirstItem].countAttrib) {
			writable = kAttribItemCount + i;
			found = true;
		}
	}

	if (!found)
		return Modifier::writeRefAttribute(thread, result, attrib);

	result.pod.ifc = DynamicValueWriteInterfaceGlue<WriteProxy>::getInstance();
	result.pod.objectRef = this;
	result.pod.ptrOrOffset = writable;
	return kMiniscriptInstructionOutcomeContinue;
}

MiniscriptInstructionOutcome ArtModifier::WriteProxy::write(MiniscriptThread *thread, const DynamicValue &assigned, void *objectRef, uintptr ptrOrOffset) {
	ArtModifier *self = static_cast<ArtModifier *>(objectRef);

	// Variables are passed by reference
	DynamicValue value = assigned;
	if (value.getType() == DynamicValueTypes::kObject) {
		Common::SharedPtr<RuntimeObject> obj = value.getObject().object.lock();
		if (obj && obj->isModifier() && static_cast<Modifier *>(obj.get())->isVariable())
			static_cast<VariableModifier *>(obj.get())->varGetValue(value);
	}

	if (ptrOrOffset == kAttribIgnored)
		return kMiniscriptInstructionOutcomeContinue;

	int32 n = 0;
	bool flag = false;
	if (value.getType() == DynamicValueTypes::kBoolean) {
		flag = value.getBool();
		n = flag ? 1 : 0;
	} else if (value.roundToInt(n))
		flag = (n != 0);
	else
		return kMiniscriptInstructionOutcomeFailed;

	switch (ptrOrOffset) {
	case kAttribObjectType:
		self->_objType = n;
		break;
	case kAttribObjItem:
		self->_objItem = n;
		break;
	case kAttribObjErase:
		if (flag)
			self->eraseLast();
		break;
	case kAttribEruption:
		if (flag)
			self->reset(thread->getRuntime());
		break;
	case kAttribErupting:
		self->_erupting = flag;
		break;
	case kAttribClickValid:
		self->_clickValid = flag;
		break;
	case kAttribObjCloned:
		self->_objCloned = flag;
		break;
	case kAttribRestore:
		self->_restoreFlag = flag;
		break;
	case kAttribGrassCel:
		self->_grassCelAttr = n;
		break;
	default:
		if (ptrOrOffset >= static_cast<uintptr>(kAttribItemCount + kFirstItem) && ptrOrOffset <= static_cast<uintptr>(kAttribItemCount + kLastItem))
			self->_counts[ptrOrOffset - kAttribItemCount] = n;
		break;
	}

	return kMiniscriptInstructionOutcomeContinue;
}

MiniscriptInstructionOutcome ArtModifier::WriteProxy::refAttrib(MiniscriptThread *thread, DynamicValueWriteProxy &proxy, void *objectRef, uintptr ptrOrOffset, const Common::String &attrib) {
	return kMiniscriptInstructionOutcomeFailed;
}

MiniscriptInstructionOutcome ArtModifier::WriteProxy::refAttribIndexed(MiniscriptThread *thread, DynamicValueWriteProxy &proxy, void *objectRef, uintptr ptrOrOffset, const Common::String &attrib, const DynamicValue &index) {
	return kMiniscriptInstructionOutcomeFailed;
}

Common::SharedPtr<Modifier> ArtModifier::shallowClone() const {
	Common::SharedPtr<ArtModifier> clone(new ArtModifier(*this));
	clone->_scheduledEvent.reset();
	clone->_running = false;
	return clone;
}

const char *ArtModifier::getDefaultName() const {
	return "Art";
}

// Hit boxes relative to an object's position, open intervals unless noted
static const int16 kSubNoLimit = 32767;

static const struct SubBox {
	int16 cel;	// Or the tube worm slot
	int16 x0;
	int16 x1;
	int16 y0;
	int16 y1;
	bool separate;	// Tested even if an earlier box for the cel hit
} kSubVentBoxes[3][18] = {
	{	// Tall
		{3, -11, 45, 143, kSubNoLimit, false},
		{4, -15, 51, 81, kSubNoLimit, false},
		{5, -17, 57, 105, kSubNoLimit, false},
		{5, -13, 53, 57, 106, false},
		{6, -17, 61, 99, kSubNoLimit, false},
		{6, -13, 55, 35, 100, false},
		{7, -25, 65, 45, kSubNoLimit, true},
		{7, -11, 57, -5, 46, true},
		{8, -23, 63, 75, kSubNoLimit, false},
		{8, -13, 55, -13, 76, false},
		{9, -17, 65, 103, kSubNoLimit, false},
		{9, -9, 59, 67, 104, false},
	},
	{	// Short
		{3, -6, 48, 103, kSubNoLimit, false},
		{4, -14, 52, 65, kSubNoLimit, false},
		{5, -16, 56, 87, kSubNoLimit, false},
		{5, -12, 50, 55, 88, false},
		{6, -14, 60, 81, kSubNoLimit, false},
		{6, -12, 54, 31, 82, false},
		{7, -20, 64, 39, kSubNoLimit, true},
		{7, -8, 56, -7, 40, true},
		{8, -20, 60, 41, kSubNoLimit, false},
		{8, -16, 54, 1, 42, false},
		{9, -18, 60, 89, kSubNoLimit, false},
		{9, -8, 54, 69, 90, false},
	},
	{	// Fat
		{3, 17, 83, 79, kSubNoLimit, false},
		{4, 7, 93, 67, kSubNoLimit, false},
		{4, 17, 79, 41, 68, false},
		{5, 3, 97, 55, kSubNoLimit, false},
		{5, 13, 85, 33, 56, false},
		{6, -9, 103, 59, kSubNoLimit, false},
		{6, 3, 89, 33, 60, false},
		{6, 21, 79, 9, 34, false},
		{7, -7, 105, 83, kSubNoLimit, false},
		{7, -21, 115, 43, 84, false},
		{7, -5, 95, 13, 44, false},
		{7, 13, 81, -7, 14, false},
		{8, -15, 109, 75, kSubNoLimit, false},
		{8, 1, 89, 37, 76, false},
		{8, 17, 79, 7, 38, false},
		{9, -3, 95, 75, kSubNoLimit, false},
		{9, 9, 81, 47, 76, false},
		{9, 19, 75, 27, 48, false},
	},
}, kSubSmokerBoxes[] = {
	{3, 23, 81, 83, 123, false},
	{3, 11, 91, 41, 84, false},
	{4, 23, 81, 69, 123, false},
	{4, 11, 91, 0, 70, false},
	{5, 21, 81, 87, 123, false},
	{5, -9, 120, 6, 88, false},
	{6, -15, 132, 6, 93, false},
	{7, -15, 132, 54, 90, false},
	{7, 6, 135, -9, 55, false},
	{8, -12, 78, 39, 96, false},
	{8, 12, 129, -6, 82, false},
	{9, -6, 120, 45, 75, false},
	{9, 18, 120, -3, 46, false},
	{10, 9, 117, 39, 63, false},
	{10, 18, 90, 0, 40, false},
}, kSubTubeWormBoxes[] = {
	{1, -22, 44, 78, 111, false},
	{1, -22, 68, 21, 63, false},
	{1, 12, 116, -5, 49, false},
	{1, 64, 127, 23, 65, false},
	{1, 79, 146, 56, 89, false},
	{2, -22, 41, 22, 64, false},
	{2, -4, 83, -10, 37, false},
	{2, 49, 106, 8, 53, false},
	{2, 70, 138, 23, 59, false},
	{3, -22, 76, 4, 47, false},
	{3, 5, 62, -11, 39, false},
	{3, 46, 101, 17, 61, false},
	{3, 56, 121, 50, 77, false},
	{4, -21, 48, 45, 81, false},
	{4, -16, 39, 3, 45, false},
	{4, 9, 66, -8, 39, false},
	{4, 47, 132, 8, 55, false},
}, kSubGemBoxes[] = {
	{13, -36, 35, -22, 20, false},	// Cels 1-13
	{14, -31, 32, -19, 20, false},
	{15, -32, 32, -19, 17, false},
	{16, -31, 29, -19, 14, false},
	{17, -31, 29, -16, 14, false},
	{18, -28, 29, -14, 13, false},
	{19, -25, 26, -13, 11, false},
	{20, -25, 23, -10, 8, false},
}, kSubBubbleBoxes[] = {	// Closed intervals
	{2, -14, 41, 35, 60, false},
	{3, -16, 49, 12, 59, false},
	{4, -15, 43, -8, 59, false},
	{5, -19, 46, -7, 56, false},
	{6, -18, 44, -3, 44, false},
	{7, -15, 41, -5, 29, false},
	{8, -12, 39, 7, 29, false},
};

static bool subInBox(const SubBox &box, int32 dx, int32 dy) {
	return dx > box.x0 && dx < box.x1 && dy > box.y0 && dy < box.y1;
}

// Children of the owner element
enum SubChild {
	kSubChildSub = 1,
	kSubChildLevels,
	kSubChildSmallRocks,	// Then medium and large
	kSubChildSplash = 6,
	kSubChildTallVents,	// Then short and fat
	kSubChildSmokers = 10,
	kSubChildSquid,
	kSubChildTubeWorms,
	kSubChildStarFish,
	kSubChildGems,
	kSubChildBubbles,
	kSubChildLine,
};

static const int32 kSubCelLeft = 1;
static const int32 kSubCelRight = 10;
static const int32 kSubCelShieldLeft = 9;
static const int32 kSubCelShieldRight = 18;
static const int32 kSubRestingRockCel = 11;
static const int32 kSubGemGoneCel = 21;
static const int32 kSubHidden = 600;

SubModifier::SubModifier()
	: _levelsLoaded(false), _running(false), _paused(true), _initialized(false), _level(1), _points(0), _timeCounter(0), _timeLimit(950),
	  _rockSpawnCountdown(0), _rockSpawnIndex(0), _rockAnimInterval(3), _splashY(40), _state(kStateFree), _subX(0), _subY(0), _posX(0.0),
	  _posY(0.0), _vx(0.0), _vy(0.0), _subCel(kSubCelRight), _spinTargetX(0), _spinLift(0), _stateCounter(0), _shieldTimer(0), _shieldBlinks(5),
	  _mouseHeld(false), _mouseMode(true), _keyUp(false), _keyDown(false), _keyLeft(false), _keyRight(false), _squidReenterX(0), _lineCel(1) {
	memset(_rocks, 0, sizeof(_rocks));
	memset(_splashes, 0, sizeof(_splashes));
	memset(_vents, 0, sizeof(_vents));
	memset(_smokers, 0, sizeof(_smokers));
	memset(&_squid, 0, sizeof(_squid));
	memset(_tubeWormHolding, 0, sizeof(_tubeWormHolding));
	memset(_gems, 0, sizeof(_gems));
	memset(&_bubbles, 0, sizeof(_bubbles));
}

SubModifier::~SubModifier() {
	stopGame();
}

bool SubModifier::load(const PlugInModifierLoaderContext &context, const Data::KW::GameModifier &data) {
	return loadDlgMgrEvent(_startWhen, data.enableWhen) && loadDlgMgrEvent(_stopWhen, data.disableWhen);
}

// The levels are records in the plug-in's data section. The fields are 32-bit
// but only the low 16 bits are used.
bool SubModifier::loadLevels() {
	if (_levelsLoaded)
		return !_levels.empty();

	_levelsLoaded = true;

	Common::File f;
	if (!f.open(Common::Path("mTrop_Resources/sub.r32")) && !f.open(Common::Path("sub.r32"))) {
		warning("Sub: sub.r32 from the installed game is needed for the levels");
		return false;
	}

	Common::Array<byte> image;
	image.resize(f.size());
	f.read(&image[0], image.size());

	bool failed = false;
	auto readS16 = [&](uint32 va) -> int16 {
		uint32 offset = va - 0x10000000;
		if (offset + 2 > image.size()) {
			failed = true;
			return 0;
		}
		return static_cast<int16>(READ_LE_UINT16(&image[offset]));
	};
	auto readPoint = [&](uint32 va) -> Common::Point {
		return Common::Point(readS16(va), readS16(va + 4));
	};
	auto readHazard = [&](uint32 va) -> Hazard {
		Hazard hazard;
		hazard.x = readS16(va);
		hazard.y = readS16(va + 4);
		hazard.warn = readS16(va + 8);
		hazard.idle = readS16(va + 12);
		return hazard;
	};

	Common::Array<Level> levels;
	levels.resize(kNumLevels + 1);
	for (uint l = 1; l <= kNumLevels; l++) {
		Level &level = levels[l];
		uint32 base = 0x10011b2c + l * 0x5ec;

		level.start = readPoint(base);
		for (uint i = 0; i < 30; i++)
			level.rockSpawn[i] = readPoint(base + 0x8 + i * 8);
		level.initRocks = readS16(base + 0xf8);
		level.rockInterval = readS16(base + 0xfc);
		level.fallSpeed = readS16(base + 0x100);

		int16 polyCount = readS16(base + 0x104);
		if (polyCount < 0 || polyCount > 100)
			failed = true;
		for (int16 i = 0; i < polyCount && !failed; i++)
			level.landscape.push_back(readPoint(base + 0x106 + i * 8));

		for (uint type = 0; type < 3; type++) {
			for (uint i = 0; i < 4; i++)
				level.vents[type][i] = readHazard(base + 0x426 + type * 0x40 + i * 16);
		}
		for (uint i = 0; i < 8; i++)
			level.smokers[i] = readHazard(base + 0x4e6 + i * 16);
		level.squid = readHazard(base + 0x566);
		level.squidSpeed = readS16(base + 0x576);
		for (uint i = 0; i < 4; i++)
			level.tubeWorms[i] = readPoint(base + 0x578 + i * 8);
		for (uint i = 0; i < 3; i++)
			level.starFish[i] = readPoint(base + 0x598 + i * 8);
		for (uint i = 0; i < 3; i++) {
			level.gems[i].pos = readPoint(base + 0x5b0 + i * 16);
			level.gems[i].surfaceHold = readS16(base + 0x5b8 + i * 16);
			level.gems[i].respawn = readS16(base + 0x5bc + i * 16);
		}
		level.bubbles = readPoint(base + 0x5e0);
		level.bubblesPeriod = readS16(base + 0x5e8);
	}

	// Level 1 starts at (50, 375) with an 81 point landscape
	if (failed || levels[1].start != Common::Point(50, 375) || levels[1].landscape.size() != 81 || levels[1].fallSpeed != 5) {
		warning("Sub: sub.r32 isn't the expected version");
		return false;
	}

	_levels.swap(levels);
	return true;
}

bool SubModifier::respondsToEvent(const Event &evt) const {
	return _startWhen.respondsTo(evt) || _stopWhen.respondsTo(evt) || evt.eventType == EventIDs::kParentDisabled
		|| evt.eventType == EventIDs::kMouseDown || evt.eventType == EventIDs::kMouseUp;
}

VThreadState SubModifier::consumeMessage(Runtime *runtime, const Common::SharedPtr<MessageProperties> &msg) {
	const Event &evt = msg->getEvent();

	if (_startWhen.respondsTo(evt)) {
		setupLevel(runtime, 1);
		_paused = true;

		if (!_running) {
			_running = true;
			_keySignaller = runtime->getProject()->notifyOnKeyboardEvent(this);
			_scheduledEvent = runtime->getScheduler().scheduleMethod<SubModifier, &SubModifier::tick>(runtime->getPlayTime() + 100, this);
		}
		return kVThreadReturn;
	}

	if (_stopWhen.respondsTo(evt) || evt.eventType == EventIDs::kParentDisabled) {
		stopGame();
		return kVThreadReturn;
	}

	// A click switches to mouse steering, but only the title unpauses
	if (evt.eventType == EventIDs::kMouseDown) {
		if (!_paused) {
			_mouseHeld = true;
			_mouseMode = true;
		}
	} else if (evt.eventType == EventIDs::kMouseUp) {
		_mouseHeld = false;
	}

	return kVThreadReturn;
}

void SubModifier::onKeyboardEvent(Runtime *runtime, const KeyboardInputEvent &keyEvt) {
	bool *flag = nullptr;
	switch (keyEvt.getKeyState().keycode) {
	case Common::KEYCODE_UP:
	case Common::KEYCODE_KP8:
		flag = &_keyUp;
		break;
	case Common::KEYCODE_DOWN:
	case Common::KEYCODE_KP2:
		flag = &_keyDown;
		break;
	case Common::KEYCODE_LEFT:
	case Common::KEYCODE_KP4:
		flag = &_keyLeft;
		break;
	case Common::KEYCODE_RIGHT:
	case Common::KEYCODE_KP6:
		flag = &_keyRight;
		break;
	default:
		return;
	}

	if (keyEvt.getKeyEventType() == Common::EVENT_KEYUP) {
		*flag = false;
		return;
	}

	// The flag is set even while paused, but only switches to the keys while playing
	if (!_paused)
		_mouseMode = false;
	*flag = true;
}

void SubModifier::disable(Runtime *runtime) {
	stopGame();
}

void SubModifier::stopGame() {
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

bool SubModifier::readAttribute(MiniscriptThread *thread, DynamicValue &result, const Common::String &attrib) {
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

MiniscriptInstructionOutcome SubModifier::writeRefAttribute(MiniscriptThread *thread, DynamicValueWriteProxy &result, const Common::String &attrib) {
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

MiniscriptInstructionOutcome SubModifier::WriteProxy::write(MiniscriptThread *thread, const DynamicValue &assigned, void *objectRef, uintptr ptrOrOffset) {
	SubModifier *self = static_cast<SubModifier *>(objectRef);

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
		if (level >= 1 && level <= kNumLevels)
			self->setupLevel(thread->getRuntime(), level);
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

MiniscriptInstructionOutcome SubModifier::WriteProxy::refAttrib(MiniscriptThread *thread, DynamicValueWriteProxy &proxy, void *objectRef, uintptr ptrOrOffset, const Common::String &attrib) {
	return kMiniscriptInstructionOutcomeFailed;
}

MiniscriptInstructionOutcome SubModifier::WriteProxy::refAttribIndexed(MiniscriptThread *thread, DynamicValueWriteProxy &proxy, void *objectRef, uintptr ptrOrOffset, const Common::String &attrib, const DynamicValue &index) {
	return kMiniscriptInstructionOutcomeFailed;
}

Structural *SubModifier::child(int index) const {
	Structural *owner = findStructuralOwner();
	if (!owner || index < 1 || index > static_cast<int>(owner->getChildren().size()))
		return nullptr;
	return owner->getChildren()[index - 1].get();
}

Structural *SubModifier::grandchild(int index, int subIndex) const {
	Structural *container = child(index);
	if (!container || subIndex < 1 || subIndex > static_cast<int>(container->getChildren().size()))
		return nullptr;
	return container->getChildren()[subIndex - 1].get();
}

void SubModifier::placeCentered(Runtime *runtime, Structural *element, int32 x, int32 y) {
	if (!element || !element->isElement() || !static_cast<Element *>(element)->isVisual())
		return;

	const Common::Rect &rect = static_cast<VisualElement *>(element)->getRelativeRect();
	placeTopLeft(runtime, element, x - rect.width() / 2, y - rect.height() / 2);
}

void SubModifier::placeTopLeft(Runtime *runtime, Structural *element, int32 x, int32 y) {
	if (!element)
		return;

	DynamicValue pos;
	pos.setPoint(Common::Point(x, y));
	dlgMgrWriteAttribute(runtime, this, element, "position", pos);
}

void SubModifier::setCel(Runtime *runtime, Structural *element, int32 cel) {
	dlgMgrSetInt(runtime, this, element, "cel", cel);
}

void SubModifier::sendMessage(Runtime *runtime, const char *messageName) {
	uint32 messageID = 0;
	if (!runtime->getProject()->findAuthorMessageID(messageName, messageID))
		return;

	Structural *owner = findStructuralOwner();
	if (!owner)
		return;

	debug(2, "Sub: Sending '%s'", messageName);
	Common::SharedPtr<MessageProperties> props(new MessageProperties(Event(EventIDs::kAuthorMessage, messageID), DynamicValue(), getSelfReference()));
	Common::SharedPtr<MessageDispatch> dispatch(new MessageDispatch(props, owner, true, true, false));
	runtime->queueMessage(dispatch);
}

// Even-odd test, as the original's polygon region
bool SubModifier::inLandscape(int32 x, int32 y) const {
	const Common::Array<Common::Point> &poly = _levels[_level].landscape;
	bool inside = false;
	for (uint i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
		const Common::Point &a = poly[i];
		const Common::Point &b = poly[j];
		if ((a.y > y) != (b.y > y)) {
			double crossX = a.x + static_cast<double>(y - a.y) * (b.x - a.x) / (b.y - a.y);
			if (x < crossX)
				inside = !inside;
		}
	}
	return inside;
}

void SubModifier::setupLevel(Runtime *runtime, int32 levelNum) {
	if (!loadLevels() || levelNum < 1 || levelNum > kNumLevels)
		return;

	const Level &level = _levels[levelNum];
	_level = levelNum;
	_points = 0;

	// The second level of a pair continues the first one's clock
	if (levelNum % 2 == 1)
		_timeCounter = 0;

	int tier = (levelNum <= 10) ? 0 : (levelNum <= 20) ? 1 : 2;
	static const int32 kTimeLimits[3] = {950, 850, 800};
	static const int32 kRockAnimIntervals[3] = {3, 5, 7};
	static const int32 kShieldBlinks[3] = {5, 4, 3};
	_timeLimit = kTimeLimits[tier];
	_rockAnimInterval = kRockAnimIntervals[tier];
	_shieldBlinks = kShieldBlinks[tier];
	if (level.fallSpeed == 5)
		_splashY = 40;
	else if (level.fallSpeed == 6)
		_splashY = 36;
	else if (level.fallSpeed == 7)
		_splashY = 42;

	_keyUp = _keyDown = _keyLeft = _keyRight = false;

	if (!findStructuralOwner())
		return;

	_vx = _vy = 0.0;
	_subCel = kSubCelRight;
	_posX = level.start.x;
	_posY = level.start.y;
	_subX = level.start.x;
	_subY = level.start.y;
	_state = kStateFree;
	placeCentered(runtime, child(kSubChildSub), _subX, _subY);
	setCel(runtime, child(kSubChildSub), _subCel);

	for (int i = 1; i <= kNumLevels; i++)
		placeTopLeft(runtime, grandchild(kSubChildLevels, i), 0, (i == levelNum) ? 0 : 650);

	_rockSpawnCountdown = level.rockInterval;
	_rockSpawnIndex = 0;

	// The first few rocks of each size start as samples lying on the floor
	for (int c = 0; c < 3; c++) {
		for (int i = 0; i < 8; i++) {
			Rock &rock = _rocks[c][i];
			rock.animTimer = _rockAnimInterval;
			if (i < level.initRocks) {
				rock.x = level.rockSpawn[_rockSpawnIndex].x;
				rock.y = rock.targetY = level.rockSpawn[_rockSpawnIndex].y;
				rock.cel = kSubRestingRockCel;
				rock.state = 3;
				_rockSpawnIndex++;
			} else {
				rock.x = 0;
				rock.y = kSubHidden;
				rock.cel = 1;
				rock.state = 4;
			}
			Structural *element = grandchild(kSubChildSmallRocks + c, i + 1);
			placeCentered(runtime, element, rock.x, rock.y);
			setCel(runtime, element, rock.cel);
		}
	}

	for (int i = 0; i < 4; i++) {
		_splashes[i].x = 0;
		_splashes[i].y = kSubHidden;
		_splashes[i].cel = 1;
		_splashes[i].active = false;
		placeTopLeft(runtime, grandchild(kSubChildSplash, i + 1), 0, kSubHidden);
		setCel(runtime, grandchild(kSubChildSplash, i + 1), 1);
	}

	for (int type = 0; type < 3; type++) {
		for (int i = 0; i < 4; i++) {
			Vent &vent = _vents[type][i];
			vent.x = level.vents[type][i].x;
			vent.y = level.vents[type][i].y;
			vent.timer = level.vents[type][i].idle;
			vent.state = 0;
			vent.cel = 1;
			placeTopLeft(runtime, grandchild(kSubChildTallVents + type, i + 1), vent.x, vent.y);
			setCel(runtime, grandchild(kSubChildTallVents + type, i + 1), 1);
		}
	}

	for (int i = 0; i < 8; i++) {
		Vent &smoker = _smokers[i];
		smoker.x = level.smokers[i].x;
		smoker.y = level.smokers[i].y;
		smoker.timer = level.smokers[i].idle;
		smoker.state = 0;
		smoker.cel = 1;
		placeTopLeft(runtime, grandchild(kSubChildSmokers, i + 1), smoker.x, smoker.y);
		setCel(runtime, grandchild(kSubChildSmokers, i + 1), 1);
	}

	resetSquid(runtime);

	for (int i = 0; i < 4; i++) {
		_tubeWorms[i] = level.tubeWorms[i];
		_tubeWormHolding[i] = false;
		placeTopLeft(runtime, grandchild(kSubChildTubeWorms, i + 1), _tubeWorms[i].x, _tubeWorms[i].y);
	}

	for (int i = 0; i < 3; i++) {
		_starFish[i] = level.starFish[i];
		placeTopLeft(runtime, grandchild(kSubChildStarFish, i + 1), _starFish[i].x, _starFish[i].y);
	}

	for (int i = 0; i < 3; i++) {
		Gem &gem = _gems[i];
		gem.x = 0;
		gem.y = kSubHidden;
		gem.timerA = level.gems[i].respawn;
		gem.timerB = 0;
		gem.cel = 1;
		placeTopLeft(runtime, grandchild(kSubChildGems, i + 1), 0, kSubHidden);
		setCel(runtime, grandchild(kSubChildGems, i + 1), 1);
	}

	_bubbles.x = level.bubbles.x;
	_bubbles.y = level.bubbles.y;
	_bubbles.timer = level.bubblesPeriod;
	_bubbles.cel = 1;
	placeTopLeft(runtime, child(kSubChildBubbles), _bubbles.x, _bubbles.y);
	setCel(runtime, child(kSubChildBubbles), 1);

	_lineCel = 1;
	placeTopLeft(runtime, child(kSubChildLine), 0, kSubHidden);
	setCel(runtime, child(kSubChildLine), 1);

	_initialized = true;
}

void SubModifier::resetSquid(Runtime *runtime) {
	const Hazard &squid = _levels[_level].squid;
	_squid.x = squid.x;
	_squid.y = squid.y;
	_squid.cel = 1;
	_squid.timer = squid.idle;
	_squid.state = 0;
	placeTopLeft(runtime, child(kSubChildSquid), _squid.x, _squid.y);
	setCel(runtime, child(kSubChildSquid), 1);
}

void SubModifier::tick(Runtime *runtime) {
	_scheduledEvent.reset();
	if (!_running)
		return;

	_scheduledEvent = runtime->getScheduler().scheduleMethod<SubModifier, &SubModifier::tick>(runtime->getPlayTime() + 100, this);

	if (_paused || !_initialized)
		return;

	// The clock stops while the sub is lifted to the bus
	if (_state != kStateLift) {
		_timeCounter++;
		if (_timeCounter % 10 == 0)
			sendMessage(runtime, "Countdown");
	}
	if (_timeCounter >= _timeLimit)
		sendMessage(runtime, "OutOfTime");

	bool right = false;
	bool left = false;
	steer(runtime, right, left);

	int32 nx = static_cast<int32>(_posX);
	int32 ny = static_cast<int32>(_posY);
	bool atLeftWall = (_posX < 36.0);
	bool belowSurface = (_posY >= 56.0);
	bool probeL = inLandscape(nx - 24, ny + 15);
	bool probeR = inLandscape(nx + 24, ny + 15);
	bool probeC = inLandscape(nx, ny + 15);

	if (_posX > 602.0) {
		resetSquid(runtime);
		if (_level % 2 == 0) {
			_state = kStateLift;
			sendMessage(runtime, "PlaySFX9");
			sendMessage(runtime, "Congrats");
		} else {
			_paused = true;
			sendMessage(runtime, "PlaySFX10");
			sendMessage(runtime, "Advance");
		}
	}

	if (_state <= kStateBounce) {
		if (atLeftWall || probeC)
			_state = kStateBlocked;
		else if (belowSurface)
			_state = (probeL || probeR) ? kStateBounce : kStateFree;
		else
			_state = kStateSurface;
	}

	tickSubState(runtime, right, left, probeL, probeR, probeC, atLeftWall, belowSurface);

	_subX = static_cast<int32>(_posX);
	_subY = static_cast<int32>(_posY);
	placeCentered(runtime, child(kSubChildSub), _subX, _subY);
	if (right)
		_subCel = kSubCelRight;
	else if (left)
		_subCel = kSubCelLeft;
	setCel(runtime, child(kSubChildSub), _subCel);

	spawnRock(runtime);
	tickRocks(runtime);
	if (_state != kStateSquid)
		tickGems(runtime);
	tickSplashes(runtime);
	tickVents(runtime);
	tickTubeWorms(runtime);
	tickSmokers(runtime);
	tickStarFish(runtime);
	tickSquid(runtime);
	tickBubbles(runtime);
}

// Accelerates toward the mouse while the button is held, or with the arrow keys
void SubModifier::steer(Runtime *runtime, bool &right, bool &left) {
	bool up = false;
	bool down = false;

	if (_mouseMode) {
		const Common::Point &mouse = runtime->getCachedMousePosition();
		double dx = mouse.x - _subX;
		double dy = mouse.y - _subY;

		// Divisions by zero count as steep, as in the original's floating point
		if (dx > 0.0)
			right = (dy == 0.0 || dx / fabs(dy) > 0.6);
		else
			left = (dx < 0.0 && (dy == 0.0 || -dx / fabs(dy) > 0.6));
		bool steep = (dy != 0.0 && (dx == 0.0 || fabs(dy / dx) > 0.6));
		if (dy < 0.0 && steep)
			up = true;
		else if (dy > 0.0 && steep)
			down = true;
	}

	if (_keyRight)
		right = true;
	else if (_keyLeft)
		left = true;
	if (_keyUp)
		up = true;
	else if (_keyDown)
		down = true;

	// Reversing stops dead, then speeds up by 1 a tick
	if (_mouseHeld || !_mouseMode) {
		if (right)
			_vx = (_vx < 0.0) ? 0.0 : MIN(_vx + 1.0, 7.0);
		else if (left)
			_vx = (_vx > 0.0) ? 0.0 : MAX(_vx - 1.0, -7.0);
		if (up)
			_vy = (_vy > 0.0) ? 0.0 : MAX(_vy - 1.0, -6.0);
		else if (down)
			_vy = (_vy < 0.0) ? 0.0 : MIN(_vy + 1.0, 6.0);
	}

	// Only fast horizontal motion slows down, so the sub keeps coasting
	if (_vx > 2.5)
		_vx -= 0.4;
	else if (_vx < -2.5)
		_vx += 0.4;

	_posX += _vx;
	_posY += _vy;
}

void SubModifier::bounce(bool probeL, bool probeR) {
	float fx = static_cast<float>(_subX - _posX) * 1.5f;
	float fy = static_cast<float>(_subY - _posY) * 1.5f;
	if (probeL) {
		if (fx < 0.0f)
			fx = 0.0f;
	} else if (probeR) {
		if (fx > 0.0f)
			fx = 0.0f;
	}
	_posX = _subX + fx;
	_posY = _subY + fy;
	_subX = static_cast<int32>(_posX);
	_subY = static_cast<int32>(_posY);
	_vx = _vy = 0.0;
}

void SubModifier::tickSubState(Runtime *runtime, bool &right, bool &left, bool probeL, bool probeR, bool probeC, bool atLeftWall, bool belowSurface) {
	const Level &level = _levels[_level];

	switch (_state) {
	case kStateFree:
		for (int i = 1; i <= 4; i++)
			setCel(runtime, grandchild(kSubChildTubeWorms, i), 1);
		_subX = static_cast<int32>(_posX);
		_subY = static_cast<int32>(_posY);
		break;

	case kStateSurface:
		_subX = static_cast<int32>(_posX);
		_posY = _subY;
		break;

	case kStateBlocked:
		_posX = _subX;
		_posY = _subY;
		break;

	case kStateBounce:
		sendMessage(runtime, "PlaySFX4");
		bounce(probeL, probeR);
		break;

	case kStateSquid:
		if (_squid.state == 2) {
			// Carried off by the squid, off screen
			int32 prevY = _subY;
			_subCel = kSubCelRight;
			setCel(runtime, child(kSubChildSub), _subCel);
			_vx = _vy = 0.0;
			_posX = 0.0;
			_posY = kSubHidden;
			_squidReenterX = 0;
			_subX = 0;
			_subY = kSubHidden;
			placeCentered(runtime, child(kSubChildSub), _subX, _subY);
			if (prevY < kSubHidden || _squid.cel == 4) {
				_squid.cel = 4;
				setCel(runtime, child(kSubChildSquid), 4);
			}
		} else if (_subX < level.start.x) {
			// The squid is gone, the sub comes back in from the left
			_squidReenterX += 3;
			_posX = _squidReenterX;
			_posY = _squid.y;
			_subX = static_cast<int32>(_posX);
			_subY = static_cast<int32>(_posY);
			placeCentered(runtime, child(kSubChildSub), _subX, _subY);
			right = left = false;
		} else {
			right = true;
			_state = kStateFree;
		}
		break;

	case kStateSpin: {
		// Three turns through the spin cels, drifting toward the smoker and up
		int32 cel = _subCel;
		right = left = false;
		_subCel = cel + 1;
		if (_stateCounter < 1) {
			_state = kStateFree;
			_vx = _vy = 0.0;
			if (cel == kSubCelLeft)
				left = true;
			else if (cel == kSubCelRight)
				right = true;
		} else {
			int32 d = _subX - _spinTargetX;
			_vx = (d < -15) ? 15 : (d < -10) ? 10 : (d < 0) ? 3 : (d < 11) ? -3 : (d < 16) ? -10 : -15;
			_vy = 1 - _spinLift;
			_posX = _subX + _vx;
			_posY = _subY + _vy;
			if (cel == 8 || cel == 17) {
				_stateCounter--;
				_subCel = cel - 7;
				setCel(runtime, child(kSubChildSub), _subCel);
			} else {
				setCel(runtime, child(kSubChildSub), cel);
			}
		}
		break;
	}

	case kStateHit:
		_stateCounter--;
		if (_stateCounter < 1) {
			sendMessage(runtime, "GameOver");
			_state = kStateFree;
		} else {
			right = left = false;
			_subCel = kSubCelRight;
			setCel(runtime, child(kSubChildSub), _subCel);
			_vx = _vy = 0.0;
			_posX = level.start.x;
			_posY = level.start.y;
			_subX = level.start.x;
			_subY = level.start.y;
			placeCentered(runtime, child(kSubChildSub), _subX, _subY);
		}
		break;

	case kStateTubeWorm:
		for (int i = 0; i < 4; i++) {
			if (!_tubeWormHolding[i]) {
				// Detected again with the tube worms below
				_state = kStateFree;
				continue;
			}

			_subCel = (_subCel < kSubCelRight) ? kSubCelLeft : kSubCelRight;
			setCel(runtime, child(kSubChildSub), _subCel);
			setCel(runtime, grandchild(kSubChildTubeWorms, i + 1), 2);
			_vx = _vy = 0.0;

			int32 dx = _subX - _tubeWorms[i].x;
			if (dx < 52)
				_posX += 20.0;
			else if (dx < 62)
				_posX += 1.0;
			if (dx >= 73)
				_posX -= 10.0;
			else if (dx > 62)
				_posX -= 1.0;

			int32 dy = _subY - _tubeWorms[i].y;
			if (dy < 52)
				_posY += 20.0;
			else if (dy < 82)
				_posY += 10.0;
			else if (dy < 92)
				_posY += 1.0;
		}
		break;

	case kStateLift:
		// Pulled up into the bus by the line
		_posX = 597.0;
		_posY -= _vy;
		_vx = _vy = 0.0;
		if (_lineCel < 3) {
			placeTopLeft(runtime, child(kSubChildLine), 572, 12);
			_lineCel++;
			setCel(runtime, child(kSubChildLine), _lineCel);
		}
		if (_subY != kSubHidden) {
			if (_subY < 95)
				_posY += 10.0;
			else if (_subY < 105)
				_posY += 1.0;
			else if (_subY == 105)
				_posY = 105.0;
			else if (_subY < 116)
				_posY -= 1.0;
			else
				_posY -= 10.0;
		}
		if (_subY == 105) {
			_lineCel = 4;
			setCel(runtime, child(kSubChildLine), _lineCel);
			_posX = 0.0;
			_posY = kSubHidden;
		}
		if (_subY == kSubHidden) {
			if (_lineCel < 8) {
				_lineCel++;
				setCel(runtime, child(kSubChildLine), _lineCel);
			} else {
				_paused = true;
				sendMessage(runtime, "Advance");
			}
		}
		break;

	case kStateShield:
		_shieldTimer--;
		if (probeL || probeR)
			bounce(probeL, probeR);
		else if (atLeftWall || probeC) {
			_posX = _subX;
			_posY = _subY;
		} else if (belowSurface) {
			_subX = static_cast<int32>(_posX);
			_subY = static_cast<int32>(_posY);
		} else {
			_subX = static_cast<int32>(_posX);
			_posY = _subY;
		}

		if (_shieldTimer < 1) {
			// Blinks out one step a tick
			if (_shieldBlinks < 1) {
				if (_subCel == kSubCelShieldRight)
					_subCel = kSubCelRight;
				else if (_subCel == kSubCelShieldLeft)
					_subCel = kSubCelLeft;
				_shieldBlinks = (_level < 11) ? 5 : (_level <= 20) ? 4 : 3;
				_state = kStateFree;
			} else {
				if (_subCel == kSubCelShieldRight)
					_subCel = kSubCelRight;
				else if (_subCel == kSubCelRight) {
					_subCel = kSubCelShieldRight;
					_shieldBlinks--;
				}
				if (_subCel == kSubCelShieldLeft)
					_subCel = kSubCelLeft;
				else if (_subCel == kSubCelLeft) {
					_subCel = kSubCelShieldLeft;
					_shieldBlinks--;
				}
			}
		} else if (right)
			_subCel = kSubCelShieldRight;
		else if (left)
			_subCel = kSubCelShieldLeft;
		else if (_subCel == kSubCelRight)
			_subCel = kSubCelShieldRight;
		else if (_subCel == kSubCelLeft)
			_subCel = kSubCelShieldLeft;
		right = left = false;
		break;

	default:
		break;
	}
}

void SubModifier::hit(Runtime *runtime, const char *sfx) {
	_state = kStateHit;
	_stateCounter = 6;
	sendMessage(runtime, sfx);
}

void SubModifier::spawnRock(Runtime *runtime) {
	const Level &level = _levels[_level];

	_rockSpawnCountdown--;
	if (_rockSpawnCountdown >= 1)
		return;

	_rockSpawnCountdown = level.rockInterval;
	int c = runtime->getRandom()->getRandomNumber(2);
	if (_rockSpawnIndex > 29)
		_rockSpawnIndex = 0;

	// Drops from the surface to a resting place, if a rock of that size is free
	for (int i = 0; i < 8; i++) {
		Rock &rock = _rocks[c][i];
		if (rock.state != 4)
			continue;

		rock.x = level.rockSpawn[_rockSpawnIndex].x;
		rock.y = 0;
		rock.targetY = level.rockSpawn[_rockSpawnIndex].y;
		rock.state = 1;
		_rockSpawnIndex++;
		placeCentered(runtime, grandchild(kSubChildSmallRocks + c, i + 1), rock.x, rock.y);
		break;
	}
}

void SubModifier::tickRocks(Runtime *runtime) {
	const Level &level = _levels[_level];

	for (int c = 0; c < 3; c++) {
		for (int i = 0; i < 8; i++) {
			Rock &rock = _rocks[c][i];
			Structural *element = grandchild(kSubChildSmallRocks + c, i + 1);

			if (rock.state == 1 || rock.state == 3) {
				if (rock.y < 46) {
					if (rock.y == _splashY) {
						for (int s = 0; s < 4; s++) {
							Splash &splash = _splashes[s];
							if (!splash.active) {
								splash.x = rock.x;
								splash.y = 50;
								splash.active = true;
								placeCentered(runtime, grandchild(kSubChildSplash, s + 1), splash.x, splash.y);
								break;
							}
						}
					}
				} else {
					// Turns into a sample as it sinks
					rock.animTimer--;
					if (rock.animTimer < 1 && rock.cel < kSubRestingRockCel) {
						rock.animTimer = _rockAnimInterval;
						rock.cel++;
						setCel(runtime, element, rock.cel);
					}
				}

				if (rock.y < rock.targetY) {
					rock.y += level.fallSpeed;
					placeCentered(runtime, element, rock.x, rock.y);
				} else
					rock.state = 3;
			}

			if (rock.state == 4 || _state == kStateSpin || _state == kStateLift)
				continue;

			int32 dx = _subX - rock.x;
			int32 dy = _subY - rock.y;
			if (rock.cel == kSubRestingRockCel) {
				if (dx > -31 && dx < 33 && dy > -13 && dy < 21) {
					placeCentered(runtime, element, 0, kSubHidden);
					rock.state = 4;
					rock.cel = 1;
					setCel(runtime, element, 1);
					_vx = _vy = 0.0;
					_points += 50;
					sendMessage(runtime, "PlaySFX3");
					sendMessage(runtime, "UpdateScore");
				}
			} else if (_state != kStateShield) {
				bool hitSub;
				if (rock.cel <= 2)
					hitSub = (dx > -37 && dx < 37 && dy > -15 && dy < 27);
				else if (rock.cel <= 7)
					hitSub = (dx > -39 && dx < 35 && dy > -19 && dy < 25);
				else
					hitSub = (dx > -35 && dx < 33 && dy > -17 && dy < 21);
				if (hitSub)
					hit(runtime, "PlaySFX8");
			}
		}
	}
}

void SubModifier::tickGems(Runtime *runtime) {
	const Level &level = _levels[_level];

	for (int i = 0; i < 3; i++) {
		Gem &gem = _gems[i];
		const GemSpawn &spawn = level.gems[i];
		Structural *element = grandchild(kSubChildGems, i + 1);

		gem.timerA--;
		gem.timerB--;

		if (gem.y < kSubHidden) {
			// Drops to the surface, floats there for a while, then sinks and fades
			bool reset = false;
			if (gem.y < 46 || gem.timerA > 0) {
				if (gem.y == _splashY) {
					gem.timerB = spawn.surfaceHold;
					gem.y += level.fallSpeed;
					placeCentered(runtime, element, gem.x, gem.y);
				}
			} else if (gem.cel < kSubGemGoneCel) {
				gem.timerA = 3;
				gem.cel++;
				setCel(runtime, element, gem.cel);
			} else
				reset = true;

			if (gem.cel >= kSubGemGoneCel)
				reset = true;
			else if (gem.timerB < 1) {
				gem.y += level.fallSpeed;
				placeCentered(runtime, element, gem.x, gem.y);
			}

			if (reset) {
				gem.timerA = spawn.respawn;
				gem.cel = 1;
				gem.x = 0;
				gem.y = kSubHidden;
				placeCentered(runtime, element, 0, kSubHidden);
				setCel(runtime, element, 1);
			}
		} else if (gem.timerA < 1) {
			gem.x = spawn.pos.x;
			gem.y = spawn.pos.y;
			placeCentered(runtime, element, gem.x, gem.y);
		}

		if (gem.cel < 1 || gem.cel > 20 || (gem.cel <= 13 && gem.y >= kSubHidden))
			continue;

		const SubBox &box = kSubGemBoxes[MAX<int32>(gem.cel - 13, 0)];
		if (subInBox(box, _subX - gem.x, _subY - gem.y)) {
			gem.timerA = spawn.respawn;
			gem.cel = 1;
			gem.x = 0;
			gem.y = kSubHidden;
			placeCentered(runtime, element, 0, kSubHidden);
			setCel(runtime, element, 1);
			_points += 100;
			sendMessage(runtime, "PlaySFX2");
			sendMessage(runtime, "UpdateScore");
		}
	}
}

void SubModifier::tickSplashes(Runtime *runtime) {
	for (int i = 0; i < 4; i++) {
		Splash &splash = _splashes[i];
		if (!splash.active)
			continue;

		Structural *element = grandchild(kSubChildSplash, i + 1);
		if (splash.cel < 9) {
			splash.cel++;
			setCel(runtime, element, splash.cel);
		} else {
			splash.cel = 1;
			setCel(runtime, element, 1);
			placeCentered(runtime, element, 0, kSubHidden);
			splash.active = false;
		}
	}
}

void SubModifier::tickVents(Runtime *runtime) {
	const Level &level = _levels[_level];

	for (int type = 0; type < 3; type++) {
		for (int i = 0; i < 4; i++) {
			Vent &vent = _vents[type][i];
			const Hazard &data = level.vents[type][i];
			Structural *element = grandchild(kSubChildTallVents + type, i + 1);

			// Idle, then a warning on cel 2, then the eruption at a cel a tick
			vent.timer--;
			if (vent.state == 0) {
				if (vent.timer < 1 && vent.y < kSubHidden) {
					vent.timer = data.warn;
					vent.state = 1;
					vent.cel = 2;
					setCel(runtime, element, 2);
				}
			} else if (vent.timer < 1) {
				if (vent.cel < 11) {
					vent.cel++;
					setCel(runtime, element, vent.cel);
				} else {
					vent.cel = 1;
					setCel(runtime, element, 1);
					vent.timer = data.idle;
					vent.state = 0;
				}
			}

			if (vent.state != 1 || _state == kStateShield)
				continue;

			int32 dx = _subX - vent.x;
			int32 dy = _subY - vent.y;
			for (const SubBox &box : kSubVentBoxes[type]) {
				if (box.cel != vent.cel || !subInBox(box, dx, dy))
					continue;
				hit(runtime, "PlaySFX7");
				if (!box.separate)
					break;
			}
		}
	}
}

void SubModifier::tickTubeWorms(Runtime *runtime) {
	if (_state == kStateTubeWorm)
		return;

	if (_state == kStateShield) {
		for (int i = 1; i <= 4; i++)
			setCel(runtime, grandchild(kSubChildTubeWorms, i), 1);
		return;
	}

	if (_state == kStateHit || _subY >= kSubHidden)
		return;

	for (int i = 0; i < 4; i++) {
		int32 dx = _subX - _tubeWorms[i].x;
		int32 dy = _subY - _tubeWorms[i].y;
		_tubeWormHolding[i] = false;
		for (const SubBox &box : kSubTubeWormBoxes) {
			if (box.cel == i + 1 && subInBox(box, dx, dy)) {
				_state = kStateTubeWorm;
				_tubeWormHolding[i] = true;
				break;
			}
		}
	}
}

void SubModifier::tickSmokers(Runtime *runtime) {
	const Level &level = _levels[_level];

	for (int i = 0; i < 8; i++) {
		Vent &smoker = _smokers[i];
		const Hazard &data = level.smokers[i];
		Structural *element = grandchild(kSubChildSmokers, i + 1);

		smoker.timer--;
		if (smoker.timer < 1) {
			if (smoker.state == 0) {
				smoker.cel = 2;
				setCel(runtime, element, 2);
				smoker.timer = data.warn;
				smoker.state = 1;
			} else if (smoker.state == 1) {
				if (smoker.cel < 11 && smoker.y < kSubHidden) {
					smoker.cel++;
					setCel(runtime, element, smoker.cel);
				} else {
					smoker.cel = 1;
					setCel(runtime, element, 1);
					smoker.timer = data.idle;
					smoker.state = 0;
				}
			}
		}

		if (smoker.state != 1 || _state == kStateLift || _state == kStateShield)
			continue;

		int32 dx = _subX - smoker.x;
		int32 dy = _subY - smoker.y;
		for (const SubBox &box : kSubSmokerBoxes) {
			if (box.cel == smoker.cel && subInBox(box, dx, dy)) {
				// Spins the sub and blows it up toward the surface
				_state = kStateSpin;
				sendMessage(runtime, "PlaySFX5");
				_spinLift = (_subY - 36) / 24;
				_spinTargetX = smoker.x + 58;
				_stateCounter = 3;
				break;
			}
		}
	}
}

void SubModifier::tickStarFish(Runtime *runtime) {
	if (_subY >= kSubHidden)
		return;

	for (int i = 0; i < 3; i++) {
		int32 dx = _subX - _starFish[i].x;
		int32 dy = _subY - _starFish[i].y;
		if (dx > -18 && dx < 66 && dy > -5 && dy < 28) {
			_starFish[i] = Common::Point(0, kSubHidden);
			placeCentered(runtime, grandchild(kSubChildStarFish, i + 1), 0, kSubHidden);
			_vx = _vy = 0.0;
			sendMessage(runtime, "PlaySFX1");
			sendMessage(runtime, "ExtraChance");
		}
	}
}

void SubModifier::tickSquid(Runtime *runtime) {
	const Level &level = _levels[_level];
	Structural *element = child(kSubChildSquid);

	// Waits, peeks in at the right edge, then swims across. No rocks fall while it's out.
	_squid.timer--;
	if (_squid.state == 0) {
		if (_squid.timer < 1 && _squid.y < kSubHidden) {
			_squid.timer = level.squid.warn;
			_squid.state = 1;
		}
	} else if (_squid.state == 1) {
		_rockSpawnCountdown++;
		if (_squid.timer < 1 && _squid.y < kSubHidden)
			_squid.state = 2;
		else {
			_squid.x = 580;
			placeTopLeft(runtime, element, _squid.x, _squid.y);
		}
	} else if (_squid.state == 2) {
		_rockSpawnCountdown++;
		if (_squid.timer < 1) {
			if (_squid.x < -224) {
				_squid.cel = 1;
				setCel(runtime, element, 1);
				_squid.timer = level.squid.idle;
				_squid.state = 0;
			} else if (_squid.cel < 4) {
				_squid.x -= level.squidSpeed;
				placeTopLeft(runtime, element, _squid.x, _squid.y);
				_squid.cel = (_squid.cel < 3) ? _squid.cel + 1 : 1;
				setCel(runtime, element, _squid.cel);
			} else {
				// Carrying the sub, faster
				_squid.x -= level.squidSpeed + 9;
				placeTopLeft(runtime, element, _squid.x, _squid.y);
				_squid.cel = (_squid.cel < 8) ? _squid.cel + 1 : 6;
				setCel(runtime, element, _squid.cel);
			}
		}
	}

	if (_state == kStateLift || _state == kStateShield || _squid.state == 1)
		return;

	int32 dx = _subX - _squid.x;
	int32 dy = _subY - _squid.y;
	if (dx > -14 && dx < 226 && dy > 8 && dy < 41) {
		sendMessage(runtime, "StopSFX5");
		sendMessage(runtime, "PlaySFX6");
		_state = kStateSquid;
	}
}

void SubModifier::tickBubbles(Runtime *runtime) {
	const Level &level = _levels[_level];
	Structural *element = child(kSubChildBubbles);

	_bubbles.timer--;
	if (_bubbles.timer < 1) {
		if (_bubbles.cel < 9) {
			_bubbles.cel++;
			setCel(runtime, element, _bubbles.cel);
		} else {
			_bubbles.cel = 1;
			setCel(runtime, element, 1);
			_bubbles.timer = level.bubblesPeriod;
		}
	}

	if (_state == kStateSquid)
		return;

	// Inside the bubbles, the sub is shielded for 15 seconds
	int32 dx = _subX - _bubbles.x;
	int32 dy = _subY - _bubbles.y;
	for (const SubBox &box : kSubBubbleBoxes) {
		if (box.cel == _bubbles.cel && dx >= box.x0 && dx <= box.x1 && dy >= box.y0 && dy <= box.y1) {
			_state = kStateShield;
			_shieldTimer = 150;
			break;
		}
	}
}

Common::SharedPtr<Modifier> SubModifier::shallowClone() const {
	Common::SharedPtr<SubModifier> clone(new SubModifier(*this));
	clone->_scheduledEvent.reset();
	clone->_keySignaller.reset();
	clone->_running = false;
	clone->_initialized = false;
	return clone;
}

const char *SubModifier::getDefaultName() const {
	return "Sub";
}

// Children of the owner element
enum RoverRaceChild {
	kRoverChildPlayer = 1,
	kRoverChildLiz,
	kRoverChildExtraLife,
	kRoverChildSun,
	kRoverChildActiveSun,
	kRoverChildTires,
	kRoverChildActiveTires,
	kRoverChildCloud,
	kRoverChildActiveCloud,
	kRoverChildItems,
	kRoverChildTwisters,
	kRoverChildBoulders,
	kRoverChildCraters,
	kRoverChildCollision,
	kRoverChildLevels,
};

static const int kRoverPickupChild[4] = {kRoverChildExtraLife, kRoverChildSun, kRoverChildTires, kRoverChildCloud};

// The plug-in's own constants, not M_PI
static const double kRoverPi = 3.14159;
static const double kRoverTwoPi = 2.0 * kRoverPi;
static const double kRoverCelStep = kRoverPi / 12.0;

static const double kRoverSpeed = 8.0;
static const int32 kRoverPickupBox = 18;
static const int32 kRoverPowerTime = 60;
static const int32 kRoverHidden = 1000;

static int32 roverRound(double v) {
	return static_cast<int32>(v >= 0.0 ? v + 0.5 : v - 0.5);
}

RoverRaceModifier::RoverRaceModifier()
	: _levelsLoaded(false), _running(false), _paused(false), _levelReady(false), _level(0), _points(0), _rocks(0), _lizRocks(0),
	  _rocksLeft(0), _tick(0), _spawnPoint(kNumPathPoints - 1), _started(false), _lizDelay(0), _lizWaypoint(0), _collisionAnim(0) {
	memset(_spawnIndex, 0, sizeof(_spawnIndex));
	memset(&_player, 0, sizeof(_player));
	memset(&_liz, 0, sizeof(_liz));
	memset(_items, 0, sizeof(_items));
	for (Pickup &pickup : _pickups) {
		pickup.x = pickup.y = kRoverHidden;
		pickup.time = 0;
	}
}

RoverRaceModifier::~RoverRaceModifier() {
	stopGame();
}

bool RoverRaceModifier::load(const PlugInModifierLoaderContext &context, const Data::KW::GameModifier &data) {
	return loadDlgMgrEvent(_startWhen, data.enableWhen) && loadDlgMgrEvent(_stopWhen, data.disableWhen);
}

// The levels and boulder outlines are tables in the plug-in's data section.
// The level fields are 32-bit but always fit in 16 bits.
bool RoverRaceModifier::loadLevels() {
	if (_levelsLoaded)
		return !_levels.empty();

	_levelsLoaded = true;

	Common::File f;
	if (!f.open(Common::Path("mTrop_Resources/RoverRace.r32")) && !f.open(Common::Path("RoverRace.r32"))) {
		warning("RoverRace: RoverRace.r32 from the installed game is needed for the levels");
		return false;
	}

	Common::Array<byte> image;
	image.resize(f.size());
	f.read(&image[0], image.size());

	bool failed = false;
	auto readS32 = [&](uint32 va) -> int16 {
		uint32 offset = va - 0x10000000;
		if (offset + 4 > image.size()) {
			failed = true;
			return 0;
		}
		return static_cast<int16>(READ_LE_UINT32(&image[offset]));
	};
	auto readS16 = [&](uint32 va) -> int16 {
		uint32 offset = va - 0x10000000;
		if (offset + 2 > image.size()) {
			failed = true;
			return 0;
		}
		return static_cast<int16>(READ_LE_UINT16(&image[offset]));
	};
	auto readPoint = [&](uint32 va) -> Common::Point {
		return Common::Point(readS32(va), readS32(va + 4));
	};

	for (int b = 0; b < kNumBoulders; b++) {
		for (int i = 0; i < kPolygonPoints; i++)
			_polygons[b][i] = readPoint(0x10037030 + (b * kPolygonPoints + i) * 8);
	}

	// Spawn time lists in the order of PickupType
	static const uint32 kSpawnTimeOffsets[kNumPickups] = {0x3e8, 0x3ac, 0x3c0, 0x3d4};

	Common::Array<Level> levels;
	levels.resize(kNumLevels + 1);
	for (uint l = 1; l <= kNumLevels; l++) {
		Level &level = levels[l];
		uint32 base = 0x100373f0 + (l - 1) * 0x3fc;

		for (int i = 0; i < kNumItems; i++)
			level.items[i] = readPoint(base + i * 8);
		for (int i = 0; i < kNumBoulders; i++)
			level.boulders[i] = readPoint(base + 0xb8 + i * 8);
		for (int i = 0; i < kNumCraters; i++)
			level.craters[i] = readPoint(base + 0x130 + i * 8);
		for (int i = 0; i < kNumPathPoints; i++)
			level.path[i] = readPoint(base + 0x190 + i * 18);
		for (int p = 0; p < kNumPickups; p++) {
			for (int i = 0; i < kNumSpawnTimes; i++)
				level.spawnTimes[p][i] = readS16(base + kSpawnTimeOffsets[p] + i * 2);
		}
	}

	// Level 1's first rock and Liz's first waypoint, and the first boulder's first point
	if (failed || levels[1].items[0] != Common::Point(126, 185) || levels[1].path[0] != Common::Point(529, 278) || _polygons[0][0] != Common::Point(-90, 23)) {
		warning("RoverRace: RoverRace.r32 isn't the expected version");
		return false;
	}

	_levels.swap(levels);
	return true;
}

bool RoverRaceModifier::respondsToEvent(const Event &evt) const {
	return _startWhen.respondsTo(evt) || _stopWhen.respondsTo(evt) || evt.eventType == EventIDs::kParentDisabled || evt.eventType == EventIDs::kMouseDown;
}

VThreadState RoverRaceModifier::consumeMessage(Runtime *runtime, const Common::SharedPtr<MessageProperties> &msg) {
	const Event &evt = msg->getEvent();

	// Enabling only starts the clock, the title sets the level with gamelevel
	if (_startWhen.respondsTo(evt)) {
		_paused = true;
		if (!_running) {
			_running = true;
			_scheduledEvent = runtime->getScheduler().scheduleMethod<RoverRaceModifier, &RoverRaceModifier::tick>(runtime->getPlayTime() + 83, this);
		}
		return kVThreadReturn;
	}

	if (_stopWhen.respondsTo(evt) || evt.eventType == EventIDs::kParentDisabled) {
		stopGame();
		return kVThreadReturn;
	}

	// While paused, or while the rover is still in its bay, a click skips the speech
	if (evt.eventType == EventIDs::kMouseDown) {
		if (_paused || _player.y >= 420.0)
			sendMessage(runtime, "PausedClick");
		else if (_level > 0)
			steerPlayer(runtime);
	}

	return kVThreadReturn;
}

void RoverRaceModifier::disable(Runtime *runtime) {
	stopGame();
}

void RoverRaceModifier::stopGame() {
	_running = false;
	if (_scheduledEvent) {
		_scheduledEvent->cancel();
		_scheduledEvent.reset();
	}
}

bool RoverRaceModifier::readAttribute(MiniscriptThread *thread, DynamicValue &result, const Common::String &attrib) {
	if (attrib == "paused") {
		result.setBool(_paused);
		return true;
	} else if (attrib == "gamelevel") {
		result.setInt(_level);
		return true;
	} else if (attrib == "rocks") {
		result.setInt(_rocks);
		return true;
	} else if (attrib == "lizrocks") {
		result.setInt(_lizRocks);
		return true;
	} else if (attrib == "points") {
		result.setInt(_points);
		return true;
	}

	return Modifier::readAttribute(thread, result, attrib);
}

MiniscriptInstructionOutcome RoverRaceModifier::writeRefAttribute(MiniscriptThread *thread, DynamicValueWriteProxy &result, const Common::String &attrib) {
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

MiniscriptInstructionOutcome RoverRaceModifier::WriteProxy::write(MiniscriptThread *thread, const DynamicValue &assigned, void *objectRef, uintptr ptrOrOffset) {
	RoverRaceModifier *self = static_cast<RoverRaceModifier *>(objectRef);

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
		if (level >= 1 && level <= kNumLevels)
			self->setupLevel(thread->getRuntime(), level);
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

		// A finished race can't be unpaused
		if (self->_levelReady || flag)
			self->_paused = flag;
	}

	return kMiniscriptInstructionOutcomeContinue;
}

MiniscriptInstructionOutcome RoverRaceModifier::WriteProxy::refAttrib(MiniscriptThread *thread, DynamicValueWriteProxy &proxy, void *objectRef, uintptr ptrOrOffset, const Common::String &attrib) {
	return kMiniscriptInstructionOutcomeFailed;
}

MiniscriptInstructionOutcome RoverRaceModifier::WriteProxy::refAttribIndexed(MiniscriptThread *thread, DynamicValueWriteProxy &proxy, void *objectRef, uintptr ptrOrOffset, const Common::String &attrib, const DynamicValue &index) {
	return kMiniscriptInstructionOutcomeFailed;
}

Structural *RoverRaceModifier::child(int index) const {
	Structural *owner = findStructuralOwner();
	if (!owner || index < 1 || index > static_cast<int>(owner->getChildren().size()))
		return nullptr;
	return owner->getChildren()[index - 1].get();
}

Structural *RoverRaceModifier::grandchild(int index, int subIndex) const {
	Structural *container = child(index);
	if (!container || subIndex < 1 || subIndex > static_cast<int>(container->getChildren().size()))
		return nullptr;
	return container->getChildren()[subIndex - 1].get();
}

// The inverse of getCenter() for positive coordinates, so a parked rover reads back its target
void RoverRaceModifier::placeCentered(Runtime *runtime, Structural *element, int32 x, int32 y) {
	if (!element || !element->isElement() || !static_cast<Element *>(element)->isVisual())
		return;

	const Common::Rect &rect = static_cast<VisualElement *>(element)->getRelativeRect();
	placeTopLeft(runtime, element, x - rect.width() / 2, y - rect.height() / 2);
}

void RoverRaceModifier::placeTopLeft(Runtime *runtime, Structural *element, int32 x, int32 y) {
	if (!element)
		return;

	DynamicValue pos;
	pos.setPoint(Common::Point(x, y));
	dlgMgrWriteAttribute(runtime, this, element, "position", pos);
}

void RoverRaceModifier::hide(Runtime *runtime, Structural *element) {
	placeCentered(runtime, element, kRoverHidden, kRoverHidden);
}

Common::Point RoverRaceModifier::getCenter(Structural *element) const {
	if (!element || !element->isElement() || !static_cast<Element *>(element)->isVisual())
		return Common::Point(0, 0);

	// As VisualElement::getCenterPosition()
	const Common::Rect &rect = static_cast<VisualElement *>(element)->getRelativeRect();
	return Common::Point((rect.left + rect.right) / 2, (rect.top + rect.bottom) / 2);
}

int32 RoverRaceModifier::getCel(Runtime *runtime, Structural *element) {
	DynamicValue cel;
	if (dlgMgrReadAttribute(runtime, this, element, "cel", cel) && cel.getType() == DynamicValueTypes::kInteger)
		return cel.getInt();
	return 1;
}

void RoverRaceModifier::setCel(Runtime *runtime, Structural *element, int32 cel) {
	dlgMgrSetInt(runtime, this, element, "cel", cel);
}

void RoverRaceModifier::sendMessage(Runtime *runtime, const char *messageName) {
	uint32 messageID = 0;
	if (!runtime->getProject()->findAuthorMessageID(messageName, messageID))
		return;

	Structural *owner = findStructuralOwner();
	if (!owner)
		return;

	debug(2, "RoverRace: Sending '%s'", messageName);
	Common::SharedPtr<MessageProperties> props(new MessageProperties(Event(EventIDs::kAuthorMessage, messageID), DynamicValue(), getSelfReference()));
	Common::SharedPtr<MessageDispatch> dispatch(new MessageDispatch(props, owner, true, true, false));
	runtime->queueMessage(dispatch);
}

// Even-odd test, as the original's polygon region
bool RoverRaceModifier::inBoulder(int boulder, int32 x, int32 y) const {
	const Common::Point *poly = _polygons[boulder];
	x -= _boulders[boulder].x;
	y -= _boulders[boulder].y;

	bool inside = false;
	for (int i = 0, j = kPolygonPoints - 1; i < kPolygonPoints; j = i++) {
		const Common::Point &a = poly[i];
		const Common::Point &b = poly[j];
		if ((a.y > y) != (b.y > y)) {
			double crossX = a.x + static_cast<double>(y - a.y) * (b.x - a.x) / (b.y - a.y);
			if (x < crossX)
				inside = !inside;
		}
	}
	return inside;
}

bool RoverRaceModifier::inCrater(int32 x, int32 y) const {
	for (const Common::Rect &crater : _craters) {
		if (crater.left < x && x < crater.right && crater.top < y && y < crater.bottom)
			return true;
	}
	return false;
}

void RoverRaceModifier::setupLevel(Runtime *runtime, int32 levelNum) {
	if (!loadLevels())
		return;

	debug(2, "RoverRace: Level %d", static_cast<int>(levelNum));
	const Level &level = _levels[levelNum];
	_level = levelNum;
	_points = 0;
	_started = false;
	_lizDelay = (levelNum < 6) ? 60 : (levelNum < 11) ? 30 : 0;
	_tick = 0;
	memset(_spawnIndex, 0, sizeof(_spawnIndex));
	_collisionAnim = 0;
	_spawnPoint = kNumPathPoints - 1;

	if (!findStructuralOwner())
		return;

	_rocks = 0;
	_lizRocks = 0;

	// Both rovers start in their bays below the field and drive 50 pixels up out of them
	static const int32 kStartX[2] = {102, 533};
	for (int r = 0; r < 2; r++) {
		Rover &rover = r ? _liz : _player;
		Structural *element = child(kRoverChildPlayer + r);
		rover.x = kStartX[r];
		rover.y = 439.0;
		placeCentered(runtime, element, kStartX[r], 439);
		rover.targetX = kStartX[r];
		rover.targetY = 389;
		rover.speed = kRoverSpeed;
		rover.cel = 7;
		setCel(runtime, element, 7);
		rover.turn = 0;
		rover.remaining = 50.0;
		rover.heading = 0.0;
		rover.cosH = 0.0;
		rover.sinH = -1.0;
		rover.sunTime = rover.tiresTime = rover.cloudTime = 0;
	}
	_lizWaypoint = 0;

	for (int p = 0; p < kNumPickups; p++) {
		hide(runtime, child(kRoverPickupChild[p]));
		_pickups[p].time = 0;
	}
	hide(runtime, child(kRoverChildActiveSun));
	hide(runtime, child(kRoverChildActiveTires));
	hide(runtime, child(kRoverChildActiveCloud));

	_rocksLeft = 0;
	for (int i = 0; i < kNumItems; i++) {
		Item &item = _items[i];
		item.x = level.items[i].x;
		item.y = level.items[i].y;
		item.state = 0;
		if (item.x == 0) {
			item.x = item.y = kRoverHidden;
		} else {
			item.state = 1;
			if (i < kNumRocks)
				_rocksLeft++;
		}
		placeCentered(runtime, grandchild(kRoverChildItems, i + 1), item.x, item.y);
	}

	// The boulders are drawn in the backgrounds, so only their outlines are used
	for (int i = 0; i < kNumBoulders; i++)
		_boulders[i] = level.boulders[i];

	for (int i = 0; i < kNumCraters; i++) {
		int32 x = level.craters[i].x;
		int32 y = level.craters[i].y;
		if (x == 0)
			x = y = kRoverHidden;

		Structural *crater = grandchild(kRoverChildCraters, i + 1);
		placeCentered(runtime, crater, x, y);
		if (crater && crater->isElement() && static_cast<Element *>(crater)->isVisual())
			_craters[i] = static_cast<VisualElement *>(crater)->getRelativeRect();
		else
			_craters[i] = Common::Rect();
	}

	hide(runtime, child(kRoverChildCollision));

	for (int i = 1; i <= kNumLevels; i++)
		placeTopLeft(runtime, grandchild(kRoverChildLevels, i), 0, (i == levelNum) ? 0 : 600);

	_levelReady = true;
}

void RoverRaceModifier::tick(Runtime *runtime) {
	_scheduledEvent.reset();
	if (!_running)
		return;

	_scheduledEvent = runtime->getScheduler().scheduleMethod<RoverRaceModifier, &RoverRaceModifier::tick>(runtime->getPlayTime() + 83, this);

	if (_paused || _level <= 0)
		return;

	// Both rovers freeze while the collision plays
	if (_collisionAnim > 0) {
		setCel(runtime, child(kRoverChildCollision), _collisionAnim);
		if (--_collisionAnim == 0)
			hide(runtime, child(kRoverChildCollision));
	} else {
		tickPlayer(runtime);
		tickLiz(runtime);
	}

	spawnPickups(runtime);

	// A tie goes to Liz
	if (_rocksLeft < 1) {
		_paused = true;
		_levelReady = false;
		sendMessage(runtime, (_lizRocks < _rocks) ? "YouWin" : "GameOver");
	}

	_tick++;
}

// Points the rover at (x, y) from where its sprite is, starting from the cel it shows
void RoverRaceModifier::aimRover(Runtime *runtime, Rover &rover, Structural *element, int32 x, int32 y) {
	Common::Point pos = getCenter(element);
	rover.x = pos.x;
	rover.y = pos.y;

	int16 dx = static_cast<int16>(x - pos.x);
	int16 dyUp = static_cast<int16>(-(y - pos.y));
	rover.remaining = sqrt(static_cast<double>(dx * dx + dyUp * dyUp));
	rover.heading = atan2(static_cast<double>(dyUp), static_cast<double>(dx));
	rover.cosH = cos(rover.heading);
	rover.sinH = -sin(rover.heading);
	rover.cel = getCel(runtime, element);
	if (rover.heading < 0.0)
		rover.heading += kRoverTwoPi;

	// Cel 1 faces east and the cels go anticlockwise, 15 degrees apart
	rover.turn = (static_cast<int16>(roverRound(rover.heading / kRoverCelStep)) + 1) - rover.cel;
	if (rover.turn > 12)
		rover.turn -= 24;
	else if (rover.turn < -12)
		rover.turn += 24;
}

void RoverRaceModifier::steerPlayer(Runtime *runtime) {
	_started = true;

	Common::Point target = runtime->getCachedMousePosition();
	_player.targetX = CLIP<int32>(target.x, 30, 610);
	_player.targetY = CLIP<int32>(target.y, 30, 385);
	aimRover(runtime, _player, child(kRoverChildPlayer), _player.targetX, _player.targetY);
}

// Turning takes one cel a tick, and the rover doesn't move meanwhile
void RoverRaceModifier::turnRover(Runtime *runtime, Rover &rover, Structural *element) {
	if (rover.turn > 0) {
		rover.turn--;
		if (++rover.cel > 24)
			rover.cel -= 24;
	} else {
		rover.turn++;
		if (--rover.cel < 1)
			rover.cel += 24;
	}
	setCel(runtime, element, rover.cel);
}

void RoverRaceModifier::tickPlayer(Runtime *runtime) {
	Structural *element = child(kRoverChildPlayer);

	double speed = _player.speed;
	int32 px = roverRound(_player.x);
	int32 py = roverRound(_player.y);
	if (_player.sunTime > 0) {
		speed *= 2.0;
		if (--_player.sunTime == 0)
			hide(runtime, child(kRoverChildActiveSun));
	}
	if (_player.cloudTime > 0) {
		speed *= 0.5;
		if (--_player.cloudTime == 0) {
			hide(runtime, child(kRoverChildActiveCloud));
		} else {
			placeCentered(runtime, child(kRoverChildActiveCloud), px, py);
			setCel(runtime, child(kRoverChildActiveCloud), _player.cel);
		}
	}

	if (_player.turn != 0) {
		turnRover(runtime, _player, element);
		return;
	}

	Common::Point pos = getCenter(element);
	px = pos.x;
	py = pos.y;
	if (px == _player.targetX && py == _player.targetY && _player.remaining <= 0.0)
		return;

	_player.remaining -= speed;
	if (_player.remaining <= 0.0) {
		px = _player.targetX;
		py = _player.targetY;
		_player.x = px;
		_player.y = py;
	} else {
		_player.x += speed * _player.cosH;
		_player.y += speed * _player.sinH;
		px = roverRound(_player.x);
		py = roverRound(_player.y);
	}

	for (int i = 0; i < kNumItems; i++) {
		Item &item = _items[i];
		if (item.state != 1 || ABS(item.x - px) >= kRoverPickupBox || ABS(item.y - py) >= kRoverPickupBox)
			continue;

		hide(runtime, grandchild(kRoverChildItems, i + 1));
		item.state = 2;
		if (i < kNumRocks) {
			_rocksLeft--;
			_rocks++;
			sendMessage(runtime, "Update Readouts");
			_points += (i < 7) ? 10 : 20;
			sendMessage(runtime, "PlaySFX3");
		} else if (i < 17) {
			_points += 25;
			sendMessage(runtime, "PlaySFX4");
		} else if (i < 20) {
			_points += 50;
			sendMessage(runtime, "PlaySFX5");
		} else {
			_points += 100;
			sendMessage(runtime, "PlaySFX4");
		}
	}

	for (int p = 0; p < kNumPickups; p++) {
		Pickup &pickup = _pickups[p];
		if (pickup.time <= 0 || ABS(pickup.x - px) >= kRoverPickupBox || ABS(pickup.y - py) >= kRoverPickupBox)
			continue;

		hide(runtime, child(kRoverPickupChild[p]));
		pickup.time = -1;
		switch (p) {
		case kPickupExtraLife:
			sendMessage(runtime, "ExtraChance");
			break;
		case kPickupSun:
			// The sun clears a cloud instead of speeding up
			if (_player.cloudTime < 1) {
				_player.sunTime = kRoverPowerTime;
				placeCentered(runtime, child(kRoverChildActiveSun), 260, 418);
			} else {
				_player.cloudTime = 0;
				hide(runtime, child(kRoverChildActiveCloud));
			}
			sendMessage(runtime, "PlaySFX2");
			break;
		case kPickupTires:
			_player.tiresTime = kRoverPowerTime;
			placeCentered(runtime, child(kRoverChildActiveTires), 283, 419);
			sendMessage(runtime, "PlaySFX2");
			break;
		case kPickupCloud:
			// Clouds Liz, or clears her sun (leaving its indicator up, as the original does)
			if (_liz.sunTime < 1)
				_liz.cloudTime = kRoverPowerTime;
			else
				_liz.sunTime = 0;
			sendMessage(runtime, "PlaySFX2");
			break;
		default:
			break;
		}
	}

	// Boulders push the rover out from their centre and stop it. Only the
	// sprite and target move; the position is resynchronised on the next click.
	int32 lastInX = px;
	int32 lastInY = py;
	for (int k = 0; k < kNumBoulders; k++) {
		if (_boulders[k].x == 0 || !inBoulder(k, px, py))
			continue;

		double angle = atan2(static_cast<double>(-(py - _boulders[k].y)), static_cast<double>(px - _boulders[k].x));
		double stepX = cos(angle);
		double stepY = -sin(angle);
		double fx = px;
		double fy = py;
		while (inBoulder(k, px, py)) {
			lastInX = px;
			lastInY = py;
			px = static_cast<int32>(fx);
			py = static_cast<int32>(fy);
			fx += stepX;
			fy += stepY;
		}
		_player.targetX = px;
		_player.targetY = py;
		_player.remaining = 0.0;
		sendMessage(runtime, "PlaySFX1");
	}

	// Craters slow the next tick; tires only wear out while moving
	_player.speed = kRoverSpeed;
	if (_player.tiresTime < 1) {
		if (inCrater(lastInX, lastInY))
			_player.speed *= 0.5;
	} else if (--_player.tiresTime == 0) {
		hide(runtime, child(kRoverChildActiveTires));
	}

	placeCentered(runtime, element, px, py);
}

void RoverRaceModifier::tickLiz(Runtime *runtime) {
	Structural *element = child(kRoverChildLiz);

	double speed = _liz.speed;
	int32 lx = roverRound(_liz.x);
	int32 ly = roverRound(_liz.y);

	// The power-up indicators are shared with the player
	if (_liz.sunTime > 0) {
		speed *= 2.0;
		if (--_liz.sunTime == 0)
			hide(runtime, child(kRoverChildActiveSun));
	}
	if (_liz.cloudTime > 0) {
		speed *= 0.5;
		if (--_liz.cloudTime == 0) {
			hide(runtime, child(kRoverChildActiveCloud));
		} else {
			placeCentered(runtime, child(kRoverChildActiveCloud), lx, ly);
			setCel(runtime, child(kRoverChildActiveCloud), _liz.cel);
		}
	}

	if (_liz.turn != 0) {
		turnRover(runtime, _liz, element);
		return;
	}

	Common::Point before = getCenter(element);
	lx = before.x;
	ly = before.y;

	// Liz waits for the player's first click and then her start delay
	if (!_started)
		return;
	if (_lizDelay >= 1) {
		_lizDelay--;
		return;
	}
	if (lx == _liz.targetX && ly == _liz.targetY && _liz.remaining <= 0.0)
		return;

	_liz.remaining -= speed;
	if (_liz.remaining <= 0.0) {
		// Arrived: head for the next waypoint, which is (0, 0) after the last one
		lx = _liz.targetX;
		ly = _liz.targetY;
		Common::Point next;
		if (_lizWaypoint < kNumPathPoints)
			next = _levels[_level].path[_lizWaypoint++];
		_liz.targetX = next.x;
		_liz.targetY = next.y;
		aimRover(runtime, _liz, element, next.x, next.y);
	} else {
		_liz.x += speed * _liz.cosH;
		_liz.y += speed * _liz.sinH;
		lx = roverRound(_liz.x);
		ly = roverRound(_liz.y);
	}

	for (int i = 0; i < kNumItems; i++) {
		Item &item = _items[i];
		if (item.state != 1 || ABS(item.x - lx) >= kRoverPickupBox || ABS(item.y - ly) >= kRoverPickupBox)
			continue;

		hide(runtime, grandchild(kRoverChildItems, i + 1));
		item.state = 3;
		if (i < kNumRocks) {
			_rocksLeft--;
			_lizRocks++;
			sendMessage(runtime, "Update Readouts");
		}
	}

	// Liz never takes the extra life, and her pickups make no sound
	for (int p = kPickupSun; p < kNumPickups; p++) {
		Pickup &pickup = _pickups[p];
		if (pickup.time <= 0 || ABS(pickup.x - lx) >= kRoverPickupBox || ABS(pickup.y - ly) >= kRoverPickupBox)
			continue;

		hide(runtime, child(kRoverPickupChild[p]));
		pickup.time = -1;
		if (p == kPickupSun) {
			if (_liz.cloudTime < 1) {
				_liz.sunTime = kRoverPowerTime;
				placeCentered(runtime, child(kRoverChildActiveSun), 444, 419);
			} else {
				_liz.cloudTime = 0;
				hide(runtime, child(kRoverChildActiveCloud));
			}
		} else if (p == kPickupTires) {
			_liz.tiresTime = kRoverPowerTime;
			placeCentered(runtime, child(kRoverChildActiveTires), 467, 420);
		} else {
			if (_player.sunTime < 1)
				_player.cloudTime = kRoverPowerTime;
			else
				_player.sunTime = 0;
		}
	}

	// Collision: the player is knocked away and stops, Liz spins and then
	// carries on. The original uses the player's position as of the last click
	// or move, and shows Liz's cel on the player.
	int16 dx = static_cast<int16>(_player.x - lx);
	int16 dy = static_cast<int16>(_player.y - ly);
	double dist = sqrt(static_cast<double>(dx * dx + dy * dy));
	if (dist < 38.0) {
		// Exactly on top of each other the original's knock-back is NaN
		if (dist > 0.0) {
			_player.x += dx * 14.0 / dist;
			_player.y += dy * 14.0 / dist;
		}
		_player.targetX = static_cast<int32>(_player.x);
		_player.targetY = static_cast<int32>(_player.y);
		_player.remaining = 0.0;
		_player.cel += runtime->getRandom()->getRandomNumberRng(1, 24);
		if (_player.cel > 24)
			_player.cel -= 24;
		setCel(runtime, child(kRoverChildPlayer), _liz.cel);
		placeCentered(runtime, child(kRoverChildPlayer), _player.targetX, _player.targetY);

		// Liz is drawn where she was but keeps this tick's advance
		lx = before.x;
		ly = before.y;
		_liz.remaining += speed;
		_liz.turn = static_cast<int32>(runtime->getRandom()->getRandomNumberRng(1, 24)) - 12;
		_liz.cel -= _liz.turn;
		if (_liz.cel < 0)
			_liz.cel += 24;
		else if (_liz.cel > 24)
			_liz.cel -= 24;
		setCel(runtime, element, _liz.cel);

		_collisionAnim = 9;
		setCel(runtime, child(kRoverChildCollision), 9);
		placeCentered(runtime, child(kRoverChildCollision), static_cast<int32>(_player.x - dx / 2), static_cast<int32>(_player.y - dy / 2));
		sendMessage(runtime, "PlaySFX1");

		if (_player.x < 0.0 || _player.x > 640.0 || _player.y < 0.0 || _player.y > 420.0) {
			_paused = true;
			_levelReady = false;
			sendMessage(runtime, "GameOver");
		}
	}

	_liz.speed = kRoverSpeed;
	if (_liz.tiresTime < 1) {
		if (inCrater(lx, ly))
			_liz.speed *= 0.5;
	} else if (--_liz.tiresTime == 0) {
		hide(runtime, child(kRoverChildActiveTires));
	}

	placeCentered(runtime, element, lx, ly);
}

// Each power-up appears at its scheduled times (in units of 8 ticks) at the
// next spawn point, counting down from the end of the path table, and stays
// for 60 ticks
void RoverRaceModifier::spawnPickups(Runtime *runtime) {
	int32 t = _tick / 8;
	if (t <= 0)
		return;

	const Level &level = _levels[_level];
	for (int p = 0; p < kNumPickups; p++) {
		Pickup &pickup = _pickups[p];
		Structural *element = child(kRoverPickupChild[p]);
		if (pickup.time < 1) {
			if (_spawnIndex[p] < kNumSpawnTimes && level.spawnTimes[p][_spawnIndex[p]] == t) {
				pickup.time = kRoverPowerTime;
				if (_spawnPoint < 0 || level.path[_spawnPoint].x == 0)
					_spawnPoint = kNumPathPoints - 1;
				pickup.x = level.path[_spawnPoint].x;
				pickup.y = level.path[_spawnPoint].y;
				_spawnPoint--;
				placeCentered(runtime, element, pickup.x, pickup.y);
				_spawnIndex[p]++;
			}
		} else if (--pickup.time == 0) {
			pickup.x = pickup.y = kRoverHidden;
			hide(runtime, element);
			pickup.time = -1;
		}
	}
}

Common::SharedPtr<Modifier> RoverRaceModifier::shallowClone() const {
	Common::SharedPtr<RoverRaceModifier> clone(new RoverRaceModifier(*this));
	clone->_scheduledEvent.reset();
	clone->_running = false;
	return clone;
}

const char *RoverRaceModifier::getDefaultName() const {
	return "RoverRace";
}

// Values of the bugtype variable
enum BugEatBugType {
	kBugTypeFly = 1,
	kBugTypeMosquito,
	kBugTypePoison,
	kBugTypeChance,
	kBugTypePredator,
	kBugTypeDistraction,
};

static const int32 kBugEatBugScreenWidth = 640;
static const int32 kBugEatBugScreenHeight = 480;
static const int32 kBugEatBugParked = 500;
static const int32 kBugEatBugCooldown = 8;

// PlaySFX1-4 are picked at random when a bug is eaten
static const char *const kBugEatBugSfx[] = {
	"PlaySFX1", "PlaySFX2", "PlaySFX3", "PlaySFX4", "PlaySFXMiss", "PlaySFXPoison", "PlaySFXTimer", "PlaySFXDistract"
};

static int32 bugEatBugPeriod(int32 tickRate) {
	return (tickRate > 0) ? static_cast<int32>(1000.0 / tickRate) : 83;
}

BugEatBugModifier::BugEatBugModifier()
	: _rate(100.0), _tickRate(15), _running(false), _paused(false), _numFlyers(0), _numEdible(0), _numPreds(0), _numDistractions(0),
	  _timerCels(0), _timerTotal(0), _timerLeft(0), _timerWarn(0), _score(0) {
	for (Flyer &flyer : _flyers) {
		flyer.type = flyer.dx = flyer.x = flyer.y = flyer.cel = flyer.pred = 0;
		flyer.dead = false;
	}
	for (int i = 0; i < kMaxStill * 2; i++) {
		Still &still = (i < kMaxStill) ? _preds[i] : _distractions[i - kMaxStill];
		still.cel = still.animEnd = still.cooldown = 1;
	}
}

BugEatBugModifier::~BugEatBugModifier() {
	stopGame();
}

bool BugEatBugModifier::load(const PlugInModifierLoaderContext &context, const Data::KW::BugEatBugModifier &data) {
	if (!loadDlgMgrEvent(_startWhen, data.enableWhen) || !loadDlgMgrEvent(_stopWhen, data.disableWhen))
		return false;

	if (data.destination.type == Data::PlugInTypeTaggedValue::kPoint)
		data.destination.value.asPoint.toScummVMPoint(_destination);
	if (data.rate.type == Data::PlugInTypeTaggedValue::kFloat)
		_rate = data.rate.value.asFloat.toXPFloat().toDouble();
	if (data.tickRate.type == Data::PlugInTypeTaggedValue::kInteger)
		_tickRate = data.tickRate.value.asInt;

	return true;
}

bool BugEatBugModifier::respondsToEvent(const Event &evt) const {
	return _startWhen.respondsTo(evt) || _stopWhen.respondsTo(evt) || evt.eventType == EventIDs::kParentDisabled || evt.eventType == EventIDs::kMouseDown;
}

VThreadState BugEatBugModifier::consumeMessage(Runtime *runtime, const Common::SharedPtr<MessageProperties> &msg) {
	const Event &evt = msg->getEvent();

	if (_startWhen.respondsTo(evt)) {
		startGame(runtime);
		return kVThreadReturn;
	}

	// Stopping only stops the clock, the tables stay
	if (_stopWhen.respondsTo(evt) || evt.eventType == EventIDs::kParentDisabled) {
		stopGame();
		return kVThreadReturn;
	}

	// The original handles clicks even while stopped
	if (evt.eventType == EventIDs::kMouseDown)
		click(runtime);

	return kVThreadReturn;
}

void BugEatBugModifier::disable(Runtime *runtime) {
	stopGame();
}

void BugEatBugModifier::stopGame() {
	_running = false;
	if (_scheduledEvent) {
		_scheduledEvent->cancel();
		_scheduledEvent.reset();
	}
}

bool BugEatBugModifier::readAttribute(MiniscriptThread *thread, DynamicValue &result, const Common::String &attrib) {
	if (attrib == "paused") {
		result.setBool(_paused);
		return true;
	} else if (attrib == "rate") {
		result.setFloat(_rate);
		return true;
	} else if (attrib == "destination") {
		result.setPoint(_destination);
		return true;
	}

	return Modifier::readAttribute(thread, result, attrib);
}

MiniscriptInstructionOutcome BugEatBugModifier::writeRefAttribute(MiniscriptThread *thread, DynamicValueWriteProxy &result, const Common::String &attrib) {
	WritableAttribute writable;
	if (attrib == "paused")
		writable = kAttribPaused;
	else if (attrib == "rate")
		writable = kAttribRate;
	else if (attrib == "destination")
		writable = kAttribDestination;
	else
		return Modifier::writeRefAttribute(thread, result, attrib);

	result.pod.ifc = DynamicValueWriteInterfaceGlue<WriteProxy>::getInstance();
	result.pod.objectRef = this;
	result.pod.ptrOrOffset = writable;
	return kMiniscriptInstructionOutcomeContinue;
}

MiniscriptInstructionOutcome BugEatBugModifier::WriteProxy::write(MiniscriptThread *thread, const DynamicValue &assigned, void *objectRef, uintptr ptrOrOffset) {
	BugEatBugModifier *self = static_cast<BugEatBugModifier *>(objectRef);

	// Variables are passed by reference
	DynamicValue value = assigned;
	if (value.getType() == DynamicValueTypes::kObject) {
		Common::SharedPtr<RuntimeObject> obj = value.getObject().object.lock();
		if (obj && obj->isModifier() && static_cast<Modifier *>(obj.get())->isVariable())
			static_cast<VariableModifier *>(obj.get())->varGetValue(value);
	}

	DynamicValue converted;
	switch (ptrOrOffset) {
	case kAttribPaused:
		if (!value.convertToType(DynamicValueTypes::kBoolean, converted))
			return kMiniscriptInstructionOutcomeFailed;
		self->_paused = converted.getBool();
		break;
	case kAttribRate:
		if (!value.convertToType(DynamicValueTypes::kFloat, converted) || converted.getFloat() < 0.0 || converted.getFloat() > 9999999.0)
			return kMiniscriptInstructionOutcomeFailed;
		self->_rate = converted.getFloat();
		break;
	case kAttribDestination:
		if (!value.convertToType(DynamicValueTypes::kPoint, converted))
			return kMiniscriptInstructionOutcomeFailed;
		self->_destination = converted.getPoint();
		break;
	default:
		break;
	}

	return kMiniscriptInstructionOutcomeContinue;
}

MiniscriptInstructionOutcome BugEatBugModifier::WriteProxy::refAttrib(MiniscriptThread *thread, DynamicValueWriteProxy &proxy, void *objectRef, uintptr ptrOrOffset, const Common::String &attrib) {
	return kMiniscriptInstructionOutcomeFailed;
}

MiniscriptInstructionOutcome BugEatBugModifier::WriteProxy::refAttribIndexed(MiniscriptThread *thread, DynamicValueWriteProxy &proxy, void *objectRef, uintptr ptrOrOffset, const Common::String &attrib, const DynamicValue &index) {
	return kMiniscriptInstructionOutcomeFailed;
}

// The value of the first of the element's own modifiers with that name, or 0
int32 BugEatBugModifier::readVar(Structural *element, const char *name) const {
	for (const Common::SharedPtr<Modifier> &modifier : element->getModifiers()) {
		if (!modifier->getName().equalsIgnoreCase(name))
			continue;

		int32 result = 0;
		if (modifier->isVariable()) {
			DynamicValue value;
			static_cast<VariableModifier *>(modifier.get())->varGetValue(value);
			if (!value.roundToInt(result))
				result = 0;
		}
		return result;
	}

	return 0;
}

void BugEatBugModifier::startGame(Runtime *runtime) {
	if (!_running) {
		_running = true;
		_scheduledEvent = runtime->getScheduler().scheduleMethod<BugEatBugModifier, &BugEatBugModifier::tick>(runtime->getPlayTime() + bugEatBugPeriod(_tickRate), this);
	}

	for (Flyer &flyer : _flyers) {
		flyer.element.reset();
		flyer.type = flyer.dx = flyer.x = flyer.y = flyer.cel = flyer.pred = 0;
		flyer.dead = false;
	}
	for (int i = 0; i < kMaxStill * 2; i++) {
		Still &still = (i < kMaxStill) ? _preds[i] : _distractions[i - kMaxStill];
		still.element.reset();
		still.body = still.mouth = Common::Rect();
		still.mouthCenter = Common::Point();
		still.cel = still.animEnd = still.cooldown = 1;
	}
	_numFlyers = _numEdible = _numPreds = _numDistractions = 0;
	_timerElement.reset();
	_scoreElement.reset();
	_timerCels = 0;

	Structural *owner = findStructuralOwner();
	if (owner) {
		for (const Common::SharedPtr<Structural> &child : owner->getChildren()) {
			Structural *element = child.get();
			if (!element->isElement() || !static_cast<Element *>(element)->isVisual())
				continue;

			const Common::Rect &rect = static_cast<VisualElement *>(element)->getRelativeRect();
			int32 type = readVar(element, "bugtype");
			int32 speed = readVar(element, "bugspeed");

			if (type < 1) {
				// Not a bug: look for the timer bar and the score
				int32 time = readVar(element, "gametime");
				if (time > 0) {
					_timerElement = element->getSelfReference();
					_timerTotal = _timerLeft = _tickRate * time;
					_timerWarn = static_cast<int32>(_tickRate * 4.3);

					DynamicValue cels;
					_timerCels = 0;
					if (dlgMgrReadAttribute(runtime, this, element, "celcount", cels) && cels.getType() == DynamicValueTypes::kInteger)
						_timerCels = cels.getInt();
				}

				int32 score = readVar(element, "gamescore");
				if (score > 0) {
					// gamescore 1 starts a new game, from 10 on it is the score carried over
					_scoreElement = element->getSelfReference();
					_score = (score < 10) ? 0 : score;
					setInt(runtime, _scoreElement, "text", _score);
				}
			} else if (speed < 1) {
				// Predators and distractions stay put, other bugs are parked off screen
				if (type == kBugTypePredator && rect.top < kBugEatBugScreenHeight) {
					Still &pred = _preds[_numPreds];
					pred.element = element->getSelfReference();
					pred.body = rect;

					// The AttackRect is the predator's first child
					if (!element->getChildren().empty()) {
						Structural *mouth = element->getChildren()[0].get();
						if (mouth->isElement() && static_cast<Element *>(mouth)->isVisual()) {
							pred.mouth = static_cast<VisualElement *>(mouth)->getRelativeRect();
							pred.mouth.translate(rect.left, rect.top);
							pred.mouthCenter = Common::Point(pred.mouth.left + pred.mouth.width() / 2, pred.mouth.top + pred.mouth.height() / 2);
						}
					}

					if (++_numPreds >= kMaxStill) {
						warning("BugEatBug: Too many predator bugs located.");
						break;
					}
				} else if (type == kBugTypeDistraction && rect.top < kBugEatBugScreenHeight) {
					Still &distraction = _distractions[_numDistractions];
					distraction.element = element->getSelfReference();
					distraction.body = rect;

					if (++_numDistractions >= kMaxStill) {
						warning("BugEatBug: Too many distraction bugs located.");
						break;
					}
				} else {
					placeTopLeft(runtime, element->getSelfReference(), kBugEatBugParked, kBugEatBugParked);
				}
			} else {
				// Flyers start in a random direction, faster ones by their bugspeed
				Flyer &flyer = _flyers[_numFlyers];
				flyer.element = element->getSelfReference();
				flyer.type = type;

				// As VisualElement::getCenterPosition()
				flyer.x = (rect.left + rect.right) / 2;
				flyer.y = (rect.top + rect.bottom) / 2;

				if (runtime->getRandom()->getRandomNumber(0x7fff) < 0x3ffc) {
					flyer.dx = speed + 3;
					flyer.cel = 4;
				} else {
					flyer.dx = -3 - speed;
					flyer.cel = 2;
				}

				if (type == kBugTypeFly || type == kBugTypeMosquito)
					_numEdible++;

				if (++_numFlyers >= kMaxFlyers) {
					warning("BugEatBug: Too many flying bugs located.");
					break;
				}
			}
		}
	}

	// Each flyer drifts to the height of a random predator's mouth
	for (int i = 0; i < _numFlyers; i++)
		_flyers[i].pred = (_numPreds > 0) ? runtime->getRandom()->getRandomNumberRng(0, _numPreds - 1) : 0;

	_paused = false;

	debug(2, "BugEatBug: Started with %i flyers (%i edible), %i predators, %i distractions, %i timer ticks", static_cast<int>(_numFlyers),
		static_cast<int>(_numEdible), static_cast<int>(_numPreds), static_cast<int>(_numDistractions), static_cast<int>(_timerTotal));
}

void BugEatBugModifier::click(Runtime *runtime) {
	if (_paused) {
		sendMessage(runtime, "PausedClick");
		return;
	}

	const Common::Point mouse = runtime->getCachedMousePosition();

	for (int j = 0; j < _numPreds; j++) {
		Still &pred = _preds[j];
		if (mouse.x <= pred.body.left || mouse.x >= pred.body.right || mouse.y <= pred.body.top || mouse.y >= pred.body.bottom || pred.cooldown >= 1)
			continue;

		pred.cel = 7;
		pred.animEnd = 9;
		pred.cooldown = kBugEatBugCooldown;

		// One flyer per strike
		bool caught = false;
		for (int i = 0; i < _numFlyers; i++) {
			Flyer &flyer = _flyers[i];
			if (flyer.x <= pred.mouth.left || flyer.x >= pred.mouth.right || flyer.y <= pred.mouth.top || flyer.y >= pred.mouth.bottom || flyer.dx == 0)
				continue;

			// Removed on the next tick
			flyer.x = pred.mouthCenter.x;
			flyer.y = pred.mouthCenter.y;
			flyer.dx = 0;

			if (flyer.type == kBugTypeFly)
				_score += 5;
			else if (flyer.type == kBugTypeMosquito)
				_score += 10;
			else if (flyer.type == kBugTypeChance)
				_score += 25;
			setInt(runtime, _scoreElement, "text", _score);

			int sfx = (flyer.type == kBugTypePoison) ? 5 : runtime->getRandom()->getRandomNumberRng(0, 3);
			if (_numEdible > 0 || flyer.type > kBugTypeMosquito)
				sendMessage(runtime, kBugEatBugSfx[sfx]);

			// Poison stuns the predator for 5 seconds
			if (flyer.type == kBugTypePoison)
				pred.cooldown = _tickRate * 5;

			debug(2, "BugEatBug: Predator %i ate flyer %i (type %i), score %i", j, i, static_cast<int>(flyer.type), static_cast<int>(_score));
			caught = true;
			break;
		}

		if (!caught)
			sendMessage(runtime, "PlaySFXMiss");
	}

	for (int k = 0; k < _numDistractions; k++) {
		Still &distraction = _distractions[k];
		if (mouse.x <= distraction.body.left || mouse.x >= distraction.body.right || mouse.y <= distraction.body.top || mouse.y >= distraction.body.bottom)
			continue;

		distraction.cel = 2;
		distraction.animEnd = 5;
		sendMessage(runtime, "PlaySFXDistract");
	}
}

// Plays cels from cel up to animEnd - 1, then shows cel 1
void BugEatBugModifier::animate(Runtime *runtime, Still &still) {
	if (still.animEnd == 1)
		return;

	setInt(runtime, still.element, "cel", still.cel);
	if (still.animEnd == 0) {
		still.animEnd = 1;
	} else if (++still.cel >= still.animEnd) {
		still.cel = 1;
		still.animEnd = 0;
	}
}

void BugEatBugModifier::tick(Runtime *runtime) {
	_scheduledEvent.reset();
	if (!_running)
		return;

	_scheduledEvent = runtime->getScheduler().scheduleMethod<BugEatBugModifier, &BugEatBugModifier::tick>(runtime->getPlayTime() + bugEatBugPeriod(_tickRate), this);

	if (_paused)
		return;

	// Flyers are drawn where they were on the last tick. Cels 1-3 fly right, 4-6 fly left.
	static const int32 kNextCel[7] = {0, 2, 3, 1, 5, 6, 4};
	for (int i = 0; i < _numFlyers; i++) {
		Flyer &flyer = _flyers[i];
		if (flyer.dead)
			continue;

		int32 shownCel = flyer.cel;
		if (flyer.cel >= 1 && flyer.cel <= 6)
			flyer.cel = kNextCel[flyer.cel];
		setInt(runtime, flyer.element, "cel", shownCel);
		placeCentered(runtime, flyer.element, flyer.x, flyer.y);

		if (flyer.dx == 0) {
			// Eaten since the last tick
			flyer.x = flyer.y = kBugEatBugParked;
			placeTopLeft(runtime, flyer.element, kBugEatBugParked, kBugEatBugParked);
			flyer.dead = true;

			if (flyer.type == kBugTypeFly || flyer.type == kBugTypeMosquito)
				_numEdible--;
			else if (flyer.type == kBugTypeChance)
				sendMessage(runtime, "ExtraChance");

			if (_numEdible < 1)
				sendMessage(runtime, "YouWin");
		} else {
			flyer.x -= flyer.dx;
			flyer.y += runtime->getRandom()->getRandomNumberRng(-3, 3);

			const Common::Rect &mouth = _preds[flyer.pred].mouth;
			if (flyer.y < mouth.top)
				flyer.y++;
			else if (flyer.y > mouth.bottom)
				flyer.y--;

			if (flyer.x < 0) {
				flyer.dx = -ABS(flyer.dx);
				flyer.cel = 1;
			}
			if (flyer.x > kBugEatBugScreenWidth) {
				flyer.dx = ABS(flyer.dx);
				flyer.cel = 4;
			}
		}
	}

	for (int j = 0; j < _numPreds; j++) {
		animate(runtime, _preds[j]);
		_preds[j].cooldown--;
	}

	for (int k = 0; k < _numDistractions; k++)
		animate(runtime, _distractions[k]);

	// The timer bar fills up as the time runs out
	if (_timerTotal > 0) {
		int32 shownCel = _timerCels - (_timerCels * _timerLeft) / _timerTotal;
		_timerLeft--;
		setInt(runtime, _timerElement, "cel", shownCel);

		if (_timerLeft == _timerWarn)
			sendMessage(runtime, "PlaySFXTimer");
		if (_timerLeft == 0)
			sendMessage(runtime, "GameOver");
	}
}

void BugEatBugModifier::placeCentered(Runtime *runtime, const Common::WeakPtr<RuntimeObject> &element, int32 x, int32 y) {
	Common::SharedPtr<RuntimeObject> obj = element.lock();
	if (!obj || !obj->isStructural())
		return;

	Structural *structural = static_cast<Structural *>(obj.get());
	if (!structural->isElement() || !static_cast<Element *>(structural)->isVisual())
		return;

	const Common::Rect &rect = static_cast<VisualElement *>(structural)->getRelativeRect();
	placeTopLeft(runtime, element, x - rect.width() / 2, y - rect.height() / 2);
}

void BugEatBugModifier::placeTopLeft(Runtime *runtime, const Common::WeakPtr<RuntimeObject> &element, int32 x, int32 y) {
	Common::SharedPtr<RuntimeObject> obj = element.lock();
	if (!obj)
		return;

	DynamicValue pos;
	pos.setPoint(Common::Point(x, y));
	dlgMgrWriteAttribute(runtime, this, obj.get(), "position", pos);
}

void BugEatBugModifier::setInt(Runtime *runtime, const Common::WeakPtr<RuntimeObject> &element, const char *attrib, int32 value) {
	Common::SharedPtr<RuntimeObject> obj = element.lock();
	if (obj)
		dlgMgrSetInt(runtime, this, obj.get(), attrib, value);
}

void BugEatBugModifier::sendMessage(Runtime *runtime, const char *messageName) {
	uint32 messageID = 0;
	if (!runtime->getProject()->findAuthorMessageID(messageName, messageID))
		return;

	Structural *owner = findStructuralOwner();
	if (!owner)
		return;

	debug(2, "BugEatBug: Sending '%s'", messageName);
	Common::SharedPtr<MessageProperties> props(new MessageProperties(Event(EventIDs::kAuthorMessage, messageID), DynamicValue(), getSelfReference()));
	Common::SharedPtr<MessageDispatch> dispatch(new MessageDispatch(props, owner, true, true, false));
	runtime->queueMessage(dispatch);
}

Common::SharedPtr<Modifier> BugEatBugModifier::shallowClone() const {
	Common::SharedPtr<BugEatBugModifier> clone(new BugEatBugModifier(*this));
	clone->_scheduledEvent.reset();
	clone->_running = false;
	return clone;
}

const char *BugEatBugModifier::getDefaultName() const {
	return "BugEatBug";
}

// Children of the owner element
enum LanderChild {
	kLanderChildLander = 1,
	kLanderChildLevels,
	kLanderChildFuel,
	kLanderChildPadEasy,	// Then medium and hard
	kLanderChildCollision = 7,
	kLanderChildGoodies,
	kLanderChildArrows,
};

static const int32 kLanderPadHalfWidths[3] = {60, 48, 36};
static const int32 kLanderPadPoints[3] = {100, 200, 300};
static const int32 kLanderPadHalfHeight = 7;
static const int32 kLanderOffScreen = 666;
static const int32 kLanderHidden = 600;
static const double kLanderSideThrust = 0.2;
static const double kLanderMainThrust = 0.25;
static const double kLanderBrake = 0.05;
static const double kLanderMaxSpeed = 5.5;
static const double kLanderMaxFuel = 20.0;

LanderModifier::LanderModifier()
	: _levelsLoaded(false), _running(false), _paused(false), _levelReady(false), _level(0), _points(0), _frame(0), _gravity(0.1), _posX(0.0), _posY(0.0),
	  _vx(0.0), _vy(0.0), _landerX(0), _landerY(0), _fuel(0.0), _crashAnim(-1), _mouseHeld(false), _mouseMode(true), _thrusting(false), _keyUp(false),
	  _keyLeft(false), _keyRight(false) {
	memset(_items, 0, sizeof(_items));
}

LanderModifier::~LanderModifier() {
	stopGame();
}

bool LanderModifier::load(const PlugInModifierLoaderContext &context, const Data::KW::GameModifier &data) {
	return loadDlgMgrEvent(_startWhen, data.enableWhen) && loadDlgMgrEvent(_stopWhen, data.disableWhen);
}

// The levels are records in the plug-in's data section
bool LanderModifier::loadLevels() {
	if (_levelsLoaded)
		return !_levels.empty();

	_levelsLoaded = true;

	Common::File f;
	if (!f.open(Common::Path("mTrop_Resources/Lander.r32")) && !f.open(Common::Path("Lander.r32"))) {
		warning("Lander: Lander.r32 from the installed game is needed for the levels");
		return false;
	}

	Common::Array<byte> image;
	image.resize(f.size());
	f.read(&image[0], image.size());

	bool failed = false;
	auto readS16 = [&](uint32 va) -> int16 {
		uint32 offset = va - 0x10000000;
		if (offset + 2 > image.size()) {
			failed = true;
			return 0;
		}
		return static_cast<int16>(READ_LE_UINT16(&image[offset]));
	};
	auto readPoint = [&](uint32 va) -> Common::Point {
		return Common::Point(readS16(va), readS16(va + 4));
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

	Common::Array<Level> levels;
	levels.resize(kNumLevels + 1);
	for (uint l = 1; l <= kNumLevels; l++) {
		Level &level = levels[l];
		uint32 base = 0x10036050 + (l - 1) * 0x30a;

		level.start = readPoint(base);
		for (uint i = 0; i < kNumPads; i++)
			level.pads[i] = readPoint(base + 0x8 + i * 8);
		level.fuel = readDouble(base + 0x20);
		level.sideCost = readDouble(base + 0x28);
		level.upCost = readDouble(base + 0x30);

		int16 polyCount = readS16(base + 0x38);
		if (polyCount < 3 || polyCount > 75)
			failed = true;
		for (int16 i = 0; i < polyCount && !failed; i++)
			level.landscape.push_back(readPoint(base + 0x3a + i * 8));

		for (uint i = 0; i < kNumItems; i++) {
			ItemSpawn &item = level.items[i];
			item.pos = readPoint(base + 0x292 + i * 12);
			item.visibleTicks = readS16(base + 0x29a + i * 12);
			item.hiddenTicks = readS16(base + 0x29c + i * 12);
		}
	}

	// Level 1 starts at (450, 100) with 20 fuel and a 20 point landscape
	if (failed || levels[1].start != Common::Point(450, 100) || levels[1].fuel != 20.0 || levels[1].landscape.size() != 20) {
		warning("Lander: Lander.r32 isn't the expected version");
		return false;
	}

	_levels.swap(levels);
	return true;
}

bool LanderModifier::respondsToEvent(const Event &evt) const {
	return _startWhen.respondsTo(evt) || _stopWhen.respondsTo(evt) || evt.eventType == EventIDs::kParentDisabled
		|| evt.eventType == EventIDs::kMouseDown || evt.eventType == EventIDs::kMouseUp;
}

VThreadState LanderModifier::consumeMessage(Runtime *runtime, const Common::SharedPtr<MessageProperties> &msg) {
	const Event &evt = msg->getEvent();

	if (_startWhen.respondsTo(evt)) {
		setupLevel(runtime, 1);
		_paused = true;

		if (!_running) {
			_running = true;
			_keySignaller = runtime->getProject()->notifyOnKeyboardEvent(this);
			_scheduledEvent = runtime->getScheduler().scheduleMethod<LanderModifier, &LanderModifier::tick>(runtime->getPlayTime() + 83, this);
		}
		return kVThreadReturn;
	}

	if (_stopWhen.respondsTo(evt) || evt.eventType == EventIDs::kParentDisabled) {
		stopGame();
		return kVThreadReturn;
	}

	// While paused a click dismisses the dialog or level box instead
	if (evt.eventType == EventIDs::kMouseDown) {
		if (!_paused) {
			_mouseHeld = true;
			_mouseMode = true;
		} else {
			sendMessage(runtime, "PausedClick");
		}
	} else if (evt.eventType == EventIDs::kMouseUp) {
		_mouseHeld = false;
	}

	return kVThreadReturn;
}

void LanderModifier::onKeyboardEvent(Runtime *runtime, const KeyboardInputEvent &keyEvt) {
	Common::KeyCode keycode = keyEvt.getKeyState().keycode;
	bool *flag = nullptr;
	switch (keycode) {
	case Common::KEYCODE_UP:
	case Common::KEYCODE_KP8:
		flag = &_keyUp;
		break;
	case Common::KEYCODE_LEFT:
	case Common::KEYCODE_KP4:
		flag = &_keyLeft;
		break;
	case Common::KEYCODE_RIGHT:
	case Common::KEYCODE_KP6:
		flag = &_keyRight;
		break;
	default:
		break;
	}

	// Releasing down also cuts the main thruster, as in the original
	if (keyEvt.getKeyEventType() == Common::EVENT_KEYUP) {
		if (keycode == Common::KEYCODE_DOWN || keycode == Common::KEYCODE_KP2)
			_keyUp = false;
		else if (flag)
			*flag = false;
		return;
	}

	// Any key switches to the keys while playing; the flags are set even while paused
	if (!_paused) {
		_mouseMode = false;
		setCel(runtime, child(kLanderChildArrows), 1);
	}
	if (flag)
		*flag = true;
}

void LanderModifier::disable(Runtime *runtime) {
	stopGame();
}

void LanderModifier::stopGame() {
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

bool LanderModifier::readAttribute(MiniscriptThread *thread, DynamicValue &result, const Common::String &attrib) {
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

MiniscriptInstructionOutcome LanderModifier::writeRefAttribute(MiniscriptThread *thread, DynamicValueWriteProxy &result, const Common::String &attrib) {
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

MiniscriptInstructionOutcome LanderModifier::WriteProxy::write(MiniscriptThread *thread, const DynamicValue &assigned, void *objectRef, uintptr ptrOrOffset) {
	LanderModifier *self = static_cast<LanderModifier *>(objectRef);

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
		if (level >= 1 && level <= kNumLevels)
			self->setupLevel(thread->getRuntime(), level);
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

		// After a landing, crash or escape only the next level can unpause
		if (self->_levelReady || flag) {
			if (self->_paused != flag)
				debug(2, "Lander: %s level %d", flag ? "Paused" : "Unpaused", self->_level);
			self->_paused = flag;
		}
	}

	return kMiniscriptInstructionOutcomeContinue;
}

MiniscriptInstructionOutcome LanderModifier::WriteProxy::refAttrib(MiniscriptThread *thread, DynamicValueWriteProxy &proxy, void *objectRef, uintptr ptrOrOffset, const Common::String &attrib) {
	return kMiniscriptInstructionOutcomeFailed;
}

MiniscriptInstructionOutcome LanderModifier::WriteProxy::refAttribIndexed(MiniscriptThread *thread, DynamicValueWriteProxy &proxy, void *objectRef, uintptr ptrOrOffset, const Common::String &attrib, const DynamicValue &index) {
	return kMiniscriptInstructionOutcomeFailed;
}

Structural *LanderModifier::child(int index) const {
	Structural *owner = findStructuralOwner();
	if (!owner || index < 1 || index > static_cast<int>(owner->getChildren().size()))
		return nullptr;
	return owner->getChildren()[index - 1].get();
}

Structural *LanderModifier::grandchild(int index, int subIndex) const {
	Structural *container = child(index);
	if (!container || subIndex < 1 || subIndex > static_cast<int>(container->getChildren().size()))
		return nullptr;
	return container->getChildren()[subIndex - 1].get();
}

void LanderModifier::placeCentered(Runtime *runtime, Structural *element, int32 x, int32 y) {
	if (!element || !element->isElement() || !static_cast<Element *>(element)->isVisual())
		return;

	const Common::Rect &rect = static_cast<VisualElement *>(element)->getRelativeRect();
	placeTopLeft(runtime, element, x - rect.width() / 2, y - rect.height() / 2);
}

void LanderModifier::placeTopLeft(Runtime *runtime, Structural *element, int32 x, int32 y) {
	if (!element)
		return;

	DynamicValue pos;
	pos.setPoint(Common::Point(x, y));
	dlgMgrWriteAttribute(runtime, this, element, "position", pos);
}

void LanderModifier::setCel(Runtime *runtime, Structural *element, int32 cel) {
	dlgMgrSetInt(runtime, this, element, "cel", cel);
}

void LanderModifier::sendMessage(Runtime *runtime, const char *messageName) {
	uint32 messageID = 0;
	if (!runtime->getProject()->findAuthorMessageID(messageName, messageID))
		return;

	Structural *owner = findStructuralOwner();
	if (!owner)
		return;

	debug(2, "Lander: Sending '%s'", messageName);
	Common::SharedPtr<MessageProperties> props(new MessageProperties(Event(EventIDs::kAuthorMessage, messageID), DynamicValue(), getSelfReference()));
	Common::SharedPtr<MessageDispatch> dispatch(new MessageDispatch(props, owner, true, true, false));
	runtime->queueMessage(dispatch);
}

// Even-odd test, as the original's polygon region
bool LanderModifier::inLandscape(int32 x, int32 y) const {
	const Common::Array<Common::Point> &poly = _levels[_level].landscape;
	bool inside = false;
	for (uint i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
		const Common::Point &a = poly[i];
		const Common::Point &b = poly[j];
		if ((a.y > y) != (b.y > y)) {
			double crossX = a.x + static_cast<double>(y - a.y) * (b.x - a.x) / (b.y - a.y);
			if (x < crossX)
				inside = !inside;
		}
	}
	return inside;
}

void LanderModifier::setupLevel(Runtime *runtime, int32 levelNum) {
	if (!loadLevels() || levelNum < 1 || levelNum > kNumLevels)
		return;

	const Level &level = _levels[levelNum];
	_level = levelNum;
	_points = 0;
	_frame = 0;

	// Gravity is weaker in the first three levels and stronger in the last tier
	_gravity = 0.1;
	if (levelNum < 4)
		_gravity -= 0.02;
	else if (levelNum > 10)
		_gravity += 0.01;

	_crashAnim = -1;
	_keyUp = _keyLeft = _keyRight = false;
	_thrusting = false;

	if (!findStructuralOwner())
		return;

	_vx = _vy = 0.0;
	_posX = level.start.x;
	_posY = level.start.y;
	_landerX = level.start.x;
	_landerY = level.start.y;
	placeCentered(runtime, child(kLanderChildLander), _landerX, _landerY);
	setCel(runtime, child(kLanderChildLander), 1);

	for (int i = 1; i <= kNumLevels; i++)
		placeTopLeft(runtime, grandchild(kLanderChildLevels, i), 0, (i == levelNum) ? 0 : kLanderHidden);

	_fuel = level.fuel;
	setCel(runtime, child(kLanderChildFuel), static_cast<int32>(_fuel + 1.0));

	for (int i = 0; i < kNumPads; i++) {
		const Common::Point &pad = level.pads[i];
		placeCentered(runtime, child(kLanderChildPadEasy + i), pad.x, pad.y);
		_padRects[i] = Common::Rect(pad.x - kLanderPadHalfWidths[i], pad.y - kLanderPadHalfHeight, pad.x + kLanderPadHalfWidths[i], pad.y + kLanderPadHalfHeight);
	}

	placeCentered(runtime, child(kLanderChildCollision), kLanderOffScreen, kLanderOffScreen);
	setCel(runtime, child(kLanderChildCollision), 8);

	// Items start at a random point of their visible time, shown on odd counts
	for (int i = 0; i < kNumItems; i++) {
		const ItemSpawn &spawn = level.items[i];
		Item &item = _items[i];
		Structural *element = grandchild(kLanderChildGoodies, i + 1);
		if (spawn.visibleTicks < 1) {
			item.active = false;
			placeCentered(runtime, element, item.x, item.y + kLanderHidden);
			continue;
		}

		item.x = spawn.pos.x;
		item.y = spawn.pos.y;
		item.timer = runtime->getRandom()->getRandomNumberRng(1, spawn.visibleTicks);
		item.visible = ((item.timer & 1) != 0);
		item.active = true;
		placeCentered(runtime, element, item.x, item.visible ? item.y : item.y + kLanderHidden);
	}

	placeCentered(runtime, child(kLanderChildArrows), kLanderOffScreen, kLanderOffScreen);

	_levelReady = true;
}

void LanderModifier::tick(Runtime *runtime) {
	_scheduledEvent.reset();
	if (!_running)
		return;

	_scheduledEvent = runtime->getScheduler().scheduleMethod<LanderModifier, &LanderModifier::tick>(runtime->getPlayTime() + 83, this);

	// The collision animation plays while paused, then the chance is lost
	if (_paused) {
		if (_crashAnim >= 1) {
			setCel(runtime, child(kLanderChildCollision), _crashAnim);
			_crashAnim--;
		} else if (_crashAnim != -1) {
			sendMessage(runtime, "GameOver");
			_crashAnim = -1;
		}
		return;
	}

	if (_levels.empty())
		return;

	_frame++;
	bool right = false;
	bool left = false;
	bool up = false;
	bool wasThrusting = _thrusting;
	_thrusting = false;

	if (_mouseMode)
		steer(runtime, right, left, up);

	if (_fuel > 0.0)
		thrust(runtime, right, left, up);

	_vy += _gravity;
	if (_vx > 2.5)
		_vx -= 0.07;
	else if (_vx < -2.5)
		_vx += 0.07;
	_posX += _vx;
	_posY += _vy;
	_landerX = static_cast<int32>(_posX);
	_landerY = static_cast<int32>(_posY);
	placeCentered(runtime, child(kLanderChildLander), _landerX, _landerY);
	placeCentered(runtime, child(kLanderChildArrows), _landerX, _landerY);

	// Cels in pairs flickering every tick: 1 main jet, 3 main and pushing
	// right, 5 main and pushing left, 7 idle, 21 pushing right, 23 pushing left
	int32 cel = 7;
	if (_fuel > 0.0) {
		if (up)
			cel = right ? 3 : left ? 5 : 1;
		else
			cel = right ? 21 : left ? 23 : 7;
	}
	setCel(runtime, child(kLanderChildLander), cel + (_frame & 1));

	if (_thrusting != wasThrusting)
		sendMessage(runtime, _thrusting ? "ThrustSFXOn" : "ThrustSFXOff");

	tickItems(runtime);

	// The original also sends GameOver for leaving the screen in the same tick
	// as a crash, costing two chances; only one is sent here
	if (checkLanding(runtime))
		return;

	if (_landerX < 6 || _landerX > 634 || _landerY < 6) {
		_paused = true;
		_levelReady = false;
		sendMessage(runtime, "GameOver");
		sendMessage(runtime, "ThrustSFXOff");
	}
}

// Thrusts towards the mouse while the button is held: straight sideways or up
// within a 0.6 slope, diagonally otherwise. The arrow over the lander shows
// the direction, one cel lower while the button is held.
void LanderModifier::steer(Runtime *runtime, bool &right, bool &left, bool &up) {
	const Common::Point &mouse = runtime->getCachedMousePosition();
	double dx = mouse.x - _landerX;
	double dy = mouse.y - _landerY;

	// Divisions by zero count as steep, as in the original's floating point
	int32 arrow;
	if (dx > 0.0) {
		right = (dy == 0.0 || dx / fabs(dy) > 0.6);
		arrow = 11;
	} else {
		left = (dx < 0.0 && (dy == 0.0 || -dx / fabs(dy) > 0.6));
		arrow = 3;
	}
	if (dy < 0.0 && (dx == 0.0 || fabs(dy / dx) > 0.6)) {
		up = true;
		arrow = right ? 9 : left ? 5 : 7;
	}

	if (_mouseHeld)
		arrow--;
	else
		right = left = up = false;
	setCel(runtime, child(kLanderChildArrows), arrow);
}

void LanderModifier::thrust(Runtime *runtime, bool &right, bool &left, bool &up) {
	const Level &level = _levels[_level];

	if (_mouseHeld) {
		// The mouse brakes harder against a fast drift
		if (right) {
			_vx += kLanderSideThrust;
			if (_vx < -kLanderMaxSpeed)
				_vx += kLanderBrake;
			_fuel -= level.sideCost;
		} else if (left) {
			_vx -= kLanderSideThrust;
			if (_vx > kLanderMaxSpeed)
				_vx -= kLanderBrake;
			_fuel -= level.sideCost;
		}
		if (up) {
			_vy -= kLanderMainThrust;
			if (_vy > kLanderMaxSpeed)
				_vy -= kLanderBrake;
			_fuel -= level.upCost;
		}
		// Holding the button counts as thrusting even in a neutral direction
		_thrusting = true;
	} else {
		if (_keyLeft) {
			_vx -= kLanderSideThrust;
			_fuel -= level.sideCost;
			left = true;
			_thrusting = true;
		} else if (_keyRight) {
			_vx += kLanderSideThrust;
			_fuel -= level.sideCost;
			right = true;
			_thrusting = true;
		}
		if (_keyUp) {
			_vy -= kLanderMainThrust;
			_fuel -= level.upCost;
			up = true;
			_thrusting = true;
		}
	}

	setCel(runtime, child(kLanderChildFuel), (_fuel >= 0.0) ? static_cast<int32>(_fuel + 1.0) : 1);
	if (_fuel <= 0.0)
		sendMessage(runtime, "PlaySFX2");
}

// Items blink on and off on their own timers and are collected by touching
// them while shown
void LanderModifier::tickItems(Runtime *runtime) {
	const Level &level = _levels[_level];

	for (int i = 0; i < kNumItems; i++) {
		Item &item = _items[i];
		if (!item.active)
			continue;

		const ItemSpawn &spawn = level.items[i];
		Structural *element = grandchild(kLanderChildGoodies, i + 1);
		item.timer--;
		if (item.timer < 0) {
			item.visible = !item.visible;
			item.timer = item.visible ? spawn.visibleTicks : spawn.hiddenTicks;
			placeCentered(runtime, element, item.x, item.visible ? item.y : item.y + kLanderHidden);
			continue;
		}

		if (!item.visible || ABS(_landerX - item.x) >= 24 || _landerY - item.y >= 34 || item.y - _landerY >= 22)
			continue;

		item.active = false;
		item.timer = 0;
		placeCentered(runtime, element, item.x, item.y + kLanderHidden);
		if (i < 3) {
			_points += 100;
			sendMessage(runtime, "PlaySFX4");
			sendMessage(runtime, "UpdateScore");
		} else if (i < 6) {
			_points += 50;
			sendMessage(runtime, "PlaySFX3");
			sendMessage(runtime, "UpdateScore");
		} else if (i < 8) {
			_fuel = MIN(_fuel + 4.0, kLanderMaxFuel);
			sendMessage(runtime, "PlaySFX5");
		} else {
			sendMessage(runtime, "ExtraChance");
		}
	}
}

// Ends the level if the lander touched down on a pad or hit the terrain. A pad
// needs both feet on it and a combined speed under 5.5, and wins over terrain
// it overlaps.
bool LanderModifier::checkLanding(Runtime *runtime) {
	Common::Point top(_landerX, _landerY - 12);
	Common::Point leftFoot(_landerX - 14, _landerY + 4);
	Common::Point rightFoot(_landerX + 14, _landerY + 4);

	bool landed = false;
	bool crashed = false;
	double speed = fabs(_vy) + fabs(_vx);
	for (int i = 0; i < kNumPads; i++) {
		const Common::Rect &pad = _padRects[i];
		bool leftOn = (leftFoot.x > pad.left && leftFoot.x < pad.right && leftFoot.y > pad.top && leftFoot.y < pad.bottom);
		bool rightOn = (rightFoot.x > pad.left && rightFoot.x < pad.right && rightFoot.y > pad.top && rightFoot.y < pad.bottom);
		if (leftOn && rightOn) {
			if (speed >= kLanderMaxSpeed) {
				crashed = true;
			} else {
				landed = true;
				_points += kLanderPadPoints[i];
			}
			debug(2, "Lander: %s pad %d at (%d, %d), speed %.2f", landed ? "Landed on" : "Too fast for", i + 1, _landerX, _landerY, speed);
			break;
		}
	}

	if (landed) {
		_paused = true;
		_levelReady = false;
		sendMessage(runtime, "YouWin");
		sendMessage(runtime, "ThrustSFXOff");
		return true;
	}

	if (crashed || inLandscape(top.x, top.y) || inLandscape(leftFoot.x, leftFoot.y) || inLandscape(rightFoot.x, rightFoot.y)) {
		_paused = true;
		_levelReady = false;
		placeCentered(runtime, child(kLanderChildCollision), _landerX, _landerY);
		placeCentered(runtime, child(kLanderChildLander), kLanderOffScreen, kLanderOffScreen);
		placeCentered(runtime, child(kLanderChildArrows), kLanderOffScreen, kLanderOffScreen);
		_crashAnim = 8;
		debug(2, "Lander: Crashed at (%d, %d)", _landerX, _landerY);
		sendMessage(runtime, "PlaySFX1");
		sendMessage(runtime, "ThrustSFXOff");
		return true;
	}

	return false;
}

Common::SharedPtr<Modifier> LanderModifier::shallowClone() const {
	Common::SharedPtr<LanderModifier> clone(new LanderModifier(*this));
	clone->_scheduledEvent.reset();
	clone->_keySignaller.reset();
	clone->_running = false;
	clone->_levelReady = false;
	clone->_paused = false;
	clone->_crashAnim = -1;
	return clone;
}

const char *LanderModifier::getDefaultName() const {
	return "Lander";
}

// Lane centre lines from the bottom lane up, the level 1 timers of each lane's
// third geyser, and which of the 3 backgrounds each level shows
static const int16 kLLavaLaneY[5] = {340, 285, 230, 175, 120};
static const int16 kLLavaGeyserL1[5] = {90, 60, 130, 76, 110};
static const int16 kLLavaBackground[16] = {0, 1, 3, 2, 1, 3, 1, 2, 3, 1, 3, 2, 1, 3, 2, 3};

// Empty objects and riders sit at this x
static const double kLLavaEmpty = -300.0;
static const int32 kLLavaGeyser = 20;
static const int32 kLLavaNoType = 21;

// Objects that have a left and a right spot for the bus and riders
static bool llavaTwoSided(int32 type) {
	switch (type) {
	case 1:
	case 4:
	case 5:
	case 6:
	case 7:
	case 9:
	case 10:
	case 11:
	case 12:
	case 13:
	case 14:
	case 16:
	case 17:
		return true;
	default:
		return false;
	}
}

// The animated rafts (types 15-17) never carry riders other than fires,
// and these never carry fires either
static bool llavaNoFire(int32 type) {
	return type == 2 || type == 3 || (type >= 15 && type <= 17);
}

static bool llavaRaft(int32 type) {
	return type >= 15 && type <= 17;
}

LLavaModifier::Lane::Lane() : moving(false), speed(0.0), wrapLeft(0), wrapRight(0) {
}

LLavaModifier::Object::Object()
	: x(kLLavaEmpty), y(600.0), active(false), type(kLLavaNoType), sub(0), counter(0), hold(0), row(-1), sinkDir(false), delay(0) {
}

LLavaModifier::Rider::Rider() : x(kLLavaEmpty), y(600.0), active(false), sub(0), phase(1), cel(1), row(-1), type(kLLavaNoType) {
}

LLavaModifier::LLavaModifier()
	: _levelsLoaded(false), _running(false), _paused(false), _levelReady(false), _introDlg(false), _gameState(kStateNewGame), _countdown(24),
	  _level(1), _points(0), _levelBonus(0), _lives(5), _mult(1.0), _bonusChance(84), _fireChance(230), _maxBonus(2), _maxFires(3),
	  _dir(kDirNone), _cooldown(0), _busX(0.0), _busY(0.0), _busRow(6), _busCel(1), _busFrames(0), _busAnim(kAnimIdle), _dx(0.0), _dy(0.0),
	  _busMotion(0), _busPhase(0) {
	memset(_types, 0, sizeof(_types));
	memset(_motion, 0, sizeof(_motion));
	for (int16 &count : _riderCount)
		count = 0;
	for (bool &scored : _rowScored)
		scored = false;
	for (Ripple &ripple : _ripples) {
		ripple.child = 0;
		ripple.x = kLLavaEmpty;
		ripple.y = 600.0;
		ripple.cel = 1;
	}

	_riders[kRiderBonus].resize(4);
	_riders[kRiderFire].resize(12);
	_riders[kRiderXLife].resize(3);
}

LLavaModifier::~LLavaModifier() {
	stop();
}

bool LLavaModifier::load(const PlugInModifierLoaderContext &context, const Data::KW::GameModifier &data) {
	return loadDlgMgrEvent(_startWhen, data.enableWhen) && loadDlgMgrEvent(_stopWhen, data.disableWhen);
}

// The levels, object extents and bob patterns are tables in the plug-in's data section
bool LLavaModifier::loadLevels() {
	if (_levelsLoaded)
		return !_levels.empty();

	_levelsLoaded = true;

	Common::File f;
	if (!f.open(Common::Path("mTrop_Resources/LLava.r32")) && !f.open(Common::Path("LLava.r32"))) {
		warning("LLava: LLava.r32 from the installed game is needed for the levels");
		return false;
	}

	Common::Array<byte> image;
	image.resize(f.size());
	f.read(&image[0], image.size());

	bool failed = false;
	auto readS16 = [&](uint32 va) -> int16 {
		uint32 offset = va - 0x10000000;
		if (offset + 2 > image.size()) {
			failed = true;
			return 0;
		}
		return static_cast<int16>(READ_LE_UINT16(&image[offset]));
	};
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

	for (int t = 0; t < kNumTypes; t++) {
		for (int i = 0; i < 4; i++)
			_types[t].box[i] = readS16(0x1000e0c0 + t * 8 + i * 2);
		for (int set = 0; set < 2; set++) {
			for (int i = 0; i < 2; i++) {
				_types[t].land[set][i] = readS16(0x1000e168 + t * 8 + set * 4 + i * 2);
				_types[t].ride[set][i] = readS16(0x1000e210 + t * 8 + set * 4 + i * 2);
			}
		}
	}

	for (int sub = 1; sub <= 4; sub++) {
		for (int phase = 0; phase < 12; phase++)
			_motion[sub][phase] = readDouble(0x1000e2c0 + (sub - 1) * 0xc0 + phase * 16);
	}

	Common::Array<Level> levels;
	levels.resize(16);
	for (uint l = 1; l <= 15; l++) {
		Level &level = levels[l];
		uint32 base = 0x1000d31e + l * 0x12d2;

		level.busStart = Common::Point(readS32(base), readS32(base + 4));
		for (int r = 0; r < kNumLanes; r++) {
			uint32 lane = base + 8 + r * 0x12;
			uint32 offset = lane - 0x10000000;
			level.lanes[r].moving = (offset < image.size() && image[offset] != 0);
			level.lanes[r].speed = readDouble(lane + 2);
			level.lanes[r].wrapLeft = readS32(lane + 10);
			level.lanes[r].wrapRight = readS32(lane + 14);

			for (int i = 0; i < kNumSlots; i++) {
				uint32 slot = base + 0x62 + (r * kNumSlots + i) * 16;
				level.slots[r][i].x = readDouble(slot);
				level.slots[r][i].type = readS32(slot + 8);
				level.slots[r][i].sub = readS32(slot + 12);
				if (level.slots[r][i].type < 0 || level.slots[r][i].type >= kNumTypes)
					failed = true;
			}
		}

		for (int i = 0; i < 20; i++) {
			level.rippleX[i] = readDouble(base + 0x1192 + i * 16);
			level.rippleY[i] = readDouble(base + 0x1192 + i * 16 + 8);
		}
	}

	// Level 1 starts at (320, 65) with the bottom lane drifting left at 2, and
	// the smallest stone is 21 wide on each side
	if (failed || levels[1].busStart != Common::Point(320, 65) || levels[1].lanes[0].speed != -2.0 || levels[1].lanes[0].wrapLeft != -44
		|| levels[1].lanes[0].wrapRight != 684 || _types[0].box[0] != 21 || readS16(0x1000e5b8) != kLLavaLaneY[0]) {
		warning("LLava: LLava.r32 isn't the expected version");
		return false;
	}

	_levels.swap(levels);
	return true;
}

bool LLavaModifier::respondsToEvent(const Event &evt) const {
	return _startWhen.respondsTo(evt) || _stopWhen.respondsTo(evt) || evt.eventType == EventIDs::kParentDisabled || evt.eventType == EventIDs::kMouseDown;
}

VThreadState LLavaModifier::consumeMessage(Runtime *runtime, const Common::SharedPtr<MessageProperties> &msg) {
	const Event &evt = msg->getEvent();

	if (_startWhen.respondsTo(evt)) {
		_introDlg = true;
		_gameState = kStateNewGame;
		setupLevel(runtime, 1);

		if (!_running) {
			_running = true;
			_keySignaller = runtime->getProject()->notifyOnKeyboardEvent(this);
			_scheduledEvent = runtime->getScheduler().scheduleMethod<LLavaModifier, &LLavaModifier::tick>(runtime->getPlayTime() + 83, this);
		}
	} else if (_stopWhen.respondsTo(evt) || evt.eventType == EventIDs::kParentDisabled) {
		stop();
	} else if (evt.eventType == EventIDs::kMouseDown) {
		clickDirection(runtime);
	}

	return kVThreadReturn;
}

void LLavaModifier::onKeyboardEvent(Runtime *runtime, const KeyboardInputEvent &keyEvt) {
	if (keyEvt.getKeyEventType() != Common::EVENT_KEYDOWN)
		return;

	// Takes effect at the next chance, even while paused
	switch (keyEvt.getKeyState().keycode) {
	case Common::KEYCODE_UP:
		_dir = kDirUp;
		break;
	case Common::KEYCODE_DOWN:
		_dir = kDirDown;
		break;
	case Common::KEYCODE_LEFT:
		_dir = kDirLeft;
		break;
	case Common::KEYCODE_RIGHT:
		_dir = kDirRight;
		break;
	default:
		break;
	}
}

void LLavaModifier::disable(Runtime *runtime) {
	stop();
}

void LLavaModifier::stop() {
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

bool LLavaModifier::readAttribute(MiniscriptThread *thread, DynamicValue &result, const Common::String &attrib) {
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

MiniscriptInstructionOutcome LLavaModifier::writeRefAttribute(MiniscriptThread *thread, DynamicValueWriteProxy &result, const Common::String &attrib) {
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

MiniscriptInstructionOutcome LLavaModifier::WriteProxy::write(MiniscriptThread *thread, const DynamicValue &assigned, void *objectRef, uintptr ptrOrOffset) {
	LLavaModifier *self = static_cast<LLavaModifier *>(objectRef);

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

		// A full new game at that level
		if (level >= 1 && level < 16) {
			self->_gameState = kStateNewGame;
			self->setupLevel(thread->getRuntime(), level);
		}
		return kMiniscriptInstructionOutcomeContinue;
	}

	bool flag = false;
	if (value.getType() == DynamicValueTypes::kBoolean)
		flag = value.getBool();
	else if (value.getType() == DynamicValueTypes::kInteger)
		flag = (value.getInt() != 0);
	else if (value.getType() == DynamicValueTypes::kFloat)
		flag = (value.getFloat() != 0.0);
	else
		return kMiniscriptInstructionOutcomeFailed;

	if (ptrOrOffset == kAttribPaused) {
		// Unpausing only takes effect once a level is set up
		if (self->_levelReady || flag) {
			self->_paused = flag;
			if (self->_gameState != kStateNewGame && self->_gameState != kStateNewLevel)
				self->_gameState = kStatePlaying;
		}
	} else if (ptrOrOffset == kAttribIntroDlg) {
		self->_introDlg = flag;
	}

	return kMiniscriptInstructionOutcomeContinue;
}

MiniscriptInstructionOutcome LLavaModifier::WriteProxy::refAttrib(MiniscriptThread *thread, DynamicValueWriteProxy &proxy, void *objectRef, uintptr ptrOrOffset, const Common::String &attrib) {
	return kMiniscriptInstructionOutcomeFailed;
}

MiniscriptInstructionOutcome LLavaModifier::WriteProxy::refAttribIndexed(MiniscriptThread *thread, DynamicValueWriteProxy &proxy, void *objectRef, uintptr ptrOrOffset, const Common::String &attrib, const DynamicValue &index) {
	return kMiniscriptInstructionOutcomeFailed;
}

// Children of the owner element: 1 bus, 2-6 lanes from the bottom up, 7 banner and
// badge, 8 backgrounds, 9 bonus critters, 10 ripples, 11 fires, 12 chances, 13 extra lives
Structural *LLavaModifier::child(int index) const {
	Structural *owner = findStructuralOwner();
	if (!owner || index < 1 || index > static_cast<int>(owner->getChildren().size()))
		return nullptr;
	return owner->getChildren()[index - 1].get();
}

Structural *LLavaModifier::grandchild(int index, int subIndex) const {
	Structural *container = child(index);
	if (!container || subIndex < 1 || subIndex > static_cast<int>(container->getChildren().size()))
		return nullptr;
	return container->getChildren()[subIndex - 1].get();
}

Structural *LLavaModifier::riderElement(RiderKind kind, int index) const {
	static const int kContainers[kRiderKindCount] = {9, 11, 13};
	return grandchild(kContainers[kind], index + 1);
}

void LLavaModifier::place(Runtime *runtime, Structural *element, double x, double y) {
	if (!element || !element->isElement() || !static_cast<Element *>(element)->isVisual())
		return;

	// Placed centered on the point, which is truncated
	const Common::Rect &rect = static_cast<VisualElement *>(element)->getRelativeRect();
	placeTopLeft(runtime, element, static_cast<int32>(x) - rect.width() / 2, static_cast<int32>(y) - rect.height() / 2);
}

void LLavaModifier::placeTopLeft(Runtime *runtime, Structural *element, int32 x, int32 y) {
	if (!element)
		return;

	DynamicValue pos;
	pos.setPoint(Common::Point(x, y));
	dlgMgrWriteAttribute(runtime, this, element, "position", pos);
}

void LLavaModifier::hide(Runtime *runtime, Structural *element) {
	place(runtime, element, -100.0, 600.0);
}

void LLavaModifier::setCel(Runtime *runtime, Structural *element, int32 cel) {
	dlgMgrSetInt(runtime, this, element, "cel", cel);
}

void LLavaModifier::sendMessage(Runtime *runtime, const char *messageName) {
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

int32 LLavaModifier::randomRange(Runtime *runtime, int32 min, int32 max) const {
	return runtime->getRandom()->getRandomNumberRng(min, max);
}

void LLavaModifier::setChances(Runtime *runtime, int32 cel) {
	setCel(runtime, child(12), cel);
}

void LLavaModifier::setupLevel(Runtime *runtime, int32 levelNum) {
	if (!loadLevels() || levelNum < 1)
		return;

	const Level &level = _levels[MIN<int32>(levelNum, 15)];
	int bg = kLLavaBackground[MIN<int32>(levelNum, 15)];

	debug(2, "LLava: Level %i", static_cast<int>(levelNum));

	_levelReady = false;
	_paused = false;
	_level = levelNum;
	_busAnim = kAnimIdle;
	_countdown = 24;
	_dir = kDirNone;
	_dx = 0.0;
	_dy = 0.0;
	for (int16 &count : _riderCount)
		count = 0;
	_levelBonus = 0;

	if (_gameState == kStateNewGame) {
		_points = 0;
		sendMessage(runtime, "UpdateScore");

		if (levelNum < 6) {
			_mult = 1.0;
			_lives = 5;
			_bonusChance = 84;
			_fireChance = 230;
			_maxBonus = 2;
			_maxFires = 3;
		} else if (levelNum <= 10) {
			_mult = 1.5;
			_lives = 4;
			_bonusChance = 134;
			_fireChance = 415;
			_maxBonus = 3;
			_maxFires = 8;
		} else {
			_mult = 2.0;
			_lives = 3;
			_bonusChance = 238;
			_fireChance = 580;
			_maxBonus = 4;
			_maxFires = 12;
		}
	}

	Structural *bus = child(1);
	_busCel = 1;
	_busFrames = 0;
	_cooldown = 0;
	_busMotion = 0;
	_busPhase = 0;
	_busX = level.busStart.x;
	_busY = level.busStart.y;
	_busRow = 6;
	place(runtime, bus, _busX, _busY);
	setCel(runtime, bus, 1);
	dlgMgrSetInt(runtime, this, bus, "layer", 1200);

	for (uint i = 0; i < _riders[kRiderFire].size(); i++) {
		Structural *fire = riderElement(kRiderFire, i);
		_riders[kRiderFire][i] = Rider();
		hide(runtime, fire);
		dlgMgrSetBool(runtime, this, fire, "paused", true);
		setCel(runtime, fire, 1);
	}

	for (int r = 0; r < kNumLanes; r++) {
		_lanes[r] = level.lanes[r];
		_rowScored[r + 1] = false;
		int geysers = 0;

		for (int i = 0; i < kNumSlots; i++) {
			Structural *element = grandchild(r + 2, i + 1);
			Object &obj = _objects[r][i];
			obj.x = level.slots[r][i].x;
			obj.type = level.slots[r][i].type;
			obj.sub = level.slots[r][i].sub;
			obj.y = kLLavaLaneY[r];
			obj.row = r;
			obj.hold = 1;

			// A few objects on the moving lanes are missing each time
			if (_lanes[r].moving && _lanes[r].speed != 0.0 && randomRange(runtime, 1, 1000) < 18) {
				obj.type = kLLavaNoType;
				obj.x = kLLavaEmpty;
				obj.y = 600.0;
				obj.sub = 0;
				obj.row = -1;
			}

			obj.active = true;
			obj.sinkDir = true;

			if (obj.sub == 5)
				obj.counter = randomRange(runtime, 24, 120);
			else if (obj.sub != 0)
				obj.counter = randomRange(runtime, 1, 12);

			if (llavaRaft(obj.type)) {
				dlgMgrSetBool(runtime, this, element, "paused", true);
				setCel(runtime, element, 1);
			} else if (obj.type == kLLavaGeyser) {
				geysers++;
				dlgMgrSetBool(runtime, this, element, "paused", true);
				setCel(runtime, element, 1);
				obj.counter = randomRange(runtime, 70, 420);
				if (levelNum == 1 && geysers == 3)
					obj.counter = kLLavaGeyserL1[r];
			}

			if (obj.x <= -70.0 || obj.x >= 710.0) {
				hide(runtime, element);
			} else if (obj.type == kLLavaGeyser) {
				place(runtime, element, obj.x, obj.y - 14.0);
			} else {
				place(runtime, element, obj.x, obj.y);

				// Some objects start out burning
				if (randomRange(runtime, 0, 1000) < _fireChance * 2.0 / 3.0 && _riderCount[kRiderFire] < _maxFires && !llavaNoFire(obj.type)) {
					for (uint f = 0; f < _riders[kRiderFire].size(); f++) {
						if (_riders[kRiderFire][f].x != kLLavaEmpty)
							continue;
						spawnRider(runtime, kRiderFire, f, obj);
						Rider &fire = _riders[kRiderFire][f];
						fire.cel = randomRange(runtime, 1, 13);
						setCel(runtime, riderElement(kRiderFire, f), fire.cel);
						place(runtime, riderElement(kRiderFire, f), fire.x, fire.y - 8.0);
						break;
					}
				}
			}
		}
	}

	placeTopLeft(runtime, grandchild(7, 1), 151, 160);
	setCel(runtime, grandchild(7, 2), levelNum % 5);
	placeTopLeft(runtime, grandchild(7, 2), 391, 198);

	for (int k = 1; k <= 3; k++) {
		if (k == bg)
			placeTopLeft(runtime, grandchild(8, k), 0, 0);
		else
			placeTopLeft(runtime, grandchild(8, k), -100, 600);
	}

	for (uint i = 0; i < _riders[kRiderBonus].size(); i++) {
		_riders[kRiderBonus][i] = Rider();
		hide(runtime, riderElement(kRiderBonus, i));
	}

	// Each background has its own set of 10 ripples
	Structural *ripples = child(10);
	if (ripples) {
		for (const Common::SharedPtr<Structural> &ripple : ripples->getChildren())
			hide(runtime, ripple.get());
	}
	int firstRipple = (bg == 3) ? 11 : 1;
	for (int k = 0; k < kNumRipples; k++) {
		Ripple &ripple = _ripples[k];
		ripple.child = firstRipple + k;
		ripple.x = level.rippleX[ripple.child - 1];
		ripple.y = level.rippleY[ripple.child - 1];
		ripple.cel = randomRange(runtime, 1, 5);

		Structural *element = grandchild(10, ripple.child);
		dlgMgrSetBool(runtime, this, element, "paused", true);
		setCel(runtime, element, ripple.cel);
		if (ripple.x != kLLavaEmpty)
			place(runtime, element, ripple.x, ripple.y);
	}

	if (_gameState == kStateNewGame) {
		dlgMgrSetBool(runtime, this, child(12), "paused", true);
		setChances(runtime, _lives + 1);
	}

	for (uint i = 0; i < _riders[kRiderXLife].size(); i++) {
		_riders[kRiderXLife][i] = Rider();
		hide(runtime, riderElement(kRiderXLife, i));
	}

	_levelReady = true;
}

// The banner shows for 2 seconds or until a click
void LLavaModifier::startPlay(Runtime *runtime) {
	_gameState = kStatePlaying;
	place(runtime, grandchild(7, 1), -1.0, 600.0);
	place(runtime, grandchild(7, 2), -1.0, 600.0);
	if (_lives >= 1)
		setChances(runtime, _lives);
}

// Clicks pick a hop direction relative to the bus
void LLavaModifier::clickDirection(Runtime *runtime) {
	if (!_paused && (_gameState == kStateNewGame || _gameState == kStateNewLevel)) {
		startPlay(runtime);
		return;
	}

	const Common::Point &mouse = runtime->getCachedMousePosition();
	if (mouse.x - 28 < _busX && _busX < mouse.x + 28 && mouse.y - 23 < _busY && _busY < mouse.y + 23)
		_dir = kDirDown;
	else if (mouse.y > _busY + 23.0)
		_dir = kDirDown;
	else if (mouse.y < _busY - 23.0)
		_dir = kDirUp;
	else if (mouse.x < _busX - 14.0)
		_dir = kDirLeft;
	else if (mouse.x > _busX + 14.0)
		_dir = kDirRight;
}

void LLavaModifier::tick(Runtime *runtime) {
	_scheduledEvent.reset();
	if (!_running)
		return;

	_scheduledEvent = runtime->getScheduler().scheduleMethod<LLavaModifier, &LLavaModifier::tick>(runtime->getPlayTime() + 83, this);

	if (_paused || !_levelReady)
		return;

	if (_gameState == kStateNewGame || _gameState == kStateNewLevel) {
		if (_countdown > 0)
			_countdown--;
		else
			startPlay(runtime);
		return;
	}

	if (_gameState != kStatePlaying)
		return;

	// The game waits while the mouse is over the bottom bar or outside
	const Common::Point &mouse = runtime->getCachedMousePosition();
	if (mouse.y > 430 || mouse.y < 0 || mouse.x > 640 || mouse.x < 0)
		return;

	bool landed = false;
	if (!tickBus(runtime, landed))
		return;

	// A direction queued during a hop starts a hop that the landing cancels again
	if (_cooldown > 0)
		_cooldown--;
	else if (_busAnim == kAnimIdle) {
		tickInput(runtime);
		if (landed)
			land(runtime);
	}

	if (!ride(runtime, landed))
		return;
	if (!tickLanes(runtime))
		return;
	tickRiders(runtime);
}

void LLavaModifier::startBusAnim(int anim, int32 frames, int32 cel) {
	_busAnim = anim;
	_busFrames = frames;
	_busCel = cel;
}

void LLavaModifier::showBusFrame(Runtime *runtime) {
	setCel(runtime, child(1), _busCel);
	_busCel++;
	_busFrames--;
}

// Returns false if the tick ends here
bool LLavaModifier::tickBus(Runtime *runtime, bool &landed) {
	Structural *bus = child(1);

	switch (_busAnim) {
	case kAnimHopUp:
	case kAnimHopLeft:
	case kAnimHopRight:
	case kAnimHopDown:
		if (_busFrames > 0) {
			showBusFrame(runtime);
			if (_busAnim == kAnimHopUp || _busAnim == kAnimHopDown) {
				_busX += _dx;
				_busY += _dy;
				place(runtime, bus, _busX, _busY);
			}
			break;
		}

		_busCel = 1;
		setCel(runtime, bus, 1);
		if (_busAnim == kAnimHopUp) {
			_busY -= 55.0;
			_busRow++;
		} else if (_busAnim == kAnimHopDown) {
			_busY += 55.0;
			_busRow--;
		} else if (_busAnim == kAnimHopLeft) {
			_busX -= 40.0;
		} else {
			_busX += 40.0;
		}
		landed = true;
		_busAnim = kAnimIdle;
		break;
	case kAnimSinking:
		if (_busFrames < 1) {
			_busAnim = kAnimLost;
			return false;
		}
		if (_busFrames == 5)
			dlgMgrSetInt(runtime, this, bus, "layer", 70);
		showBusFrame(runtime);
		return false;
	case kAnimBurning:
		// The burning bus keeps riding
		if (_busFrames > 0) {
			if (_busCel == 51)
				dlgMgrSetInt(runtime, this, bus, "layer", 2700);
			showBusFrame(runtime);
		} else {
			_busAnim = kAnimLost;
		}
		break;
	case kAnimGeyser:
		if (_busFrames < 1) {
			_busAnim = kAnimLost;
			return false;
		}
		showBusFrame(runtime);
		return false;
	case kAnimWon:
		if (_busFrames > 0) {
			if (_busFrames == 10)
				sendMessage(runtime, "PlaySFX14");
			showBusFrame(runtime);
			return false;
		}

		// Count the level bonus into the score, then move on
		if (_levelBonus > 0) {
			_levelBonus -= 10;
			_points += 10;
			sendMessage(runtime, "UpdateScore");
		} else if (_level != 5 && _level != 10 && _level != 15) {
			_gameState = kStateNewLevel;
			_paused = true;
			sendMessage(runtime, "levelwon");
			setupLevel(runtime, _level + 1);
		} else {
			_gameState = kStateNewGame;
			_paused = true;
			sendMessage(runtime, "YouWin");
		}
		return false;
	case kAnimLost:
		_paused = true;
		if (_lives < 2) {
			_gameState = kStateNewGame;
			sendMessage(runtime, "GameOver");
		} else {
			_gameState = kStateNewLevel;
			_lives--;
			setupLevel(runtime, _level);
		}
		return false;
	default:
		break;
	}

	return true;
}

void LLavaModifier::tickInput(Runtime *runtime) {
	switch (_dir) {
	case kDirUp:
		if (_busRow < 6)
			startHop(runtime, false);
		break;
	case kDirDown:
		if (_busRow > 0)
			startHop(runtime, true);
		break;
	case kDirLeft:
	case kDirRight:
		if ((_dir == kDirLeft && _busX > 60.0) || (_dir == kDirRight && _busX < 580.0)) {
			sendMessage(runtime, "PlaySFX1");
			if (_dir == kDirLeft)
				startBusAnim(kAnimHopLeft, 8, 19);
			else
				startBusAnim(kAnimHopRight, 8, 10);
			_dir = kDirNone;
			_dx = 0.0;
			_dy = 0.0;
		}
		break;
	default:
		break;
	}
}

// Hops up or down a lane, aiming at the first object that will be under the
// bus when it arrives
void LLavaModifier::startHop(Runtime *runtime, bool down) {
	sendMessage(runtime, "PlaySFX1");
	_dx = 0.0;
	_dy = 0.0;
	if (down)
		startBusAnim(kAnimHopDown, 8, 1);
	else
		startBusAnim(kAnimHopUp, 8, 27);
	_dir = kDirNone;

	double current = (_busRow >= 1 && _busRow <= 5) ? _lanes[_busRow - 1].speed : 0.0;
	int lane = down ? _busRow - 2 : _busRow;
	if (lane < 0 || lane >= kNumLanes)
		return;

	double ticks = down ? 9.0 : 8.0;
	double targetY = down ? _busY + 55.0 : _busY - 55.0;
	for (int i = 0; i < kNumSlots; i++) {
		const Object &obj = _objects[lane][i];
		if (obj.x == kLLavaEmpty || obj.type >= kNumTypes)
			continue;

		double objX = obj.x + _lanes[lane].speed * ticks;
		double busX = _busX + current * ticks;
		int16 ox = 0;
		int16 oy = 0;
		landOffset(obj.type, objX <= busX, ox, oy);

		const int16 *box = _types[obj.type].box;
		int32 a = static_cast<int32>(objX);
		int32 b = static_cast<int32>(busX);
		if (a - box[0] < b && b < a + box[1] && obj.y - box[2] < targetY && targetY < obj.y + box[3]) {
			_dx = (ox + objX - busX) / ticks;
			if (down)
				_dy = (oy + obj.y - targetY) / ticks;
			break;
		}
	}
}

void LLavaModifier::landOffset(int32 type, bool left, int16 &dx, int16 &dy) const {
	int set = (left && llavaTwoSided(type)) ? 1 : 0;
	dx = _types[type].land[set][0];
	dy = _types[type].land[set][1];
}

// The bus lands on whatever is under it, or in the lava
void LLavaModifier::land(Runtime *runtime) {
	_cooldown = 4;
	_dir = kDirNone;
	_busAnim = kAnimIdle;

	if (_busRow == 0 || _busRow == 6) {
		_busMotion = 0;
		if (_busRow == 0) {
			startBusAnim(kAnimWon, 10, 41);
			_levelBonus = static_cast<int32>(_mult * 100.0);
		}
		return;
	}

	int lane = _busRow - 1;
	bool hit = false;

	// Overlapping objects all count, the last one wins
	for (int i = 0; i < kNumSlots; i++) {
		const Object &obj = _objects[lane][i];
		if (!obj.active || obj.x == kLLavaEmpty || obj.type >= kNumTypes)
			continue;

		const int16 *box = _types[obj.type].box;
		if (!(obj.x - box[0] < _busX && _busX < obj.x + box[1] && obj.y - box[2] < _busY && _busY < obj.y + box[3]))
			continue;

		hit = true;
		switch (obj.type) {
		case 14:
			sendMessage(runtime, "PlaySFX10");	// Piano
			break;
		case 5:
			sendMessage(runtime, "PlaySFX11");	// Phone booth
			break;
		case 4:
		case 17:
			sendMessage(runtime, "PlaySFX12");	// Car
			break;
		case 12:
			sendMessage(runtime, "PlaySFX13");	// Bed
			break;
		default:
			sendMessage(runtime, "PlaySFX9");
			break;
		}

		int16 ox = 0;
		int16 oy = 0;
		landOffset(obj.type, obj.x <= _busX, ox, oy);
		_busX = obj.x + ox;
		_busY = obj.y + oy;
		_busMotion = obj.sub;
		_busPhase = obj.counter;

		// No points for a lane if the bus lands next to a fire
		bool nearFire = false;
		for (const Rider &fire : _riders[kRiderFire]) {
			if (fabs(fire.x - _busX) < 20.0 && fabs(fire.y - _busY) < 20.0)
				nearFire = true;
		}
		if (!nearFire && !_rowScored[_busRow]) {
			_points += static_cast<int32>(_mult * 10.0);
			sendMessage(runtime, "UpdateScore");
			_rowScored[_busRow] = true;
		}
	}

	if (!hit) {
		sendMessage(runtime, "PlaySFX3");
		startBusAnim(kAnimSinking, 5, 36);
	}

	for (uint b = 0; b < _riders[kRiderBonus].size(); b++) {
		const Rider &bonus = _riders[kRiderBonus][b];
		if (bonus.x == kLLavaEmpty || bonus.row < 0 || !_lanes[bonus.row].moving)
			continue;

		if (fabs(bonus.x - _busX) < 18.0 && fabs(bonus.y - _busY) < 30.0) {
			_points += static_cast<int32>(_mult * 20.0);
			sendMessage(runtime, "UpdateScore");
			resetRider(runtime, kRiderBonus, b);
			sendMessage(runtime, "PlaySFX7");
		}
	}
}

// Carries the bus along a moving lane. Returns false if the tick ends here.
bool LLavaModifier::ride(Runtime *runtime, bool landed) {
	bool moved = landed;
	if (_busRow >= 1 && _busRow <= 5 && _lanes[_busRow - 1].moving) {
		_busX += _lanes[_busRow - 1].speed;
		if (_busX < 20.0 || _busX > 620.0) {
			sendMessage(runtime, "PlaySFX2");
			startBusAnim(kAnimSinking, 5, 36);
			return false;
		}
		bob(_busMotion, _busPhase, _busY);
		moved = true;
	}

	if (moved)
		place(runtime, child(1), _busX, _busY);
	return true;
}

bool LLavaModifier::tickLanes(Runtime *runtime) {
	for (int r = 0; r < kNumLanes; r++) {
		for (int i = 0; i < kNumSlots; i++) {
			Object &obj = _objects[r][i];
			if (obj.x == kLLavaEmpty)
				continue;

			Structural *element = grandchild(r + 2, i + 1);
			if (!_lanes[r].moving) {
				if (obj.type == kLLavaGeyser && tickGeyser(runtime, obj, element))
					return false;
				continue;
			}

			obj.x += _lanes[r].speed;
			wrapAndSpawn(runtime, r, i);
			objectMotion(runtime, r, i);
			if (_busAnim == kAnimSinking) {
				_busFrames = 5;
				_busCel = 36;
				return false;
			}

			// Objects off the screen stay where they were last drawn
			if (obj.x > -70.0 && obj.x < 710.0 && obj.active)
				place(runtime, element, obj.x, obj.y);
		}
	}

	return true;
}

// Geysers build up, erupt for a while and settle again. Returns true if one
// caught the bus.
bool LLavaModifier::tickGeyser(Runtime *runtime, Object &obj, Structural *element) {
	if (obj.counter >= 62) {
		obj.counter--;
		return false;
	}

	if (obj.active) {
		setCel(runtime, element, 62 - obj.counter);
		if (obj.counter == 61)
			sendMessage(runtime, "PlaySFX6");
		obj.counter--;
		if (obj.counter == 18) {
			obj.active = false;
			obj.hold = -1;
		}
		return false;
	}

	const int16 *box = _types[kLLavaGeyser].box;
	if (_busAnim == kAnimIdle && obj.x - box[0] < _busX && _busX < obj.x + box[1] && obj.y - box[2] < _busY && _busY < obj.y + box[3]) {
		sendMessage(runtime, "PlaySFX8");
		startBusAnim(kAnimGeyser, 9, 64);
		return true;
	}

	if (obj.counter == 4 && obj.hold == -1)
		obj.hold = randomRange(runtime, 40, 160);

	if (obj.hold < 1) {
		setCel(runtime, element, 62 - obj.counter);
		obj.counter--;
		if (obj.counter == 0) {
			obj.active = true;
			obj.counter = randomRange(runtime, 70, 420);
		}
	} else {
		obj.hold--;
	}

	return false;
}

// Objects leaving one side of a moving lane come back on the other, sometimes
// with a fire, a critter or an extra life on them
void LLavaModifier::wrapAndSpawn(Runtime *runtime, int lane, int slot) {
	Object &obj = _objects[lane][slot];
	const Lane &laneInfo = _lanes[lane];
	if (obj.x > laneInfo.wrapLeft && obj.x < laneInfo.wrapRight)
		return;

	double newX = (obj.x >= laneInfo.wrapRight) ? laneInfo.wrapLeft : laneInfo.wrapRight;

	// Riders still on it are dropped
	if (obj.type < kNumTypes) {
		const int16 *box = _types[obj.type].box;
		for (int kind = 0; kind < kRiderKindCount; kind++) {
			for (uint i = 0; i < _riders[kind].size(); i++) {
				const Rider &rider = _riders[kind][i];
				if (rider.x == kLLavaEmpty)
					continue;
				if (obj.x - (box[0] + 18) < rider.x && rider.x < obj.x + (box[1] + 18) && obj.y - (box[2] + 35) < rider.y && rider.y < obj.y + (box[3] + 35)) {
					_riderCount[kind]--;
					resetRider(runtime, static_cast<RiderKind>(kind), i);
				}
			}
		}
	}

	obj.x = newX;

	int32 bonusRoll = randomRange(runtime, 0, 1000);
	int32 fireRoll = randomRange(runtime, 0, 1000);
	int32 xlifeRoll = randomRange(runtime, 0, 1000);

	if (fireRoll < _fireChance && _riderCount[kRiderFire] < _maxFires) {
		if (llavaNoFire(obj.type))
			return;
		for (uint f = 0; f < _riders[kRiderFire].size(); f++) {
			if (_riders[kRiderFire][f].x != kLLavaEmpty)
				continue;
			spawnRider(runtime, kRiderFire, f, obj);
			place(runtime, riderElement(kRiderFire, f), _riders[kRiderFire][f].x, _riders[kRiderFire][f].y - 8.0);
			return;
		}
		return;
	}

	if (bonusRoll < _bonusChance && _riderCount[kRiderBonus] < _maxBonus) {
		if (llavaRaft(obj.type))
			return;

		// The critters are tried in a random order
		int order[4];
		bool used[4] = {false, false, false, false};
		for (int k = 0; k < 4; k++) {
			int pick;
			do {
				pick = randomRange(runtime, 0, 3);
			} while (used[pick]);
			used[pick] = true;
			order[k] = pick;
		}

		for (int k = 0; k < 4; k++) {
			int b = order[k];
			if (_riders[kRiderBonus][b].x != kLLavaEmpty)
				continue;
			spawnRider(runtime, kRiderBonus, b, obj);
			place(runtime, riderElement(kRiderBonus, b), _riders[kRiderBonus][b].x, _riders[kRiderBonus][b].y);
			return;
		}
	} else if (xlifeRoll < 17 && _lives < 5 && !llavaRaft(obj.type)) {
		for (uint e = 0; e < _riders[kRiderXLife].size(); e++) {
			if (_riders[kRiderXLife][e].x != kLLavaEmpty)
				continue;
			spawnRider(runtime, kRiderXLife, e, obj);
			place(runtime, riderElement(kRiderXLife, e), _riders[kRiderXLife][e].x, _riders[kRiderXLife][e].y + 6.0);
			return;
		}
	}
}

void LLavaModifier::spawnRider(Runtime *runtime, RiderKind kind, int index, const Object &obj) {
	Rider &rider = _riders[kind][index];
	_riderCount[kind]++;
	rider.x = obj.x;
	rider.y = obj.y;
	rider.active = true;
	rider.sub = obj.sub;
	rider.phase = obj.counter;
	rider.row = obj.row;
	rider.type = obj.type;
	rider.cel = 1;

	int set = 0;
	if (llavaTwoSided(obj.type) && randomRange(runtime, 0, 1000) >= 500)
		set = 1;
	if (obj.type < kNumTypes) {
		rider.x += _types[obj.type].ride[set][0];
		rider.y += _types[obj.type].ride[set][1];
	}
}

void LLavaModifier::resetRider(Runtime *runtime, RiderKind kind, int index) {
	Rider &rider = _riders[kind][index];
	rider.x = kLLavaEmpty;
	rider.active = false;
	rider.row = -1;
	rider.type = kLLavaNoType;
	hide(runtime, riderElement(kind, index));
}

void LLavaModifier::bob(int32 sub, int32 &phase, double &y) const {
	if (sub < 1 || sub > 4)
		return;

	phase = (phase + 1) % 12;
	if (phase < 0)
		phase += 12;
	y += _motion[sub][phase];
}

// Bobbing objects go up and down, sinking ones go under and come back up now and then
void LLavaModifier::objectMotion(Runtime *runtime, int lane, int slot) {
	Object &obj = _objects[lane][slot];
	if (obj.sub != 5) {
		bob(obj.sub, obj.counter, obj.y);
		return;
	}

	if (obj.counter > 7) {
		obj.counter--;
		return;
	}

	Structural *element = grandchild(lane + 2, slot + 1);
	if (!obj.active) {
		if (obj.counter >= 0) {
			// Surfacing starts
			setCel(runtime, element, obj.counter + 1);
			obj.counter--;
			obj.active = true;
			return;
		}
	} else if (obj.counter >= 0) {
		// One frame every 4 ticks
		if (obj.delay > 0) {
			obj.delay--;
			return;
		}

		int32 cel;
		if (obj.sinkDir) {
			cel = 8 - obj.counter;
			if (obj.counter == 7 && obj.x > -70.0 && obj.x < 710.0)
				sendMessage(runtime, "PlaySFX5");
		} else {
			cel = obj.counter + 1;
		}
		setCel(runtime, element, cel);
		obj.counter--;
		obj.delay = 3;
		return;
	}

	obj.counter = randomRange(runtime, 24, 120);
	obj.delay = 3;
	if (obj.sinkDir)
		obj.active = !obj.active;
	obj.sinkDir = !obj.sinkDir;

	if (!obj.active) {
		// Just went under, with the bus if it was on it
		hide(runtime, element);
		const int16 *box = _types[obj.type].box;
		if (_busAnim == kAnimIdle && obj.x - box[0] < _busX && _busX < obj.x + box[1] && obj.y - box[2] < _busY && _busY < obj.y + box[3]) {
			sendMessage(runtime, "PlaySFX3");
			startBusAnim(kAnimSinking, 5, 36);
		}
	}
}

// Critters, fires and extra lives ride their objects. Returns false if a fire hit the bus.
bool LLavaModifier::tickRiders(Runtime *runtime) {
	for (uint b = 0; b < _riders[kRiderBonus].size(); b++) {
		Rider &bonus = _riders[kRiderBonus][b];
		if (bonus.x == kLLavaEmpty || bonus.row < 0 || !_lanes[bonus.row].moving)
			continue;
		bonus.x += _lanes[bonus.row].speed;
		bob(bonus.sub, bonus.phase, bonus.y);
		place(runtime, riderElement(kRiderBonus, b), bonus.x, bonus.y);
	}

	for (uint f = 0; f < _riders[kRiderFire].size(); f++) {
		Rider &fire = _riders[kRiderFire][f];
		if (fire.x == kLLavaEmpty || fire.row < 0 || !_lanes[fire.row].moving)
			continue;

		Structural *element = riderElement(kRiderFire, f);
		fire.x += _lanes[fire.row].speed;
		fire.cel = (fire.cel + 1) % 14;
		setCel(runtime, element, fire.cel);
		bob(fire.sub, fire.phase, fire.y);
		if (_busAnim == kAnimIdle && fabs(fire.x - _busX) < 20.0 && fabs(fire.y - _busY) < 20.0) {
			sendMessage(runtime, "PlaySFX8");
			startBusAnim(kAnimBurning, 10, 51);
			return false;
		}
		place(runtime, element, fire.x, fire.y - 8.0);
	}

	for (Ripple &ripple : _ripples) {
		ripple.cel = (ripple.cel + 1) % 5;
		setCel(runtime, grandchild(10, ripple.child), ripple.cel);
	}

	for (uint e = 0; e < _riders[kRiderXLife].size(); e++) {
		Rider &xlife = _riders[kRiderXLife][e];
		if (xlife.x == kLLavaEmpty || xlife.row < 0 || !_lanes[xlife.row].moving)
			continue;

		xlife.x += _lanes[xlife.row].speed;
		bob(xlife.sub, xlife.phase, xlife.y);
		if (_busAnim != kAnimIdle || !(fabs(xlife.x - _busX) < 20.0 && fabs(xlife.y - _busY) < 20.0)) {
			place(runtime, riderElement(kRiderXLife, e), xlife.x, xlife.y + 6.0);
		} else if (_lives < 5) {
			_lives++;
			sendMessage(runtime, "PlaySFX4");
			_riderCount[kRiderXLife]--;
			resetRider(runtime, kRiderXLife, e);
			setChances(runtime, _lives);
		}
	}

	return true;
}

Common::SharedPtr<Modifier> LLavaModifier::shallowClone() const {
	Common::SharedPtr<LLavaModifier> clone(new LLavaModifier(*this));
	clone->_scheduledEvent.reset();
	clone->_keySignaller.reset();
	clone->_running = false;
	clone->_paused = false;
	clone->_levelReady = false;
	clone->_level = 1;
	clone->_points = 0;
	return clone;
}

const char *LLavaModifier::getDefaultName() const {
	return "LLava";
}

// Children of the owner element
enum SvTownChild {
	kSvTownVents = 1,
	kSvTownSteam = 2,
	kSvTownLava = 3,
	kSvTownLeftBlocks = 4,
	kSvTownRightBlocks = 5,
	kSvTownNav = 6,
	kSvTownChopper = 7,
	kSvTownHomes = 8,
	kSvTownSmoke = 9,
	kSvTownTrees = 10,
	kSvTownBackgrounds = 11,
	kSvTownPoints = 12,
	kSvTownChances = 13,
	kSvTownFrame = 14,
	kSvTownShadow = 15,
	kSvTownBrokenLeft = 16,
	kSvTownBrokenRight = 17,
};

static const int32 kSvTownCell = 38;
static const int32 kSvTownHidden = -100;
static const int32 kSvTownChopperLayer = 8560;

// Home types: 1-4 houses, 5, 7, 9, 10 and 11 two cells wide houses, 12-14 cars
static bool svTownIsCar(int32 type) {
	return type >= 12 && type <= 14;
}

static bool svTownIsWide(int32 type) {
	return type == 5 || type == 7 || type == 9 || type == 10 || type == 11;
}

// Lava cels 1-12 flow straight down, 13-24 spread left and 25-36 spread right
static bool svTownIsFinalCel(int32 cel) {
	return cel == 12 || cel == 24 || cel == 36;
}

SvTownModifier::SvTownModifier()
	: _firstBlockCol(0), _firstBlockRow(0), _levelsLoaded(false), _running(false), _initialized(false), _paused(false),
	  _halted(false), _cursorIn(false), _firstBlockSent(false), _level(1), _score(0), _state(kStateNewGame), _lives(5),
	  _introCountdown(0), _lavaRow(0), _lastRow(11), _steps(11), _delay(0), _carry(kCarryNone), _pendingDrop(false),
	  _chopState(0), _chopCel(1), _chopLastX(kSvTownHidden), _shadowCol(kSvTownHidden), _shadowRow(kSvTownHidden),
	  _shadowCel(1), _popupTimer(0), _numPopups(0), _lavaCount(0), _leftUsed(0), _rightUsed(0), _brokenLeftCount(0),
	  _brokenRightCount(0) {
	memset(_placeRange, 0, sizeof(_placeRange));
	memset(_spreadRange, 0, sizeof(_spreadRange));
	memset(_typeOffsets, 0, sizeof(_typeOffsets));
	memset(_dirThresholds, 0, sizeof(_dirThresholds));
	memset(_forkThresholds, 0, sizeof(_forkThresholds));
	memset(_popups, 0, sizeof(_popups));
	memset(_lava, 0, sizeof(_lava));
	memset(_leftBlocks, 0, sizeof(_leftBlocks));
	memset(_rightBlocks, 0, sizeof(_rightBlocks));
	memset(_brokenLeft, 0, sizeof(_brokenLeft));
	memset(_brokenRight, 0, sizeof(_brokenRight));
	memset(_leftList, 0, sizeof(_leftList));
	memset(_rightList, 0, sizeof(_rightList));
	memset(_homes, 0, sizeof(_homes));
	memset(_trees, 0, sizeof(_trees));
	memset(_steam, 0, sizeof(_steam));
	memset(_smoke, 0, sizeof(_smoke));
}

SvTownModifier::~SvTownModifier() {
	stopGame();
}

bool SvTownModifier::load(const PlugInModifierLoaderContext &context, const Data::KW::GameModifier &data) {
	return loadDlgMgrEvent(_startWhen, data.enableWhen) && loadDlgMgrEvent(_stopWhen, data.disableWhen);
}

// The levels are tables in the plug-in's data section
bool SvTownModifier::loadLevels() {
	if (_levelsLoaded)
		return !_levels.empty();

	_levelsLoaded = true;

	Common::File f;
	if (!f.open(Common::Path("mTrop_Resources/SvTown.r32")) && !f.open(Common::Path("SvTown.r32"))) {
		warning("SvTown: SvTown.r32 from the installed game is needed for the levels");
		return false;
	}

	Common::Array<byte> image;
	image.resize(f.size());
	f.read(&image[0], image.size());

	const uint32 kImageBase = 0x10000000;
	bool failed = false;
	auto readS32 = [&](uint32 va) -> int32 {
		uint32 offset = va - kImageBase;
		if (offset + 4 > image.size()) {
			failed = true;
			return 0;
		}
		return static_cast<int32>(READ_LE_UINT32(&image[offset]));
	};
	auto readS16 = [&](uint32 va) -> int32 {
		uint32 offset = va - kImageBase;
		if (offset + 2 > image.size()) {
			failed = true;
			return 0;
		}
		return static_cast<int16>(READ_LE_UINT16(&image[offset]));
	};

	// The range tables are indexed with level * 24 + row, past the end of a row's 12 entries for the
	// bottom row, so they are kept whole
	for (int i = 0; i < kRangeTableSize; i++) {
		_placeRange[i] = readS32(0x1000f1b0 + i * 4);
		_spreadRange[i] = readS32(0x1000f750 + i * 4);
	}

	for (int type = 0; type < kNumTypes; type++) {
		_typeOffsets[type][0] = readS32(0x1000f0c0 + type * 8);
		_typeOffsets[type][1] = readS32(0x1000f0c0 + type * 8 + 4);
	}

	// Lava direction thresholds for the left edge, right edge, left half and right half
	for (int table = 0; table < 4; table++) {
		for (int i = 0; i < 4; i++)
			_dirThresholds[table][i] = readS32(0x1000f150 + table * 16 + i * 4);
	}
	_forkThresholds[0] = readS32(0x1000f190);
	_forkThresholds[1] = readS32(0x1000f194);

	// Level 1 starts with a right block in place
	_firstBlockCol = readS32(0x1000f1dc);
	_firstBlockRow = readS32(0x1000f1e0);

	Common::Array<Level> levels;
	levels.resize(16);
	for (uint l = 1; l <= 15; l++) {
		Level &level = levels[l];
		level.bk = readS16(0x1000f19a + l * 2);
		level.numLava = readS16(0x1000f1ba + l * 2);
		level.delay = readS16(0x1000f1ea + l * 2);

		uint32 base = 0x1000f88c + l * 0x4c4;
		level.startRow = readS32(base);
		level.randMin = readS32(base + 4);
		level.randMax = readS32(base + 8);
		level.colBase = readS32(base + 12);

		for (uint i = 0; i < kNumHomes; i++) {
			uint32 va = base + 2 + 14 * (i + 1);
			level.homes[i].col = readS32(va);
			level.homes[i].row = readS32(va + 4);
			level.homes[i].type = readS32(va + 8);
		}
		for (uint i = 0; i < kNumTrees; i++) {
			uint32 va = base + 0x24e + 14 * (i + 1);
			level.trees[i].col = readS32(va);
			level.trees[i].row = readS32(va + 4);
			level.trees[i].type = readS32(va + 8);
		}
	}

	// Level 1's third home is a house at (7, 3)
	const TableObject &check = levels[1].homes[2];
	if (failed || check.col != 7 || check.row != 3 || check.type != 1 || _firstBlockCol != 8 || _firstBlockRow != 3) {
		warning("SvTown: SvTown.r32 isn't the expected version");
		return false;
	}

	for (const Level &level : levels) {
		for (const TableObject &obj : level.homes) {
			if (obj.col != kSvTownHidden && (obj.type < 0 || obj.type >= kNumTypes))
				failed = true;
		}
		for (const TableObject &obj : level.trees) {
			if (obj.col != kSvTownHidden && (obj.type < 0 || obj.type >= kNumTypes))
				failed = true;
		}
	}
	if (failed) {
		warning("SvTown: SvTown.r32 has unexpected object types");
		return false;
	}

	_levels.swap(levels);
	return true;
}

bool SvTownModifier::respondsToEvent(const Event &evt) const {
	return _startWhen.respondsTo(evt) || _stopWhen.respondsTo(evt) || evt.eventType == EventIDs::kParentDisabled
		|| evt.eventType == EventIDs::kMouseDown || evt.eventType == EventIDs::kMouseUp;
}

VThreadState SvTownModifier::consumeMessage(Runtime *runtime, const Common::SharedPtr<MessageProperties> &msg) {
	const Event &evt = msg->getEvent();

	if (_startWhen.respondsTo(evt)) {
		_state = kStateNewGame;
		setupLevel(runtime, 1);
		_firstBlockSent = false;

		if (!_running) {
			_running = true;
			// The original ticks every 83 milliseconds
			_scheduledEvent = runtime->getScheduler().scheduleMethod<SvTownModifier, &SvTownModifier::tick>(runtime->getPlayTime() + 83, this);
		}
		return kVThreadReturn;
	}

	if (_stopWhen.respondsTo(evt) || evt.eventType == EventIDs::kParentDisabled) {
		stopGame();
		return kVThreadReturn;
	}

	if (evt.eventType == EventIDs::kMouseDown)
		mouseDown(runtime);

	return kVThreadReturn;
}

void SvTownModifier::disable(Runtime *runtime) {
	stopGame();
}

void SvTownModifier::stopGame() {
	_running = false;
	if (_scheduledEvent) {
		_scheduledEvent->cancel();
		_scheduledEvent.reset();
	}
}

bool SvTownModifier::readAttribute(MiniscriptThread *thread, DynamicValue &result, const Common::String &attrib) {
	if (attrib == "paused") {
		result.setBool(_paused);
		return true;
	} else if (attrib == "gamelevel") {
		result.setInt(_level);
		return true;
	} else if (attrib == "points") {
		result.setInt(_score);
		return true;
	}

	return Modifier::readAttribute(thread, result, attrib);
}

MiniscriptInstructionOutcome SvTownModifier::writeRefAttribute(MiniscriptThread *thread, DynamicValueWriteProxy &result, const Common::String &attrib) {
	WritableAttribute writable;
	if (attrib == "paused")
		writable = kAttribPaused;
	else if (attrib == "gamelevel")
		writable = kAttribGameLevel;
	else if (attrib == "hsscreen")
		writable = kAttribHSScreen;
	else
		return Modifier::writeRefAttribute(thread, result, attrib);

	result.pod.ifc = DynamicValueWriteInterfaceGlue<WriteProxy>::getInstance();
	result.pod.objectRef = this;
	result.pod.ptrOrOffset = writable;
	return kMiniscriptInstructionOutcomeContinue;
}

MiniscriptInstructionOutcome SvTownModifier::WriteProxy::write(MiniscriptThread *thread, const DynamicValue &assigned, void *objectRef, uintptr ptrOrOffset) {
	SvTownModifier *self = static_cast<SvTownModifier *>(objectRef);
	Runtime *runtime = thread->getRuntime();

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

		// The original has no lower bound
		if (level < 16) {
			self->_state = kStateNewGame;
			self->setupLevel(runtime, MAX<int32>(level, 1));
			self->_halted = false;
		}
		return kMiniscriptInstructionOutcomeContinue;
	}

	bool flag = false;
	if (value.getType() == DynamicValueTypes::kBoolean)
		flag = value.getBool();
	else if (value.getType() == DynamicValueTypes::kInteger)
		flag = (value.getInt() != 0);
	else
		return kMiniscriptInstructionOutcomeFailed;

	if (ptrOrOffset == kAttribHSScreen) {
		// The high score screen halts the game
		self->_halted = flag;
	} else if (self->_initialized || flag) {
		// Unpausing only takes effect once a level is set up
		self->_paused = flag;
		if (self->_state != kStateNewGame && self->_state != kStateNextLevel)
			self->_state = kStatePlaying;

		const Common::Point &mouse = runtime->getCachedMousePosition();
		if (!flag && mouse.x > 9 && mouse.x < 631 && mouse.y > 13 && mouse.y < 431)
			self->sendMessage(runtime, "choppercursor");
	}

	return kMiniscriptInstructionOutcomeContinue;
}

MiniscriptInstructionOutcome SvTownModifier::WriteProxy::refAttrib(MiniscriptThread *thread, DynamicValueWriteProxy &proxy, void *objectRef, uintptr ptrOrOffset, const Common::String &attrib) {
	return kMiniscriptInstructionOutcomeFailed;
}

MiniscriptInstructionOutcome SvTownModifier::WriteProxy::refAttribIndexed(MiniscriptThread *thread, DynamicValueWriteProxy &proxy, void *objectRef, uintptr ptrOrOffset, const Common::String &attrib, const DynamicValue &index) {
	return kMiniscriptInstructionOutcomeFailed;
}

Structural *SvTownModifier::child(int index) const {
	Structural *owner = findStructuralOwner();
	if (!owner || index < 1 || index > static_cast<int>(owner->getChildren().size()))
		return nullptr;
	return owner->getChildren()[index - 1].get();
}

Structural *SvTownModifier::grandchild(int index, int subIndex) const {
	Structural *container = child(index);
	if (!container || subIndex < 1 || subIndex > static_cast<int>(container->getChildren().size()))
		return nullptr;
	return container->getChildren()[subIndex - 1].get();
}

void SvTownModifier::place(Runtime *runtime, Structural *element, int32 x, int32 y) {
	if (!element || !element->isElement() || !static_cast<Element *>(element)->isVisual())
		return;

	// Placed centered on the point
	const Common::Rect &rect = static_cast<VisualElement *>(element)->getRelativeRect();
	placeTopLeft(runtime, element, x - rect.width() / 2, y - rect.height() / 2);
}

void SvTownModifier::placeTopLeft(Runtime *runtime, Structural *element, int32 x, int32 y) {
	if (!element)
		return;

	DynamicValue pos;
	pos.setPoint(Common::Point(x, y));
	dlgMgrWriteAttribute(runtime, this, element, "position", pos);
}

void SvTownModifier::hide(Runtime *runtime, Structural *element) {
	placeTopLeft(runtime, element, kSvTownHidden, 600);
}

void SvTownModifier::setInt(Runtime *runtime, Structural *element, const char *attrib, int32 value) {
	dlgMgrSetInt(runtime, this, element, attrib, value);
}

void SvTownModifier::setBool(Runtime *runtime, Structural *element, const char *attrib, bool value) {
	dlgMgrSetBool(runtime, this, element, attrib, value);
}

void SvTownModifier::sendMessage(Runtime *runtime, const char *messageName) {
	uint32 messageID = 0;
	if (!runtime->getProject()->findAuthorMessageID(messageName, messageID)) {
		debug(2, "SvTown: No author message '%s'", messageName);
		return;
	}

	debug(2, "SvTown: Sending '%s'", messageName);

	Structural *owner = findStructuralOwner();
	if (!owner)
		return;

	Common::SharedPtr<MessageProperties> props(new MessageProperties(Event(EventIDs::kAuthorMessage, messageID), DynamicValue(), getSelfReference()));
	Common::SharedPtr<MessageDispatch> dispatch(new MessageDispatch(props, owner, true, true, false));
	runtime->queueMessage(dispatch);
}

int32 SvTownModifier::randomRange(Runtime *runtime, int32 lo, int32 hi) {
	if (hi <= lo)
		return lo;
	return lo + static_cast<int32>(runtime->getRandom()->getRandomNumber(static_cast<uint>(hi - lo)));
}

void SvTownModifier::setupLevel(Runtime *runtime, int32 levelNum) {
	if (!loadLevels() || levelNum < 1 || levelNum > 15)
		return;

	debug(2, "SvTown: Setting up level %i", static_cast<int>(levelNum));

	const Level &level = _levels[levelNum];

	_initialized = false;
	_paused = true;
	_level = levelNum;
	_introCountdown = 24;
	_steps = 11;
	_delay = level.delay;
	_lavaCount = 0;
	_leftUsed = 0;
	_rightUsed = 0;
	_brokenLeftCount = 0;
	_brokenRightCount = 0;
	_carry = kCarryNone;
	_pendingDrop = false;
	_cursorIn = false;
	_halted = false;

	if (!_firstBlockSent && levelNum != 1) {
		sendMessage(runtime, "firstblockpicked");
		_firstBlockSent = true;
	}

	for (int i = 0; i < kNumBlocks; i++) {
		_leftList[i] = i;
		_rightList[i] = i;
	}

	_lastRow = (levelNum % 5 == 0) ? 10 : 11;

	if (_state == kStateNewGame) {
		_score = 0;
		sendMessage(runtime, "UpdateScore");
	}

	_lives = 5;

	for (int i = 1; i <= kNumVents; i++) {
		Structural *vent = grandchild(kSvTownVents, i);
		place(runtime, vent, kSvTownHidden, 600);
		setInt(runtime, vent, "layer", 20 + i);
	}

	for (int i = 0; i < kNumSteam; i++) {
		Structural *steam = grandchild(kSvTownSteam, i + 1);
		place(runtime, steam, kSvTownHidden, 600);
		setInt(runtime, steam, "layer", 8001 + i);
		_steam[i].active = false;
		_steam[i].cel = 1;
		setBool(runtime, steam, "paused", true);
		setInt(runtime, steam, "cel", 1);
	}

	for (int i = 0; i < kNumBlocks; i++) {
		_leftBlocks[i].col = _leftBlocks[i].row = kSvTownHidden;
		_leftBlocks[i].cel = -1;
		hide(runtime, grandchild(kSvTownLeftBlocks, i + 1));
		_rightBlocks[i].col = _rightBlocks[i].row = kSvTownHidden;
		_rightBlocks[i].cel = -1;
		hide(runtime, grandchild(kSvTownRightBlocks, i + 1));
	}

	// The starting lava pieces, with vents and steam on the first level of each tier
	_lavaRow = level.startRow + 1;
	int ventsUsed = 0;
	int steamUsed = 0;
	for (int i = 0; i < kNumLava; i++) {
		Piece &lava = _lava[i];
		Structural *elem = grandchild(kSvTownLava, i + 1);
		place(runtime, elem, kSvTownHidden, 600);
		setBool(runtime, elem, "paused", true);
		setInt(runtime, elem, "cel", 1);
		lava.cel = -1;
		lava.col = lava.row = kSvTownHidden;

		if (i >= level.numLava)
			continue;

		if (levelNum == 1) {
			lava.cel = 25;
			setInt(runtime, elem, "cel", lava.cel);
			lava.row = level.startRow;
			lava.y = lava.row * kSvTownCell + 2;
			lava.col = 6;
			lava.x = lava.col * kSvTownCell + 9;

			int b = _rightList[_rightUsed];
			Piece &block = _rightBlocks[b];
			block.col = _firstBlockCol;
			block.row = _firstBlockRow;
			block.x = block.col * kSvTownCell - 39;
			block.y = block.row * kSvTownCell - 45;
			placeTopLeft(runtime, grandchild(kSvTownRightBlocks, b + 1), block.x, block.y);
			_rightUsed++;
		} else {
			// Pick a column that no other piece starts in, and don't let two pieces spread into each other
			for (int attempt = 0; attempt < 40; attempt++) {
				lava.cel = randomRange(runtime, 1, 3) * 12 - 11;
				setInt(runtime, elem, "cel", lava.cel);
				lava.row = level.startRow;
				lava.y = lava.row * kSvTownCell + 2;
				lava.col = level.colBase + randomRange(runtime, level.randMin, level.randMax);
				lava.x = lava.col * kSvTownCell + 9;

				bool clash = false;
				for (int j = 0; j < i && !clash; j++) {
					const Piece &other = _lava[j];
					if (other.col == lava.col)
						clash = true;
					else if (lava.cel == 13 && other.cel == 25 && lava.col == other.col + 1)
						clash = true;
					else if (lava.cel == 25 && other.cel == 13 && lava.col + 1 == other.col)
						clash = true;
				}
				if (!clash)
					break;
			}
		}

		_lavaCount++;
		placeTopLeft(runtime, elem, lava.x, lava.y);

		if (levelNum % 5 == 1 && ventsUsed < kNumVents)
			placeTopLeft(runtime, grandchild(kSvTownVents, ++ventsUsed), lava.x + 34, lava.y - 9);

		if (levelNum % 5 == 1 && steamUsed < kNumSteam) {
			Puff &steam = _steam[steamUsed];
			Structural *steamElem = grandchild(kSvTownSteam, ++steamUsed);
			steam.x = lava.x + 44;
			steam.y = lava.y - 24;
			steam.cel = randomRange(runtime, 1, 5);
			setInt(runtime, steamElem, "cel", steam.cel);
			place(runtime, steamElem, steam.x, steam.y);
			steam.active = true;
		}
	}

	// The level banner. Levels 5, 10 and 15 set cel 0, as in the original.
	placeTopLeft(runtime, grandchild(kSvTownNav, 1), 151, 160);
	Structural *levelNumber = grandchild(kSvTownNav, 2);
	setInt(runtime, levelNumber, "cel", levelNum % 5);
	placeTopLeft(runtime, levelNumber, 391, 198);

	_chopState = 0;
	_chopCel = 1;
	_chopLastX = kSvTownHidden;
	Structural *chopper = child(kSvTownChopper);
	place(runtime, chopper, kSvTownHidden, 600);
	setBool(runtime, chopper, "paused", true);
	setInt(runtime, chopper, "cel", 1);
	setInt(runtime, chopper, "layer", kSvTownChopperLayer);

	// Element i of the homes and trees matches table record i
	for (int i = 0; i < kNumHomes; i++) {
		Home &home = _homes[i];
		Structural *elem = grandchild(kSvTownHomes, i + 1);
		place(runtime, elem, kSvTownHidden, 600);
		setBool(runtime, elem, "paused", true);
		setInt(runtime, elem, "cel", 1);
		home.cel = 1;
		home.x = kSvTownHidden;
		home.y = 600;
		home.col = level.homes[i].col;
		home.row = level.homes[i].row;
		home.type = level.homes[i].type;
		home.active = false;
		if (home.col != kSvTownHidden) {
			home.x = home.col * kSvTownCell + 9 + _typeOffsets[home.type][0];
			home.y = home.row * kSvTownCell + 2 + _typeOffsets[home.type][1];
			home.active = true;
			placeTopLeft(runtime, elem, home.x, home.y);
		}
	}

	for (int i = 0; i < kNumSmoke; i++) {
		Structural *elem = grandchild(kSvTownSmoke, i + 1);
		place(runtime, elem, kSvTownHidden, 600);
		setInt(runtime, elem, "layer", 3301 + i);
		_smoke[i].active = false;
		_smoke[i].cel = 1;
		_smoke[i].x = kSvTownHidden;
		_smoke[i].y = 600;
		setBool(runtime, elem, "paused", true);
		setInt(runtime, elem, "cel", 1);
	}

	for (int i = 0; i < kNumTrees; i++) {
		Home &tree = _trees[i];
		Structural *elem = grandchild(kSvTownTrees, i + 1);
		place(runtime, elem, kSvTownHidden, 600);
		tree.cel = 1;
		tree.x = kSvTownHidden;
		tree.y = 600;
		tree.col = level.trees[i].col;
		tree.row = level.trees[i].row;
		tree.type = level.trees[i].type;
		tree.active = false;
		setBool(runtime, elem, "paused", true);
		setInt(runtime, elem, "cel", 1);
		setInt(runtime, elem, "layer", 121 + i);
		if (tree.col != kSvTownHidden) {
			// Trees are scattered a little around their cell
			tree.x = tree.col * kSvTownCell + 9 + (randomRange(runtime, 1, 20) - 10) + _typeOffsets[tree.type][0];
			tree.y = tree.row * kSvTownCell + 2 + (randomRange(runtime, 1, 20) - 10) + _typeOffsets[tree.type][1];
			tree.active = true;
			placeTopLeft(runtime, elem, tree.x, tree.y);
		}
	}

	for (int i = 1; i <= 5; i++) {
		Structural *bk = grandchild(kSvTownBackgrounds, i);
		if (i == level.bk) {
			setInt(runtime, bk, "layer", 4);
			placeTopLeft(runtime, bk, 0, 0);
		} else
			hide(runtime, bk);
	}

	// Score popups come in three tiers of 16. Level 15 is in a fourth tier and has none, as in the original.
	_popupTimer = 0;
	int base = (levelNum / 5) * 16 + 1;
	_numPopups = 0;
	memset(_popups, 0, sizeof(_popups));
	for (int k = 1; k <= 48; k++) {
		Structural *popup = grandchild(kSvTownPoints, k);
		place(runtime, popup, kSvTownHidden, 600);
		setBool(runtime, popup, "paused", true);
		if (k >= base && k < base + 15 && _numPopups < kNumPopups) {
			_popups[_numPopups++] = k;
			setInt(runtime, popup, "layer", 3350 + k);
			setInt(runtime, popup, "cel", 1);
		}
	}

	Structural *chances = child(kSvTownChances);
	setBool(runtime, chances, "paused", true);
	setInt(runtime, chances, "cel", _lives + 1);

	setInt(runtime, child(kSvTownFrame), "layer", 6844);

	Structural *shadow = child(kSvTownShadow);
	setInt(runtime, shadow, "layer", 40);
	hide(runtime, shadow);
	_shadowCol = _shadowRow = kSvTownHidden;
	_shadowCel = 1;
	setBool(runtime, shadow, "paused", true);
	setInt(runtime, shadow, "cel", 1);

	for (int i = 0; i < kNumBlocks; i++) {
		Structural *elem = grandchild(kSvTownBrokenLeft, i + 1);
		setInt(runtime, elem, "layer", 51 + i);
		_brokenLeft[i].col = _brokenLeft[i].row = kSvTownHidden;
		_brokenLeft[i].cel = -1;
		hide(runtime, elem);

		elem = grandchild(kSvTownBrokenRight, i + 1);
		setInt(runtime, elem, "layer", 121 + i);
		_brokenRight[i].col = _brokenRight[i].row = kSvTownHidden;
		_brokenRight[i].cel = -1;
		hide(runtime, elem);
	}

	// Initial depth sort: lower rows are in front, then left to right within a row
	int32 counters[17];
	for (int k = 0; k < 17; k++)
		counters[k] = (k < 16) ? (k + 1) * 10 : 0;

	auto sortLayer = [&](Structural *elem, int32 col, int32 row) {
		if (col == kSvTownHidden)
			return;
		int k = CLIP<int32>(col, 0, 16);
		setInt(runtime, elem, "layer", counters[k] + (row + 2) * 200);
		counters[k]++;
	};

	for (int i = 0; i < kNumTrees; i++)
		sortLayer(grandchild(kSvTownTrees, i + 1), _trees[i].col, _trees[i].row);
	for (int i = 0; i < kNumBlocks; i++) {
		sortLayer(grandchild(kSvTownLeftBlocks, i + 1), _leftBlocks[i].col, _leftBlocks[i].row);
		sortLayer(grandchild(kSvTownRightBlocks, i + 1), _rightBlocks[i].col, _rightBlocks[i].row);
	}
	for (int i = 0; i < kNumHomes; i++)
		sortLayer(grandchild(kSvTownHomes, i + 1), _homes[i].col, _homes[i].row);
	for (int i = 0; i < kNumLava; i++)
		setInt(runtime, grandchild(kSvTownLava, i + 1), "layer", 100 + i);

	_initialized = true;

	const Common::Point &mouse = runtime->getCachedMousePosition();
	if (mouse.x > 10 && mouse.x < 630 && mouse.y > 14 && mouse.y < 430)
		sendMessage(runtime, "choppercursor");
}

void SvTownModifier::firstPick(Runtime *runtime) {
	if (!_firstBlockSent) {
		sendMessage(runtime, "firstblockpicked");
		_firstBlockSent = true;
	}
}

void SvTownModifier::mouseDown(Runtime *runtime) {
	if (_levels.empty())
		return;

	const Common::Point &mouse = runtime->getCachedMousePosition();

	// A click skips the level banner
	if (_state == kStateNewGame || _state == kStateNextLevel) {
		_paused = false;
		_state = kStatePlaying;
		place(runtime, grandchild(kSvTownNav, 1), -1, 600);
		place(runtime, grandchild(kSvTownNav, 2), -1, 600);
		sendMessage(runtime, "canceldialog");
		return;
	}

	if (_paused)
		return;

	// The left pile
	if (_carry == kCarryNone && _leftUsed < 59 && mouse.x > 11 && mouse.x < 57 && mouse.y > 264 && mouse.y < 324) {
		_pendingDrop = false;
		_carry = kCarryLeft;
		sendMessage(runtime, "PlaySFX1");
		firstPick(runtime);
		return;
	}

	// Holding a block, so the tick drops it
	if (_carry != kCarryNone) {
		_dropPoint = mouse;
		_pendingDrop = true;
		return;
	}

	// The right pile
	if (_rightUsed <= 58 && mouse.x >= 585 && mouse.x <= 629 && mouse.y >= 267 && mouse.y <= 325) {
		_pendingDrop = false;
		_carry = kCarryRight;
		sendMessage(runtime, "PlaySFX1");
		firstPick(runtime);
		return;
	}

	// Pick up a block from the board. The free list is shifted from list position k rather than from
	// the block's position in it, and the used count isn't decremented; both are as in the original.
	int32 col = (mouse.x - 16) / kSvTownCell;
	int32 row = (mouse.y - 10) / kSvTownCell;
	for (int k = 0; k < kNumBlocks; k++) {
		bool left = (_leftBlocks[k].col == col && _leftBlocks[k].row == row);
		bool right = !left && (_rightBlocks[k].col == col && _rightBlocks[k].row == row);
		if (!left && !right)
			continue;

		Piece &block = left ? _leftBlocks[k] : _rightBlocks[k];
		int32 *list = left ? _leftList : _rightList;
		int32 &used = left ? _leftUsed : _rightUsed;

		block.col = block.row = kSvTownHidden;
		_pendingDrop = false;
		_carry = left ? kCarryLeft : kCarryRight;
		sendMessage(runtime, "PlaySFX1");
		hide(runtime, grandchild(left ? kSvTownLeftBlocks : kSvTownRightBlocks, k + 1));
		for (int i = k; i < kNumBlocks - 1; i++)
			list[i] = list[i + 1];
		list[kNumBlocks - 1] = k;
		if (used == 59)
			used = 58;
		return;
	}
}

void SvTownModifier::tick(Runtime *runtime) {
	_scheduledEvent.reset();
	if (!_running)
		return;

	_scheduledEvent = runtime->getScheduler().scheduleMethod<SvTownModifier, &SvTownModifier::tick>(runtime->getPlayTime() + 83, this);

	if (_halted || _levels.empty())
		return;

	// The cursor is the helicopter only inside the play area
	const Common::Point mouse = runtime->getCachedMousePosition();
	if (mouse.y > 430 || mouse.y < 14 || mouse.x > 630 || mouse.x < 10) {
		if (_cursorIn) {
			sendMessage(runtime, "resetcursor");
			_cursorIn = false;
		}
		return;
	}

	if (!_cursorIn)
		sendMessage(runtime, "choppercursor");
	_cursorIn = true;

	if (_paused)
		return;

	// The level banner shows for 2 seconds
	if (_state == kStateNewGame || _state == kStateNextLevel) {
		if (_introCountdown < 1) {
			_paused = false;
			_state = kStatePlaying;
			place(runtime, grandchild(kSvTownNav, 1), -1, 600);
			place(runtime, grandchild(kSvTownNav, 2), -1, 600);
		} else
			_introCountdown--;
		return;
	}

	// The lava has reached the bottom
	if (_lavaRow > 10 && _steps < 7 && _steps > 0) {
		_paused = true;
		sendMessage(runtime, "resetcursor");
		_cursorIn = false;

		if (_level != 5 && _level != 10 && _level != 15) {
			debug(2, "SvTown: Level %i won", static_cast<int>(_level));
			_state = kStateNextLevel;
			sendMessage(runtime, "levelwon");
			resetLayers(runtime);
			setupLevel(runtime, _level + 1);
			_paused = false;
		} else {
			debug(2, "SvTown: Game won on level %i", static_cast<int>(_level));
			_state = kStateNewGame;
			resetLayers(runtime);
			_halted = true;
			hide(runtime, child(kSvTownChopper));
			sendMessage(runtime, "YouWin");
		}
		return;
	}

	// Each row of lava takes 11 cel advances, the delay apart
	bool spawned = false;
	if (_steps < 1) {
		_steps = 11;
		_delay = _levels[_level].delay;
		spawnRow(runtime);
		spawned = true;
		if (_level % 5 == 0 && _lavaRow == _lastRow + 1)
			finalEruption(runtime);
	} else if (_delay < 1) {
		_delay = _levels[_level].delay;
		_steps--;
		for (int i = 0; i < kNumLava; i++) {
			Piece &lava = _lava[i];
			if (lava.col != kSvTownHidden && !svTownIsFinalCel(lava.cel))
				setInt(runtime, grandchild(kSvTownLava, i + 1), "cel", ++lava.cel);
		}
	} else
		_delay--;

	int32 placedRow = 0;
	int placed = dropBlock(runtime, placedRow);

	animateChopper(runtime, mouse, placed != kPlacedNone);
	updateShadow(runtime, mouse);
	animatePuffs(runtime);

	if (spawned && checkCollisions(runtime, _lavaRow - 1))
		return;

	if (_popupTimer < 1) {
		for (int i = 0; i < kNumPopups; i++) {
			if (_popups[i])
				hide(runtime, grandchild(kSvTownPoints, _popups[i]));
		}
	} else
		_popupTimer--;

	if (spawned)
		awardPoints(runtime, _lavaRow - 1);

	if (placed != kPlacedNone || spawned)
		depthSort(runtime, placed, placedRow);
}

void SvTownModifier::spawnRow(Runtime *runtime) {
	if (_lavaCount >= kNumLava || _lavaRow >= kNumRows)
		return;

	int idx = _level * 24 + _lavaRow;
	int32 first = _spreadRange[idx] - 1;
	int32 width = _spreadRange[idx + 12] + 2;

	// A cell gets lava if a finished piece in the row above flows into it
	for (int32 col = first; col < first + width && _lavaCount < kNumLava; col++) {
		for (int i = 0; i < kNumLava; i++) {
			const Piece &above = _lava[i];
			if (above.row != _lavaRow - 1)
				continue;
			if ((above.col == col && above.cel == 12) || (above.col == col + 1 && above.cel == 24) || (above.col == col - 1 && above.cel == 36)) {
				int n = _lavaCount;
				Piece &lava = _lava[n];
				lava.row = _lavaRow;
				lava.y = lava.row * kSvTownCell + 2;
				lava.col = col;
				lava.x = col * kSvTownCell + 9;

				int32 fork = -3;
				int32 dir = chooseDir(runtime, n, first, width, fork);
				addLava(runtime, col, _lavaRow, dir);

				// Some cells split into two directions
				if (fork >= 1 && fork <= 3 && _lavaCount < kNumLava)
					addLava(runtime, col, _lavaRow, fork);
				break;
			}
		}
	}

	_lavaRow++;
}

void SvTownModifier::addLava(Runtime *runtime, int32 col, int32 row, int32 dir) {
	Piece &lava = _lava[_lavaCount];
	Structural *elem = grandchild(kSvTownLava, _lavaCount + 1);
	lava.col = col;
	lava.row = row;
	lava.x = col * kSvTownCell + 9;
	lava.y = row * kSvTownCell + 2;
	lava.cel = dir * 12 - 11;
	setInt(runtime, elem, "cel", lava.cel);
	placeTopLeft(runtime, elem, lava.x, lava.y);
	_lavaCount++;
}

// Picks the direction of new lava piece n from the blocks around it. Returns the direction
// (1 down, 2 left, 3 right) and sets fork to a second direction, or -3 for none.
int32 SvTownModifier::chooseDir(Runtime *runtime, int n, int32 first, int32 width, int32 &fork) {
	const int32 c = _lava[n].col;
	const int32 r = _lava[n].row;

	bool l0 = false, l1 = false, l1u = false, r0 = false, r1 = false, r0u = false;
	for (int i = 0; i < kNumBlocks; i++) {
		const Piece &left = _leftBlocks[i];
		const Piece &right = _rightBlocks[i];
		if (left.col == c && left.row == r)
			l0 = true;
		if (left.col == c + 1 && left.row == r)
			l1 = true;
		if (left.col == c + 1 && left.row == r - 1)
			l1u = true;
		if (right.col == c + 1 && right.row == r)
			r1 = true;
		if (right.col == c && right.row == r)
			r0 = true;
		if (right.col == c && right.row == r - 1)
			r0u = true;
	}

	int choice;
	if (l0) {
		if (r1)
			choice = l1u ? (r0u ? 4 : 0) : (r0u ? 1 : 3);
		else if (r0u)
			choice = l1 ? (l1u ? 0 : 2) : (l1u ? 0 : 1);
		else
			choice = 0;
	} else if (!r1) {
		if (l1)
			choice = (r0 || r0u) ? 2 : 5;
		else if (r0)
			choice = l1u ? 2 : 6;
		else if (l1u)
			choice = 5;
		else
			choice = r0u ? 6 : 4;
	} else if (l1u)
		choice = r0 ? (r0u ? 1 : 2) : (r0u ? 1 : 0);
	else
		choice = 1;

	fork = -3;
	int32 dir = 1;
	switch (choice) {
	case 0:
		dir = 2;
		break;
	case 1:
		dir = 3;
		break;
	case 2:
		dir = 1;
		break;
	case 3:
		dir = 2;
		fork = 3;
		break;
	case 5:
		if (randomRange(runtime, 1, 1000) <= 220) {
			dir = 1;
			fork = 2;
		} else
			dir = randomRange(runtime, 1, 2);
		break;
	case 6:
		if (randomRange(runtime, 1, 1000) <= 220) {
			dir = 1;
			fork = 3;
		} else
			dir = (randomRange(runtime, 1, 2) == 1) ? 1 : 3;
		break;
	default: {
		// Free choice, weighted towards the middle of the screen, rerolled if it repeats a piece
		// already in this cell or makes two pieces spread into each other
		int attempts = 0;
		bool reroll = true;
		while (reroll) {
			reroll = false;
			fork = -3;

			const int32 *table;
			if (c == first || c == first + 1)
				table = _dirThresholds[0];
			else if (c == first + width || c == first + width - 1)
				table = _dirThresholds[1];
			else
				table = _dirThresholds[_lava[n].x < 318 ? 2 : 3];

			int32 rnd = randomRange(runtime, 1, 1000);
			int idx = 0;
			while (idx < 3 && rnd >= table[idx])
				idx++;

			if (idx < 3)
				dir = idx + 1;
			else {
				int32 r2 = randomRange(runtime, 1, 1000);
				if (r2 < _forkThresholds[0]) {
					dir = 2;
					fork = 3;
				} else if (r2 < _forkThresholds[1]) {
					dir = 1;
					fork = 3;
				} else {
					dir = 1;
					fork = 2;
				}
			}
			attempts++;

			for (int j = 0; j < n && attempts < 16; j++) {
				const Piece &other = _lava[j];
				if (other.cel == dir * 12 - 11 && other.col == c && other.row == r) {
					reroll = true;
					break;
				}
				if (dir == 2 && other.cel == 25 && other.col + 1 == c && other.row == r) {
					dir = (randomRange(runtime, 1, 2) == 1) ? 1 : 3;
					fork = -3;
				} else if (dir == 3 && other.cel == 13 && c + 1 == other.col && other.row == r) {
					dir = randomRange(runtime, 1, 2);
					fork = -3;
				}
			}
		}

		if (attempts >= 16)
			debug(2, "SvTown: Lava direction retries ran out");
	} break;
	}

	dir = CLIP<int32>(dir, 1, 3);
	if (fork >= 0)
		fork = CLIP<int32>(fork, 1, 3);
	return dir;
}

// Steam bursts out of the last row on the final level of a tier
void SvTownModifier::finalEruption(Runtime *runtime) {
	int slot = 0;
	for (int pass = 0; pass < 3 && slot < kNumSteam; pass++) {
		for (int i = 0; i < kNumLava; i++) {
			const Piece &lava = _lava[i];
			if (lava.col == kSvTownHidden || lava.row != _lavaRow - 1 || slot >= kNumSteam || _steam[slot].active)
				continue;

			Puff &steam = _steam[slot];
			Structural *elem = grandchild(kSvTownSteam, slot + 1);
			steam.x = randomRange(runtime, 1, 40) - 20 + lava.x + 44;
			steam.y = randomRange(runtime, 1, 40) - 20 + lava.y - 18;
			steam.cel = randomRange(runtime, 1, 3);
			setInt(runtime, elem, "cel", steam.cel);
			place(runtime, elem, steam.x, steam.y);
			steam.active = true;
			slot++;
		}
	}

	sendMessage(runtime, "PlaySFX6");
}

bool SvTownModifier::inArea(int32 col, int32 row) const {
	int idx = row + _level * 24;
	if (idx < 0 || idx + 12 >= kRangeTableSize)
		return false;

	int32 start = _placeRange[idx];
	int32 width = _placeRange[idx + 12];
	if (col < start - 1 || col > start + width || row < _levels[_level].startRow)
		return false;
	return !(_level % 5 == 0 && row >= _lastRow);
}

// Whether lava covers the cell for the block being carried
bool SvTownModifier::onLava(int32 col, int32 row) const {
	for (const Piece &lava : _lava) {
		if (lava.col == kSvTownHidden || lava.row != row)
			continue;

		if (_carry == kCarryLeft) {
			if ((lava.col == col && lava.cel <= 12) || (lava.col + 1 == col && lava.cel >= 25))
				return true;
		} else if (_carry == kCarryRight) {
			if ((lava.col + 1 == col && lava.cel <= 12) || (lava.col == col && lava.cel >= 13 && lava.cel <= 24))
				return true;
		}
	}
	return false;
}

// Houses (destroyed or not) and blocks occupy their cells; cars don't
bool SvTownModifier::occupied(int32 col, int32 row) const {
	for (const Home &home : _homes) {
		if (!svTownIsCar(home.type) && home.col == col && home.row == row)
			return true;
		if (svTownIsWide(home.type) && home.col + 1 == col && home.row == row)
			return true;
	}

	for (int i = 0; i < kNumBlocks; i++) {
		if ((_leftBlocks[i].col == col && _leftBlocks[i].row == row) || (_rightBlocks[i].col == col && _rightBlocks[i].row == row))
			return true;
	}
	return false;
}

bool SvTownModifier::dropCheck(Runtime *runtime, int32 col, int32 row, bool &lavaHit) {
	if (!inArea(col, row)) {
		// The block goes back to its pile
		_carry = kCarryNone;
		_chopState = (_chopState == 3) ? 1 : 0;
		hideShadow(runtime);
		return false;
	}

	if (row >= kNumRows)
		return false;

	lavaHit = onLava(col, row);
	return !occupied(col, row);
}

// Drops the carried block where the player clicked. Returns what was placed.
int SvTownModifier::dropBlock(Runtime *runtime, int32 &placedRow) {
	if (!_pendingDrop)
		return kPlacedNone;

	_pendingDrop = false;
	if (_carry != kCarryLeft && _carry != kCarryRight)
		return kPlacedNone;

	int32 col = MIN<int32>((_dropPoint.x - 16) / kSvTownCell, 16);
	int32 row = (_dropPoint.y - 10) / kSvTownCell;
	bool right = (_carry == kCarryRight);
	bool lavaHit = false;
	if (!dropCheck(runtime, col, row, lavaHit))
		return kPlacedNone;

	int placed = kPlacedNone;
	if (!lavaHit) {
		int32 &used = right ? _rightUsed : _leftUsed;
		if (used < kNumBlocks) {
			int b = (right ? _rightList : _leftList)[used];
			Piece &block = right ? _rightBlocks[b] : _leftBlocks[b];
			block.col = col;
			block.row = row;
			block.x = right ? col * kSvTownCell - 39 : col * kSvTownCell + 11;
			block.y = right ? row * kSvTownCell - 45 : row * kSvTownCell + 5;
			_carry = kCarryNone;
			sendMessage(runtime, "PlaySFX2");
			placeTopLeft(runtime, grandchild(right ? kSvTownRightBlocks : kSvTownLeftBlocks, b + 1), block.x, block.y);
			used++;
			placed = right ? kPlacedRight : kPlacedLeft;
		}
	} else {
		// A block dropped on lava breaks
		int32 &count = right ? _brokenRightCount : _brokenLeftCount;
		Piece *broken = right ? _brokenRight : _brokenLeft;
		bool taken = false;
		for (int i = 0; i < kNumBlocks; i++) {
			if (broken[i].col == col && broken[i].row == row)
				taken = true;
		}

		if (count < kNumBlocks && !taken) {
			Piece &block = broken[count];
			block.col = col;
			block.row = row;
			block.x = col * kSvTownCell + 11;
			block.y = row * kSvTownCell + 5;
			sendMessage(runtime, "PlaySFX2");
			placeTopLeft(runtime, grandchild(right ? kSvTownBrokenRight : kSvTownBrokenLeft, count + 1), block.x, block.y);
			_carry = kCarryNone;
			count++;
			placed = right ? kPlacedBrokenRight : kPlacedBrokenLeft;
		}
	}

	if (placed != kPlacedNone) {
		hideShadow(runtime);
		placedRow = row;
	}
	return placed;
}

// The helicopter faces the way the mouse moves and shows the block it carries
void SvTownModifier::animateChopper(Runtime *runtime, const Common::Point &mouse, bool placed) {
	bool turned = false;
	if (placed) {
		if (_chopState == 2) {
			_chopState = 0;
			_chopCel = 1;
			turned = true;
		} else if (_chopState == 3) {
			_chopState = 1;
			_chopCel = 5;
			turned = true;
		}
	}

	if (!turned) {
		if (mouse.x < _chopLastX + 32 && _chopState != 0 && _carry == kCarryNone) {
			_chopState = 0;
			_chopCel = 1;
		} else if (mouse.x > _chopLastX + 36 && _chopState != 1 && _carry == kCarryNone) {
			_chopState = 1;
			_chopCel = 5;
		} else if (_chopState != 2 && _carry == kCarryLeft) {
			_chopState = 2;
			_chopCel = 3;
		} else if (_chopState != 3 && _carry == kCarryRight) {
			_chopState = 3;
			_chopCel = 7;
		}
	}

	// Each state flips between two cels
	static const int32 kFirstCels[4] = {1, 5, 3, 7};
	if (_chopState >= 0 && _chopState < 4) {
		int32 firstCel = kFirstCels[_chopState];
		_chopCel = (_chopCel < firstCel + 1) ? firstCel + 1 : firstCel;
	}

	Structural *chopper = child(kSvTownChopper);
	setInt(runtime, chopper, "cel", _chopCel);

	DynamicValue pos;
	if (dlgMgrReadAttribute(runtime, this, chopper, "position", pos) && pos.getType() == DynamicValueTypes::kPoint)
		_chopLastX = pos.getPoint().x;
}

// Highlights the cell under the cursor when a block can be dropped there
void SvTownModifier::updateShadow(Runtime *runtime, const Common::Point &mouse) {
	if (_carry != kCarryLeft && _carry != kCarryRight)
		return;

	int32 col = (mouse.x - 16) / kSvTownCell;
	int32 row = (mouse.y - 10) / kSvTownCell;
	bool valid = inArea(col, row) && row >= _levels[_level].startRow - 1 && row < kNumRows && !onLava(col, row) && !occupied(col, row);

	if (!valid) {
		if (_shadowCol != kSvTownHidden)
			hideShadow(runtime);
		return;
	}

	if (col == _shadowCol && row == _shadowRow)
		return;

	Structural *shadow = child(kSvTownShadow);
	int32 cel = (_carry == kCarryLeft) ? 1 : 2;
	if (cel != _shadowCel) {
		_shadowCel = cel;
		setInt(runtime, shadow, "cel", cel);
	}

	_shadowCol = col;
	_shadowRow = row;
	if (_carry == kCarryLeft)
		placeTopLeft(runtime, shadow, col * kSvTownCell + 5, row * kSvTownCell + 11);
	else
		placeTopLeft(runtime, shadow, col * kSvTownCell + 18, row * kSvTownCell + 14);
}

void SvTownModifier::hideShadow(Runtime *runtime) {
	hide(runtime, child(kSvTownShadow));
	_shadowCol = _shadowRow = kSvTownHidden;
}

// Steam, smoke and burning trees
void SvTownModifier::animatePuffs(Runtime *runtime) {
	for (int i = 0; i < kNumSteam; i++) {
		Puff &steam = _steam[i];
		if (!steam.active)
			continue;

		Structural *elem = grandchild(kSvTownSteam, i + 1);
		if (++steam.cel < 9)
			setInt(runtime, elem, "cel", steam.cel);
		else {
			steam.active = false;
			place(runtime, elem, kSvTownHidden, 600);
		}
	}

	for (int i = 0; i < kNumSmoke; i++) {
		Puff &smoke = _smoke[i];
		if (!smoke.active)
			continue;

		Structural *elem = grandchild(kSvTownSmoke, i + 1);
		setInt(runtime, elem, "cel", smoke.cel);
		if (++smoke.cel > 10) {
			smoke.active = false;
			hide(runtime, elem);
		}
	}

	// Trees the lava reached burn down. Unused trees run through their cels too, as in the original.
	for (int i = 0; i < kNumTrees; i++) {
		Home &tree = _trees[i];
		if (!tree.active && tree.cel < 8)
			setInt(runtime, grandchild(kSvTownTrees, i + 1), "cel", ++tree.cel);
	}
}

// Lava in the row just spawned destroys homes and sets trees on fire. Returns true if the game is lost.
bool SvTownModifier::checkCollisions(Runtime *runtime, int32 row) {
	bool soundPlayed = false;

	for (const Piece &lava : _lava) {
		if (lava.col == kSvTownHidden)
			continue;

		bool downOrRight = (lava.cel < 13 || lava.cel > 24);
		bool downOrLeft = (lava.cel >= 1 && lava.cel <= 24);

		for (int i = 0; i < kNumHomes; i++) {
			Home &home = _homes[i];
			if (!home.active || home.row != row)
				continue;

			// Lava from the cell to the left hits unless it flows left
			bool hit = (lava.col == home.col - 1 && lava.row == home.row && downOrRight);
			bool hitCol = false;
			if (svTownIsWide(home.type)) {
				if (lava.col == home.col && lava.row == home.row)
					hit = true;
				hitCol = (lava.col == home.col + 1);
			} else if ((home.type >= 1 && home.type <= 4) || home.type == 8 || svTownIsCar(home.type))
				hitCol = (lava.col == home.col);
			else
				debug(2, "SvTown: Invalid home type %i in a collision", static_cast<int>(home.type));

			if (!hit && !(hitCol && lava.row == home.row && downOrLeft))
				continue;

			home.active = false;
			if (!soundPlayed) {
				sendMessage(runtime, "PlaySFX5");
				soundPlayed = true;
			}

			for (int s = 0; s < kNumSmoke; s++) {
				Puff &smoke = _smoke[s];
				if (smoke.active)
					continue;

				Structural *elem = grandchild(kSvTownSmoke, s + 1);
				smoke.active = true;
				smoke.x = home.x;
				smoke.y = home.y;
				smoke.cel = 1;
				setInt(runtime, elem, "cel", 1);
				placeTopLeft(runtime, elem, smoke.x, smoke.y);
				break;
			}

			// The destroyed home stays where it is
			home.x = kSvTownHidden;
			home.y = 600;
			setInt(runtime, grandchild(kSvTownHomes, i + 1), "cel", 2);

			// Only houses cost a chance
			if (!svTownIsCar(home.type)) {
				if (_lives < 2) {
					endGame(runtime);
					return true;
				}

				_lives--;
				setInt(runtime, child(kSvTownChances), "cel", _lives + 1);
			}
		}

		for (Home &tree : _trees) {
			if (!tree.active || tree.row != row)
				continue;

			if ((lava.col == tree.col - 1 && lava.row == tree.row && downOrRight) || (lava.col == tree.col && lava.row == tree.row && downOrLeft))
				tree.active = false;
		}
	}

	return false;
}

void SvTownModifier::endGame(Runtime *runtime) {
	debug(2, "SvTown: Game lost on level %i", static_cast<int>(_level));

	setInt(runtime, child(kSvTownChances), "cel", 1);
	sendMessage(runtime, "resetcursor");
	_state = kStateNewGame;
	_paused = true;
	_halted = true;
	_cursorIn = false;
	resetLayers(runtime);
	sendMessage(runtime, "GameOver");
	hide(runtime, child(kSvTownChopper));
}

// Homes the lava front passed without hitting earn points
void SvTownModifier::awardPoints(Runtime *runtime, int32 row) {
	const int32 tier = _level / 5;
	bool houseSound = false;
	bool carSound = false;
	int popup = 0;
	int32 cel = 1;

	for (const Home &home : _homes) {
		if (!home.active || home.row != row)
			continue;

		bool *sound = nullptr;
		if (home.type >= 1 && home.type <= 4) {
			cel = 2;
			_score += (tier + 1) * 50;
			sound = &houseSound;
		} else if (home.type == 5 || home.type == 7 || home.type == 10) {
			cel = 3;
			_score += tier * 50 + 75;
			sound = &houseSound;
		} else if (home.type == 8 || home.type == 9 || home.type == 11) {
			cel = 4;
			_score += (tier + 2) * 50;
			sound = &houseSound;
		} else if (svTownIsCar(home.type)) {
			cel = 1;
			_score += (tier + 1) * 40;
			sound = &carSound;
		} else
			debug(2, "SvTown: Invalid home type %i when giving points", static_cast<int>(home.type));

		if (sound && !*sound) {
			sendMessage(runtime, sound == &carSound ? "PlaySFX3" : "PlaySFX4");
			*sound = true;
		}

		sendMessage(runtime, "UpdateScore");
		_popupTimer = 24;

		// The original doesn't check for more than 16 awards in a tick
		if (popup < kNumPopups) {
			if (_popups[popup]) {
				Structural *elem = grandchild(kSvTownPoints, _popups[popup]);
				setInt(runtime, elem, "cel", cel);
				placeTopLeft(runtime, elem, home.x + 4, home.y - 36);
			}
			popup++;
		}
	}
}

// Lower rows are drawn in front. Only the rows around the lava front and the new block are sorted.
void SvTownModifier::depthSort(Runtime *runtime, int placed, int32 placedRow) {
	int32 lo = _lavaRow - 2;
	int32 hi = _lavaRow + 2;
	if (placed != kPlacedNone) {
		// For broken blocks the original reads the row of the next unused slot, so everything above is sorted
		if (placedRow < lo)
			lo = (placed == kPlacedLeft || placed == kPlacedRight) ? placedRow - 2 : kSvTownHidden - 2;
		if (placedRow > hi)
			hi = placedRow + 2;
	}

	setInt(runtime, child(kSvTownShadow), "layer", 40);

	int32 counters[17];
	for (int k = 0; k < 17; k++)
		counters[k] = (k < 16) ? (k + 1) * 10 : 0;

	auto sortLayer = [&](Structural *elem, int32 col, int32 row, int32 k) {
		if (col == kSvTownHidden || row < lo || row > hi)
			return;
		k = CLIP<int32>(k, 0, 16);
		setInt(runtime, elem, "layer", counters[k] + (row + 2) * 200);
		counters[k]++;
	};

	for (int i = 0; i < kNumHomes; i++) {
		if (!_homes[i].active)
			sortLayer(grandchild(kSvTownHomes, i + 1), _homes[i].col, _homes[i].row, _homes[i].col);
	}
	for (int i = 0; i < kNumLava; i++)
		sortLayer(grandchild(kSvTownLava, i + 1), _lava[i].col, _lava[i].row, _lava[i].col + 1);
	for (int i = 0; i < kNumTrees; i++)
		sortLayer(grandchild(kSvTownTrees, i + 1), _trees[i].col, _trees[i].row, _trees[i].col + 1);
	for (int i = 0; i < kNumHomes; i++) {
		if (_homes[i].active && svTownIsCar(_homes[i].type))
			sortLayer(grandchild(kSvTownHomes, i + 1), _homes[i].col, _homes[i].row, _homes[i].col);
	}
	for (int i = 0; i < kNumBlocks; i++) {
		sortLayer(grandchild(kSvTownBrokenLeft, i + 1), _brokenLeft[i].col, _brokenLeft[i].row, _brokenLeft[i].col + 1);
		sortLayer(grandchild(kSvTownBrokenRight, i + 1), _brokenRight[i].col, _brokenRight[i].row, _brokenRight[i].col);
		sortLayer(grandchild(kSvTownLeftBlocks, i + 1), _leftBlocks[i].col, _leftBlocks[i].row, _leftBlocks[i].col + 1);
		sortLayer(grandchild(kSvTownRightBlocks, i + 1), _rightBlocks[i].col, _rightBlocks[i].row, _rightBlocks[i].col);
	}
	for (int i = 0; i < kNumHomes; i++) {
		if (_homes[i].active && !svTownIsCar(_homes[i].type))
			sortLayer(grandchild(kSvTownHomes, i + 1), _homes[i].col, _homes[i].row, _homes[i].col + 1);
	}

	setInt(runtime, child(kSvTownChopper), "layer", kSvTownChopperLayer);
}

// Puts everything back in sequence at the end of a level, so the next level's layers don't collide
void SvTownModifier::resetLayers(Runtime *runtime) {
	int32 layer = 100;
	for (int i = 1; i <= kNumLava; i++)
		setInt(runtime, grandchild(kSvTownLava, i), "layer", layer++);
	for (int i = 1; i <= kNumTrees; i++)
		setInt(runtime, grandchild(kSvTownTrees, i), "layer", layer++);
	for (int i = 1; i <= kNumBlocks; i++) {
		setInt(runtime, grandchild(kSvTownLeftBlocks, i), "layer", layer++);
		setInt(runtime, grandchild(kSvTownRightBlocks, i), "layer", layer++);
		setInt(runtime, grandchild(kSvTownBrokenLeft, i), "layer", layer++);
		setInt(runtime, grandchild(kSvTownBrokenRight, i), "layer", layer++);
	}
	for (int i = 1; i <= kNumHomes; i++)
		setInt(runtime, grandchild(kSvTownHomes, i), "layer", layer++);
}

Common::SharedPtr<Modifier> SvTownModifier::shallowClone() const {
	Common::SharedPtr<SvTownModifier> clone(new SvTownModifier(*this));
	clone->_scheduledEvent.reset();
	clone->_running = false;
	return clone;
}

const char *SvTownModifier::getDefaultName() const {
	return "SvTown";
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
	if (attrib == "amminimized") {
		// Whether the player window is minimized
		result.setBool(false);
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
		{"resswitch", kAttribResSwitch},
		{"dofocusmsg", kAttribDoFocusMsg},
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
	case kAttribResSwitch:		// Screen resolution switching, which ScummVM handles
	case kAttribDoFocusMsg:		// Window focus messages
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

	Common::Array<int32> cues;

	for (DlgMgrClientModifier::Track &track : client->getTracks()) {
		if (track.isFinished)
			continue;

		Common::SharedPtr<Structural> target = track.target.lock();

		if (track.trackType == Data::KW::DlgMgrTrack::kTrackTypeCue) {
			if (frame >= track.startFrame) {
				track.isFinished = true;
				cues.push_back(track.cueValue);
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

	sendCuesBeforeDone(runtime, client, cues);
}

// Cue and done messages are sent immediately, which runs the last one sent
// first, so they are sent backwards: the done message (already sent) arrives
// after every cue, and the cues arrive in timeline order
void DlgMgrServerModifier::sendCuesBeforeDone(Runtime *runtime, DlgMgrClientModifier *client, const Common::Array<int32> &cues) {
	for (uint i = cues.size(); i > 0; i--)
		client->sendCue(runtime, cues[i - 1]);
}

void DlgMgrServerModifier::stopCurrentLine(Runtime *runtime, bool resetTracks, bool notifyDone) {
	DlgMgrClientModifier *client = getCurrentClient();
	_isPlaying = false;
	_currentClient.reset();
	_skipRequested = false;

	if (!client)
		return;

	Common::Array<int32> cues;

	for (DlgMgrClientModifier::Track &track : client->getTracks()) {
		Common::SharedPtr<Structural> target = track.target.lock();

		if (track.trackType == Data::KW::DlgMgrTrack::kTrackTypeSound) {
			if (track.isSoundPlaying)
				dlgMgrSendCommand(runtime, this, target.get(), EventIDs::kStop);
			track.isSoundPlaying = false;
		} else if (resetTracks) {
			if (track.trackType == Data::KW::DlgMgrTrack::kTrackTypeCue) {
				if (!track.isFinished)
					cues.push_back(track.cueValue);
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
	sendCuesBeforeDone(runtime, client, cues);
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
			// mTropolis 2.0 sends variables by reference (Bugs' bus sends iDialog)
			int32 value = 0;
			if (!msg->getValue().dereference().roundToInt(value) || value != _triggerValue)
				return kVThreadReturn;
		}

		start();
	} else if (_stopWhen.respondsTo(evt) || evt.eventType == EventIDs::kParentDisabled) {
		stop(runtime, evt.eventType != EventIDs::kParentDisabled);
	}

	return kVThreadReturn;
}

void DlgMgrClientModifier::start() {
	if (!_isQueued) {
		DlgMgrServerModifier *server = findServer();
		if (server) {
			_isQueued = true;
			server->enqueue(this);
		} else
			warning("DlgMgrClient '%s': No dialog manager server is running", getName().c_str());
	}
}

void DlgMgrClientModifier::stop(Runtime *runtime, bool notifyDone) {
	DlgMgrServerModifier *server = findServer();
	if (server && _isQueued)
		server->cancelClient(runtime, this, notifyDone);
}

bool DlgMgrClientModifier::readAttribute(MiniscriptThread *thread, DynamicValue &result, const Common::String &attrib) {
	if (attrib == "active") {
		result.setBool(_isQueued);
		return true;
	}

	return Modifier::readAttribute(thread, result, attrib);
}

MiniscriptInstructionOutcome DlgMgrClientModifier::writeRefAttribute(MiniscriptThread *thread, DynamicValueWriteProxy &writeProxy, const Common::String &attrib) {
	if (attrib == "active") {
		DynamicValueWriteFuncHelper<DlgMgrClientModifier, &DlgMgrClientModifier::scriptSetActive, true>::create(this, writeProxy);
		return kMiniscriptInstructionOutcomeContinue;
	}

	return Modifier::writeRefAttribute(thread, writeProxy, attrib);
}

// Setting active plays the client's lines or stops them, like its play and stop events (Magic
// School Bus Animals plays the classroom file cabinet's lines with it)
MiniscriptInstructionOutcome DlgMgrClientModifier::scriptSetActive(MiniscriptThread *thread, const DynamicValue &value) {
	DynamicValue converted;
	if (!value.convertToType(DynamicValueTypes::kBoolean, converted))
		return kMiniscriptInstructionOutcomeFailed;

	if (converted.getBool())
		start();
	else
		stop(thread->getRuntime(), true);

	return kMiniscriptInstructionOutcomeContinue;
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
	  _kwSmackerModifierFactory(this), _editCtrlModifierFactory(this), _fillModModifierFactory(this), _aviCtrlModifierFactory(this), _whaleMazeModifierFactory(this), _antMazeModifierFactory(this), _eyefollowerModifierFactory(this), _smkJumpModifierFactory(this), _balloonModifierFactory(this), _artModifierFactory(this), _svTownModifierFactory(this), _lLavaModifierFactory(this), _subModifierFactory(this), _landerModifierFactory(this), _roverRaceModifierFactory(this), _bugEatBugModifierFactory(this) {
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
	registrar->registerPlugInModifier("FillMod", &_fillModModifierFactory);
	registrar->registerPlugInModifier("AVICtrl", &_aviCtrlModifierFactory);

	// Mini-game modifiers from separate Magic School Bus plug-ins
	registrar->registerPlugInModifier("WhaleMaze", &_whaleMazeModifierFactory);
	registrar->registerPlugInModifier("AntMaze", &_antMazeModifierFactory);
	registrar->registerPlugInModifier("Eyefollower", &_eyefollowerModifierFactory);
	registrar->registerPlugInModifier("SmkJump", &_smkJumpModifierFactory);
	registrar->registerPlugInModifier("Balloon", &_balloonModifierFactory);
	registrar->registerPlugInModifier("Art", &_artModifierFactory);
	registrar->registerPlugInModifier("SvTown", &_svTownModifierFactory);
	registrar->registerPlugInModifier("LLava", &_lLavaModifierFactory);
	registrar->registerPlugInModifier("Sub", &_subModifierFactory);
	registrar->registerPlugInModifier("Lander", &_landerModifierFactory);
	registrar->registerPlugInModifier("RoverRace", &_roverRaceModifierFactory);
	registrar->registerPlugInModifier("BugEatBug", &_bugEatBugModifierFactory);
}

} // End of namespace KW

namespace PlugIns {

Common::SharedPtr<PlugIn> createKnowWonder() {
	return Common::SharedPtr<PlugIn>(new KW::KnowWonderPlugIn());
}

} // End of namespace PlugIns

} // End of namespace MTropolis
