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

#ifndef ASPIA_VERSION_H
#define ASPIA_VERSION_H

#define STR_HELPER_INTERNAL(x) #x
#define STR_INTERNAL(x) STR_HELPER_INTERNAL(x)

// The suffix is empty for a released build and something like "-mod" for a modified one. It is
// only ever appended to the two printed forms below: ASPIA_VERSION, the numeric one that anything
// comparing versions works with, carries nothing of it.
#ifndef ASPIA_VERSION_SUFFIX
#    define ASPIA_VERSION_SUFFIX ""
#endif

#define ASPIA_VERSION_STRING                 \
    STR_INTERNAL(ASPIA_VERSION_MAJOR) "."    \
    STR_INTERNAL(ASPIA_VERSION_MINOR) "."    \
    STR_INTERNAL(ASPIA_VERSION_PATCH) "."    \
    STR_INTERNAL(ASPIA_VERSION_BUILD)        \
    ASPIA_VERSION_SUFFIX

#define ASPIA_VERSION_SHORT_STRING           \
    STR_INTERNAL(ASPIA_VERSION_MAJOR) "."    \
    STR_INTERNAL(ASPIA_VERSION_MINOR) "."    \
    STR_INTERNAL(ASPIA_VERSION_PATCH)        \
    ASPIA_VERSION_SUFFIX

#define ASPIA_VERSION                        \
    ASPIA_VERSION_MAJOR,ASPIA_VERSION_MINOR, \
    ASPIA_VERSION_PATCH,ASPIA_VERSION_BUILD

#endif // ASPIA_VERSION_H
