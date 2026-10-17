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

#ifndef MSPAM_DETECTION_H
#define MSPAM_DETECTION_H

#include "engines/advancedDetector.h"

namespace MSPAM {

enum MSPAMDebugChannels {
	kDebugLoading = 1,
	kDebugGraphics,
	kDebugLayout,
	kDebugActions,
	kDebugInput,
	kDebugScript,
	kDebugSound
};

extern const PlainGameDescriptor mspamGames[];

extern const ADGameDescription gameDescriptions[];

} // End of namespace MSPAM

class MSPAMMetaEngineDetection : public AdvancedMetaEngineDetection<ADGameDescription> {
	static const DebugChannelDef debugFlagList[];

public:
	MSPAMMetaEngineDetection();
	~MSPAMMetaEngineDetection() override {}

	const char *getName() const override {
		return "mspam";
	}

	const char *getEngineName() const override {
		return "Microsoft Picture Animation Manager";
	}

	const char *getOriginalCopyright() const override {
		return "The Magic School Bus Explores the Solar System (C) Microsoft 1994";
	}

	const DebugChannelDef *getDebugChannels() const override {
		return debugFlagList;
	}
};

#endif // MSPAM_DETECTION_H
