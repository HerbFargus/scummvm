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
#include "mspam/detection.h"
#include "mspam/detection_tables.h"

const DebugChannelDef MSPAMMetaEngineDetection::debugFlagList[] = {
	{ MSPAM::kDebugLoading, "Loading", "Title, layout and media loading" },
	{ MSPAM::kDebugGraphics, "Graphics", "Palettes and PibLists" },
	{ MSPAM::kDebugLayout, "Layout", "Layout model and rendering" },
	{ MSPAM::kDebugActions, "Actions", "Triggers and action lists" },
	{ MSPAM::kDebugInput, "Input", "Mouse hit-testing" },
	DEBUG_CHANNEL_END
};

// The game runs from the disc's MSBSOLAR\ folder; pointing at the disc root works too
static const char *const directoryGlobs[] = {
	"msbsolar",
	nullptr
};

MSPAMMetaEngineDetection::MSPAMMetaEngineDetection() : AdvancedMetaEngineDetection(
	MSPAM::gameDescriptions, MSPAM::mspamGames) {
	_maxScanDepth = 2;
	_directoryGlobs = directoryGlobs;
}

REGISTER_PLUGIN_STATIC(MSPAM_DETECTION, PLUGIN_TYPE_ENGINE_DETECTION, MSPAMMetaEngineDetection);
