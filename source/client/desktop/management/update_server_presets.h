//
// Aspia Project
// Copyright (C) 2016-2026 Dmitry Chapyshev <dmitry@aspia.ru>
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <https://www.gnu.org/licenses/>.
//

#ifndef CLIENT_DESKTOP_MANAGEMENT_UPDATE_SERVER_PRESETS_H
#define CLIENT_DESKTOP_MANAGEMENT_UPDATE_SERVER_PRESETS_H

#include <QString>

namespace {

// The original Aspia update server and its public key. Used by the "switch to
// original server" preset button.
constexpr const char* kOriginalUpdateServer =
    "https://aspia.org/updates";
constexpr const char* kOriginalUpdatePublicKey =
    "b3e3f061842f1b29726578e09bcb1c5f13a5e065ce97c3ec53900ebd67cf760a";

} // namespace

#endif // CLIENT_DESKTOP_MANAGEMENT_UPDATE_SERVER_PRESETS_H
