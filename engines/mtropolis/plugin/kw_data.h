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

#ifndef MTROPOLIS_PLUGIN_KW_DATA_H
#define MTROPOLIS_PLUGIN_KW_DATA_H

#include "mtropolis/data.h"

namespace MTropolis {

namespace Data {

namespace KW {

//
// Known KnowWonder custom modifiers:
// - IdxIter
// - StrUtil
// - TicTacToe
// - DlgMgrServer, DlgMgrClient (Magic School Bus dialog manager)


struct StrUtilModifier : public PlugInModifierData {
protected:
	DataReadErrorCode load(PlugIn &plugIn, const PlugInModifier &prefix, DataReader &reader) override;
};

struct DlgMgrServerModifier : public PlugInModifierData {
	DlgMgrServerModifier();

	uint16 version;

	PlugInTypeTaggedValue enableWhen;
	PlugInTypeTaggedValue disableWhen;
	PlugInTypeTaggedValue captionTextEvent;		// Version 2+, sent with the caption text
	PlugInTypeTaggedValue captionFile;			// Version 2+, .ccd file
	PlugInTypeTaggedValue captionSpeakerEvent;	// Version 3+, sent with the speaker name
	PlugInTypeTaggedValue captionClearEvent;	// Version 3+
	PlugInTypeTaggedValue registryKey;			// Version 4+

protected:
	DataReadErrorCode load(PlugIn &plugIn, const PlugInModifier &prefix, DataReader &reader) override;
};

struct KWSmackerModifier : public PlugInModifierData {
	PlugInTypeTaggedValue playWhen;
	PlugInTypeTaggedValue stopWhen;
	PlugInTypeTaggedValue unknown1Event;
	PlugInTypeTaggedValue unknown2Label;
	PlugInTypeTaggedValue filePath;
	PlugInTypeTaggedValue unknown3;	// Null or boolean, sometimes absent

protected:
	DataReadErrorCode load(PlugIn &plugIn, const PlugInModifier &prefix, DataReader &reader) override;
};

struct EditCtrlModifier : public PlugInModifierData {
protected:
	DataReadErrorCode load(PlugIn &plugIn, const PlugInModifier &prefix, DataReader &reader) override;
};

// FillMod starts with its own u16 revision, which the coded size
// doesn't count, so its data is read value by value without using it.
struct FillModModifier : public PlugInModifierData {
	PlugInTypeTaggedValue enableWhen;
	PlugInTypeTaggedValue disableWhen;

protected:
	DataReadErrorCode load(PlugIn &plugIn, const PlugInModifier &prefix, DataReader &reader) override;
};

// Game-specific modifiers from separate plug-ins (mini-games), which only
// store enable and disable events.
struct GameModifier : public PlugInModifierData {
	PlugInTypeTaggedValue enableWhen;
	PlugInTypeTaggedValue disableWhen;

protected:
	DataReadErrorCode load(PlugIn &plugIn, const PlugInModifier &prefix, DataReader &reader) override;
};

// DlgMgrClient data is written by the plug-in itself in big-endian order,
// using a reduced set of value types.
struct DlgMgrValue {
	DlgMgrValue();

	bool load(DataReader &reader);

	uint16 type;	// Same type codes as PlugInTypeTaggedValue
	int32 value1;	// Integer, boolean, event ID or label ID
	int32 value2;	// Event info or label super group ID
};

struct DlgMgrBlob {
	bool load(DataReader &reader);

	Common::Array<uint8> data;
};

struct DlgMgrTrack {
	enum TrackType {
		kTrackTypeAnimation = 6,	// Sets the target's cel from a frame list or a looping cel range
		kTrackTypeSound = 7,		// Plays the target sound element
		kTrackTypeCue = 8,			// Sends the client's cue message
	};

	DlgMgrTrack();

	bool load(DataReader &reader);

	int32 loopStartCel;
	int32 loopEndCel;
	int32 unknown1;
	int32 startFrame;
	int32 endFrame;
	int32 unknown2;

	// Object references, as a GUID followed by up to 8 ancestor GUIDs
	int32 targetGUID;
	int32 targetPath[8];
	int32 sourceGUID;	// Sound tracks copy this element's asset to the target
	int32 sourcePath[8];

	int32 trackType;
	int32 hideAtEnd;
	int32 preload;
	int32 unknown3;
	int32 cueValue;

	bool hasMedia;
	int32 mediaUnknown1;
	int32 mediaUnknown2;
	DlgMgrBlob mediaName;

	Common::Array<int32> frames;

	Common::Array<int32> childUnknowns;
	Common::Array<DlgMgrBlob> children;
};

struct DlgMgrClientModifier : public PlugInModifierData {
	DlgMgrClientModifier();

	uint16 version;

	DlgMgrValue playWhen;
	DlgMgrValue stopWhen;
	DlgMgrValue cueEvent;
	DlgMgrValue cueDestination;	// Label value, the label ID is the destination
	DlgMgrValue cueUnknown;

	Common::Array<DlgMgrTrack> tracks;

	DlgMgrValue triggerValue;			// Version 2+
	DlgMgrValue filterByTriggerValue;	// Version 2+
	DlgMgrValue doneEvent;				// Version 4+
	DlgMgrValue doneDestination;		// Version 4+
	DlgMgrValue doneWith;				// Version 5+

protected:
	DataReadErrorCode load(PlugIn &plugIn, const PlugInModifier &prefix, DataReader &reader) override;
};

} // End of namespace KW

} // End of namespace Data

} // End of namespace MTropolis

#endif
