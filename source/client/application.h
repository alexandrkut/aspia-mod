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

#ifndef CLIENT_APPLICATION_H
#define CLIENT_APPLICATION_H

#include "base/gui_application.h"

class Application final : public GuiApplication
{
    Q_OBJECT

public:
    Application(int& argc, char* argv[]);
    virtual ~Application() final;

    static Application* instance();

public slots:
    void activateWindow();

    // Asks the running instance to connect to the address book record |identifier| names, which is
    // a record ID or a host ID. An empty |username| with an empty |password| means the credentials
    // of the record. |session_type| is a proto::peer::SessionType value.
    void openHost(const QString& identifier, int session_type, const QString& username,
                  const QString& password);

signals:
    void sig_windowActivated();

    // The command line of a second instance asked for this record. |identifier| is what it named
    // the host by; |session_type| is a proto::peer::SessionType value, and the credentials are
    // empty when the record keeps its own.
    void sig_hostOpened(const QString& identifier, int session_type, const QString& username,
                        const QString& password);

private:
    Q_DISABLE_COPY_MOVE(Application)
};

#endif // CLIENT_APPLICATION_H
