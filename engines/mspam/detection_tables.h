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

namespace MSPAM {

const PlainGameDescriptor mspamGames[] = {
	{"msbsolar", "The Magic School Bus Explores the Solar System"},
	{ 0, 0 }
};

const ADGameDescription gameDescriptions[] = {
	// US Windows CD, Microsoft Home reissue (files dated November 1995)
	{
		"msbsolar",
		nullptr,
		AD_ENTRY4s("MSBSOLAR.EX_", "7b24c03ce16508f4c3f44f961f7bee53", 32544,
				   "MSBSOLAR.CMF", "7437002f84c079959b7f46744ceff422", 118043808,
				   "MSBSOLAR.MDF", "55e0b3cbe8e05338520b527411e2aeef", 75688,
				   "MSBSOLAR.TDF", "472937499287f4b988b3e292d74ceb37", 1024000),
		Common::EN_USA,
		Common::kPlatformWindows,
		ADGF_UNSTABLE,
		GUIO1(GUIO_NOMIDI)
	},

	// US Windows CD (1994, files dated October 1994). Same screens and media as the
	// Microsoft Home reissue above; only the player and the title DLL were rebuilt.
	{
		"msbsolar",
		nullptr,
		AD_ENTRY2s("MSBSOLAR.EX_", "cf87249715963720ba36c285112d3844", 32576,
				   "MSBSOLAR.TDF", "472937499287f4b988b3e292d74ceb37", 1024000),
		Common::EN_USA,
		Common::kPlatformWindows,
		ADGF_UNSTABLE,
		GUIO1(GUIO_NOMIDI)
	},

	AD_TABLE_END_MARKER
};

} // End of namespace MSPAM
