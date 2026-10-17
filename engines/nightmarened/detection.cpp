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

#include "base/plugins.h"
#include "nightmarened/detection.h"
#include "nightmarened/detection_tables.h"

const DebugChannelDef NightmareNedMetaEngineDetection::debugFlagList[] = {
	{ NightmareNed::kDebugLoading, "Loading", "Archive and resource loading" },
	{ NightmareNed::kDebugGraphics, "Graphics", "Backgrounds and sprites" },
	{ NightmareNed::kDebugVideo, "Video", "Smacker videos" },
	{ NightmareNed::kDebugSound, "Sound", "Sound effects and music" },
	DEBUG_CHANNEL_END
};

// The CD keeps everything below DATA\ (DATA\DATA\*.NAF, DATA\VIDEO\*.SMK, DATA\DATA32.Z)
static const char *const directoryGlobs[] = {
	"data",
	nullptr
};

NightmareNedMetaEngineDetection::NightmareNedMetaEngineDetection() : AdvancedMetaEngineDetection(
	NightmareNed::gameDescriptions, NightmareNed::nightmareNedGames) {
	_maxScanDepth = 3;
	_directoryGlobs = directoryGlobs;
	_flags = kADFlagMatchFullPaths;
}

REGISTER_PLUGIN_STATIC(NIGHTMARENED_DETECTION, PLUGIN_TYPE_ENGINE_DETECTION, NightmareNedMetaEngineDetection);
