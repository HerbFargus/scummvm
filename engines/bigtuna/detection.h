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

#ifndef BIGTUNA_DETECTION_H
#define BIGTUNA_DETECTION_H

#include "engines/advancedDetector.h"

namespace BigTuna {

enum BigTunaDebugChannels {
	kDebugLoading = 1,
	kDebugScore,
	kDebugSound,
};

// The first player (Just Me and My Mom): 10-byte bitmap headers, and
// labels stored as score byte offsets in resource 3.
enum BigTunaGameFlags {
	GF_IM2_V1 = 1 << 0,
};

extern const PlainGameDescriptor bigTunaGames[];

extern const ADGameDescription gameDescriptions[];

} // End of namespace BigTuna

class BigTunaMetaEngineDetection : public AdvancedMetaEngineDetection<ADGameDescription> {
	static const DebugChannelDef debugFlagList[];

public:
	BigTunaMetaEngineDetection();
	~BigTunaMetaEngineDetection() override {}

	const char *getName() const override {
		return "bigtuna";
	}

	const char *getEngineName() const override {
		return "Big Tuna";
	}

	const char *getOriginalCopyright() const override {
		return "Big Tuna New Media (C) 1996-2001";
	}

	const DebugChannelDef *getDebugChannels() const override {
		return debugFlagList;
	}
};

#endif // BIGTUNA_DETECTION_H
