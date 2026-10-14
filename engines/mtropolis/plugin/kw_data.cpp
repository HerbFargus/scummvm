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

#include "mtropolis/plugin/kw_data.h"

namespace MTropolis {

namespace Data {

namespace KW {

DataReadErrorCode StrUtilModifier::load(PlugIn &plugIn, const PlugInModifier &prefix, DataReader &reader) {
	if (prefix.plugInRevision != 0)
		return kDataReadErrorUnsupportedRevision;

	// Seemingly no data to load

	return kDataReadErrorNone;
}

// mTropolis 2.0 plug-in data starts with a copy of the plug-in revision
static bool loadV2RevisionPrefix(const PlugInModifier &prefix, DataReader &reader) {
	if (prefix.getRevision() < 2000 || prefix.plugInRevision == 0)
		return true;

	uint16 revision = 0;
	return reader.readU16(revision) && revision == prefix.plugInRevision;
}

DataReadErrorCode KWSmackerModifier::load(PlugIn &plugIn, const PlugInModifier &prefix, DataReader &reader) {
	if (prefix.plugInRevision != 1)
		return kDataReadErrorUnsupportedRevision;

	int64 endPos = reader.tell() + prefix.subObjectSize;

	if (!loadV2RevisionPrefix(prefix, reader) || !playWhen.load(reader) || !stopWhen.load(reader) || !unknown1Event.load(reader)
		|| !unknown2Label.load(reader) || !filePath.load(reader))
		return kDataReadErrorReadFailed;

	if (reader.tell() < endPos && !unknown3.load(reader))
		return kDataReadErrorReadFailed;

	return kDataReadErrorNone;
}

DataReadErrorCode EditCtrlModifier::load(PlugIn &plugIn, const PlugInModifier &prefix, DataReader &reader) {
	if (prefix.plugInRevision != 1)
		return kDataReadErrorUnsupportedRevision;

	return kDataReadErrorNone;
}

DataReadErrorCode GameModifier::load(PlugIn &plugIn, const PlugInModifier &prefix, DataReader &reader) {
	if (prefix.plugInRevision != 1)
		return kDataReadErrorUnsupportedRevision;

	if (!loadV2RevisionPrefix(prefix, reader) || !enableWhen.load(reader) || !disableWhen.load(reader))
		return kDataReadErrorReadFailed;

	return kDataReadErrorNone;
}

DlgMgrServerModifier::DlgMgrServerModifier() : version(0) {
}

DataReadErrorCode DlgMgrServerModifier::load(PlugIn &plugIn, const PlugInModifier &prefix, DataReader &reader) {
	if (prefix.plugInRevision != 1)
		return kDataReadErrorUnsupportedRevision;

	if (!reader.readU16(version) || version < 1 || version > 4)
		return kDataReadErrorReadFailed;

	if (!enableWhen.load(reader) || !disableWhen.load(reader))
		return kDataReadErrorReadFailed;

	if (version >= 2 && (!captionTextEvent.load(reader) || !captionFile.load(reader)))
		return kDataReadErrorReadFailed;

	if (version >= 3 && (!captionSpeakerEvent.load(reader) || !captionClearEvent.load(reader)))
		return kDataReadErrorReadFailed;

	if (version >= 4 && !registryKey.load(reader))
		return kDataReadErrorReadFailed;

	return kDataReadErrorNone;
}

static bool readS32BE(DataReader &reader, int32 &value) {
	uint8 bytes[4];
	if (!reader.readBytes(bytes))
		return false;

	value = static_cast<int32>(READ_BE_UINT32(bytes));
	return true;
}

DlgMgrValue::DlgMgrValue() : type(PlugInTypeTaggedValue::kNull), value1(0), value2(0) {
}

bool DlgMgrValue::load(DataReader &reader) {
	uint8 typeBytes[2];
	if (!reader.readBytes(typeBytes))
		return false;

	type = READ_BE_UINT16(typeBytes);
	value1 = 0;
	value2 = 0;

	switch (type) {
	case PlugInTypeTaggedValue::kNull:
		return true;
	case PlugInTypeTaggedValue::kInteger:
		return readS32BE(reader, value1);
	case PlugInTypeTaggedValue::kBoolean: {
			uint8 boolBytes[2];
			if (!reader.readBytes(boolBytes))
				return false;
			value1 = READ_BE_UINT16(boolBytes);
			return true;
		}
	case PlugInTypeTaggedValue::kEvent:
	case PlugInTypeTaggedValue::kLabel:
		return readS32BE(reader, value1) && readS32BE(reader, value2);
	default:
		warning("DlgMgrClient: Unknown value type %x", static_cast<uint>(type));
		return false;
	}
}

static bool readIntValue(DataReader &reader, int32 &value) {
	DlgMgrValue v;
	if (!v.load(reader) || v.type != PlugInTypeTaggedValue::kInteger)
		return false;

	value = v.value1;
	return true;
}

bool DlgMgrBlob::load(DataReader &reader) {
	int32 size = 0;
	if (!readIntValue(reader, size) || size < 0)
		return false;

	data.resize(size);
	return size == 0 || reader.read(&data[0], size);
}

DlgMgrTrack::DlgMgrTrack()
	: loopStartCel(0), loopEndCel(0), unknown1(0), startFrame(0), endFrame(0), unknown2(0),
	  targetGUID(0), targetPath{0, 0, 0, 0, 0, 0, 0, 0}, sourceGUID(0), sourcePath{0, 0, 0, 0, 0, 0, 0, 0},
	  trackType(0), hideAtEnd(0), preload(0), unknown3(0), cueValue(0), hasMedia(false), mediaUnknown1(0), mediaUnknown2(0) {
}

bool DlgMgrTrack::load(DataReader &reader) {
	if (!readIntValue(reader, loopStartCel) || !readIntValue(reader, loopEndCel) || !readIntValue(reader, unknown1)
		|| !readIntValue(reader, startFrame) || !readIntValue(reader, endFrame) || !readIntValue(reader, unknown2))
		return false;

	if (!readIntValue(reader, targetGUID))
		return false;

	for (int32 &v : targetPath)
		if (!readIntValue(reader, v))
			return false;

	if (!readIntValue(reader, sourceGUID))
		return false;

	for (int32 &v : sourcePath)
		if (!readIntValue(reader, v))
			return false;

	if (!readIntValue(reader, trackType) || !readIntValue(reader, hideAtEnd) || !readIntValue(reader, preload)
		|| !readIntValue(reader, unknown3) || !readIntValue(reader, cueValue))
		return false;

	int32 hasMediaInt = 0;
	if (!readIntValue(reader, hasMediaInt))
		return false;

	hasMedia = (hasMediaInt != 0);
	if (hasMedia && (!readIntValue(reader, mediaUnknown1) || !readIntValue(reader, mediaUnknown2) || !mediaName.load(reader)))
		return false;

	int32 numFrames = 0;
	if (!readIntValue(reader, numFrames) || numFrames < 0)
		return false;

	frames.resize(numFrames);
	for (int32 &frame : frames)
		if (!readIntValue(reader, frame))
			return false;

	int32 numChildren = 0;
	if (!readIntValue(reader, numChildren) || numChildren < 0)
		return false;

	childUnknowns.resize(numChildren);
	children.resize(numChildren);
	for (int32 i = 0; i < numChildren; i++)
		if (!readIntValue(reader, childUnknowns[i]) || !children[i].load(reader))
			return false;

	return true;
}

DlgMgrClientModifier::DlgMgrClientModifier() : version(0) {
}

DataReadErrorCode DlgMgrClientModifier::load(PlugIn &plugIn, const PlugInModifier &prefix, DataReader &reader) {
	if (prefix.plugInRevision != 1)
		return kDataReadErrorUnsupportedRevision;

	uint8 versionBytes[2];
	if (!reader.readBytes(versionBytes))
		return kDataReadErrorReadFailed;

	version = READ_BE_UINT16(versionBytes);
	if (version < 1 || version > 5)
		return kDataReadErrorReadFailed;

	if (!playWhen.load(reader) || !stopWhen.load(reader) || !cueEvent.load(reader)
		|| !cueDestination.load(reader) || !cueUnknown.load(reader))
		return kDataReadErrorReadFailed;

	int32 numTracks = 0;
	if (!readIntValue(reader, numTracks) || numTracks < 0)
		return kDataReadErrorReadFailed;

	tracks.resize(numTracks);
	for (DlgMgrTrack &track : tracks)
		if (!track.load(reader))
			return kDataReadErrorReadFailed;

	if (version >= 2 && (!triggerValue.load(reader) || !filterByTriggerValue.load(reader)))
		return kDataReadErrorReadFailed;

	if (version >= 4 && (!doneEvent.load(reader) || !doneDestination.load(reader)))
		return kDataReadErrorReadFailed;

	if (version >= 5 && !doneWith.load(reader))
		return kDataReadErrorReadFailed;

	return kDataReadErrorNone;
}

} // End of namespace KW

} // End of namespace Data

} // End of namespace MTropolis
