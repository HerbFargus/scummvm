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

class DlgMgrServerModifier : public Modifier {
public:
	DlgMgrServerModifier();
	~DlgMgrServerModifier();

	bool load(const PlugInModifierLoaderContext &context, const Data::KW::DlgMgrServerModifier &data);

	bool respondsToEvent(const Event &evt) const override;
	VThreadState consumeMessage(Runtime *runtime, const Common::SharedPtr<MessageProperties> &msg) override;

	void disable(Runtime *runtime) override;

#ifdef MTROPOLIS_DEBUG_ENABLE
	const char *debugGetTypeName() const override { return "DlgMgrServer Modifier"; }
	SupportStatus debugGetSupportStatus() const override { return kSupportStatusPartial; }
	void debugInspect(IDebugInspectionReport *report) const override;
#endif

private:
	Common::SharedPtr<Modifier> shallowClone() const override;
	const char *getDefaultName() const override;

	Event _enableWhen;
	Event _disableWhen;
	Event _unknown1Event;
	Event _unknown2Event;
	Event _unknown3Event;

	Common::String _captionFile;
	Common::String _registryKey;
};

class DlgMgrClientModifier : public Modifier {
public:
	struct Track {
		Track();

		int32 trackType;
		Common::String mediaName;
		Common::Array<int32> frames;
	};

	DlgMgrClientModifier();
	~DlgMgrClientModifier();

	bool load(const PlugInModifierLoaderContext &context, const Data::KW::DlgMgrClientModifier &data);

	bool respondsToEvent(const Event &evt) const override;
	VThreadState consumeMessage(Runtime *runtime, const Common::SharedPtr<MessageProperties> &msg) override;

	void disable(Runtime *runtime) override;

#ifdef MTROPOLIS_DEBUG_ENABLE
	const char *debugGetTypeName() const override { return "DlgMgrClient Modifier"; }
	SupportStatus debugGetSupportStatus() const override { return kSupportStatusPartial; }
	void debugInspect(IDebugInspectionReport *report) const override;
#endif

private:
	Common::SharedPtr<Modifier> shallowClone() const override;
	const char *getDefaultName() const override;

	Event _playWhen;
	Event _unknown1Event;
	Event _unknown2Event;

	Common::Array<Track> _tracks;
};

class KnowWonderPlugIn : public MTropolis::PlugIn {
public:
	KnowWonderPlugIn();
	~KnowWonderPlugIn();

	void registerModifiers(IPlugInModifierRegistrar *registrar) const override;

private:
	PlugInModifierFactory<StrUtilModifier, Data::KW::StrUtilModifier> _strUtilModifierFactory;
	PlugInModifierFactory<DlgMgrServerModifier, Data::KW::DlgMgrServerModifier> _dlgMgrServerModifierFactory;
	PlugInModifierFactory<DlgMgrClientModifier, Data::KW::DlgMgrClientModifier> _dlgMgrClientModifierFactory;
};

} // End of namespace KW

} // End of namespace MTropolis

#endif
