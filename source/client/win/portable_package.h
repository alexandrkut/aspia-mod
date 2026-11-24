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

#ifndef CLIENT_WIN_PORTABLE_PACKAGE_H
#define CLIENT_WIN_PORTABLE_PACKAGE_H

#include <QString>

#include <optional>

#include "base/build_config.h"
#include "base/crypto/secure_string.h"
#include "base/net/address.h"

// A self-contained client that connects to Quick Support hosts only. The router address and
// the account are baked into a resource of a copy of aspia_client.exe, so the exported file
// needs no installer and carries everything it needs to log in.
class PortableClientPackage
{
public:
    struct Settings
    {
        Address router_address { kDefaultRouterHostTcpPort };
        QString username;
        SecureString password;
    };

    // Copies the running client to |file_path| and embeds |settings| in it.
    static bool exportWithSettings(const QString& file_path, const Settings& settings);

    // Returns the settings embedded in the executable of the current process, if there are any.
    static std::optional<Settings> builtInSettings();

private:
    Q_DISABLE_COPY_MOVE(PortableClientPackage)
};

#endif // CLIENT_WIN_PORTABLE_PACKAGE_H
