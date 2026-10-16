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

#ifndef YAGA_PYTHONTEST_H
#define YAGA_PYTHONTEST_H

#include "common/str.h"

namespace Yaga {

// Runs every .pyc in a folder with the Python VM and writes what each prints
// (stdout and stderr, in order) to <name>.out in the output folder. Used to
// check the VM against CPython 2.2, run with the config key
// yaga_python_test=<folder> (and optionally yaga_python_test_out=<folder>).
void runPythonTests(const Common::String &dir, const Common::String &outDir);

} // End of namespace Yaga

#endif // YAGA_PYTHONTEST_H
