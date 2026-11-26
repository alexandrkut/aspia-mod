//
// Aspia Project
// Copyright (C) 2016-2026 Dmitry Chapyshev <dmitry@aspia.ru>
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <https://www.gnu.org/licenses/>.
//

#ifndef BASE_VERSION_CONSTANTS_H
#define BASE_VERSION_CONSTANTS_H

#include <QVersionNumber>

extern const QVersionNumber kCurrentVersion;
extern const QVersionNumber kMinimumSupportedVersion;

// The oldest mod client a router of this line still accepts. A client of an older line is rejected
// even if its three compatibility numbers are formally supported, because the router cannot
// guarantee the protocol it speaks. Clients at or above this version are let in even when they are
// older than the router: mod 3.4.3.x clients talk to a 3.5.2 router without a problem.
extern const QVersionNumber kMinimumSupportedClientVersion;
extern const QVersionNumber kVersion_3_0_0;
extern const QVersionNumber kVersion_3_0_34;

// Returns the version as the three numbers that tell whether the programs of two builds are
// compatible with each other. The fourth number is the number of the build of the line of the
// versions, and it tells nothing about the compatibility: the builds of the same line of the
// versions work with each other.
QVersionNumber compatibilityVersion(const QVersionNumber& version);

// A router accepts a client whose compatibility version is at least the oldest accepted mod line.
// The fourth number of the client version is ignored: 3.4.3.4 and 3.4.3 are the same line here.
bool isClientVersionAccepted(const QVersionNumber& client_version);
extern const QVersionNumber kVersion_3_2_0;
extern const QVersionNumber kVersion_3_3_0;

#endif // BASE_VERSION_CONSTANTS_H
