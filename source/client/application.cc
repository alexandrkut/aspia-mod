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

#include "client/application.h"

#include "version.h"
#include "base/build_config.h"
#include "base/logging.h"
#include "client/database.h"
#include "client/settings.h"
#include "client/workers/router_worker.h"
#include "client/workers/update_worker.h"

#if !defined(Q_OS_ANDROID)
#include "client/mcp/mcp_worker.h"
#endif // !defined(Q_OS_ANDROID)

#include <QIcon>
#include <QList>

namespace {

const char kActivateWindow[] = "activate_window";
const char kOpenHost[] = "open_host:";

} // namespace

//--------------------------------------------------------------------------------------------------
Application::Application(int& argc, char* argv[])
    : GuiApplication(argc, argv)
{
    LOG(INFO) << "Ctor";

    setOrganizationName("Aspia");
    setApplicationName("Client");
    setApplicationVersion(ASPIA_VERSION_STRING);
#if !defined(Q_OS_MACOS)
    setWindowIcon(QIcon(":/img/aspia.ico"));
#endif // !defined(Q_OS_MACOS)

    connect(this, &Application::sig_messageReceived, this, [this](const QByteArray& message)
    {
        if (message.startsWith(kActivateWindow))
        {
            emit sig_windowActivated();
        }
        else if (message.startsWith(kOpenHost))
        {
            const QByteArray payload = message.mid(static_cast<qsizetype>(qstrlen(kOpenHost)));
            const QList<QByteArray> fields = payload.split(':');

            if (fields.size() != 4)
            {
                LOG(ERROR) << "Malformed open host message:" << payload;
                return;
            }

            bool type_ok = false;
            const int session_type = fields.at(1).toInt(&type_ok);
            const QString identifier = QString::fromUtf8(QByteArray::fromBase64(fields.at(0)));

            if (!type_ok || identifier.isEmpty())
            {
                LOG(ERROR) << "Malformed open host message:" << payload;
                return;
            }

            QString username;
            QString password;

            if (!fields.at(2).isEmpty())
                username = QString::fromUtf8(QByteArray::fromBase64(fields.at(2)));

            if (!fields.at(3).isEmpty())
                password = QString::fromUtf8(QByteArray::fromBase64(fields.at(3)));

            if (username.isEmpty() != password.isEmpty())
            {
                LOG(ERROR) << "Open host message with half a pair of credentials";
                return;
            }

            LOG(INFO) << "Connect to host requested from the command line:" << identifier
                      << "(session type:" << session_type
                      << (username.isEmpty() ? ", credentials of the record" : ", credentials given")
                      << ")";

            emit sig_hostOpened(identifier, session_type, username, password);
        }
        else
        {
            LOG(ERROR) << "Unhandled message";
        }
    });

    Settings settings;

    QString locale = resolveLocale(settings.locale());
    if (locale != settings.locale())
    {
        LOG(INFO) << "Locale changed to:" << locale;
        settings.setLocale(locale);
    }

    setLocale(locale);
    setTheme(settings.theme());

    addWorker(std::make_unique<RouterWorker>());
    addWorker(std::make_unique<UpdateWorker>());

#if !defined(Q_OS_ANDROID)
    if (Database::instance().isMcpEnabled())
        addWorker(std::make_unique<McpWorker>());
#endif // !defined(Q_OS_ANDROID)
}

//--------------------------------------------------------------------------------------------------
Application::~Application()
{
    LOG(INFO) << "Dtor";
}

//--------------------------------------------------------------------------------------------------
// static
Application* Application::instance()
{
    return static_cast<Application*>(QApplication::instance());
}

//--------------------------------------------------------------------------------------------------
void Application::openHost(const QString& identifier, int session_type, const QString& username,
                           const QString& password)
{
    const QByteArray payload =
        identifier.toUtf8().toBase64() + ':' +
        QByteArray::number(session_type) + ':' +
        username.toUtf8().toBase64() + ':' +
        password.toUtf8().toBase64();

    sendMessage(kOpenHost + payload);
}

//--------------------------------------------------------------------------------------------------
void Application::activateWindow()
{
    sendMessage(kActivateWindow);
}
