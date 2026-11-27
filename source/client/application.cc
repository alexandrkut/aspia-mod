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

#include <QIcon>
#include <QLocale>
#include <QUuid>

#if defined(Q_OS_WINDOWS)
#include <qt_windows.h>
#endif // defined(Q_OS_WINDOWS)

#include "version.h"
#include "base/build_config.h"
#include "base/logging.h"
#include "client/storage/database.h"
#include "client/storage/database_migration.h"
#include "client/storage/instance_settings.h"
#include "client/storage/user_settings.h"
#include "client/workers/router_worker.h"
#include "client/workers/sync_worker.h"
#include "client/workers/update_worker.h"

#if !defined(Q_OS_ANDROID)
#include "client/mcp/mcp_worker.h"
#endif // !defined(Q_OS_ANDROID)

namespace {

const char kActivateWindow[] = "activate_window";
const char kOpenHost[] = "open_host:";

} // namespace

bool Application::is_quick_support_ = false;

//--------------------------------------------------------------------------------------------------
// static
void Application::setQuickSupportMode(bool enable)
{
    is_quick_support_ = enable;
}

//--------------------------------------------------------------------------------------------------
// static
bool Application::isQuickSupportMode()
{
    return is_quick_support_;
}

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
            const QByteArray payload = message.mid(static_cast<qsizetype>(sizeof(kOpenHost) - 1));
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

    UserSettings settings;

    QString locale = resolveLocale(QLocale::system().bcp47Name());

#if !defined(Q_OS_ANDROID)
    locale = resolveLocale(settings.locale());
    if (locale != settings.locale())
    {
        LOG(INFO) << "Locale changed to:" << locale;
        settings.setLocale(locale);
    }
#endif // !defined(Q_OS_ANDROID)

    setLocale(locale);
    setTheme(settings.theme());

    // A portable Quick Support client keeps its own address book next to the executable. It must
    // not pick up the full client's old database from the user profile.
    if (!isQuickSupportMode())
    {
        const DatabaseMigration::Result migration = DatabaseMigration::migrate(
            DatabaseMigration::oldFilePath(), Database::directoryPath());
        if (migration == DatabaseMigration::Result::FAILED)
        {
            LOG(ERROR) << "Unable to migrate the old address book";
            migration_failed_ = true;
            return;
        }
    }

    addWorker(std::make_unique<RouterWorker>());

    // The exported client carries its account in the executable and has no other device to sync
    // with or to download a package from.
    if (!isQuickSupportMode())
    {
        addWorker(std::make_unique<SyncWorker>());

#if !defined(ASPIA_GOOGLE_PLAY)
        addWorker(std::make_unique<UpdateWorker>());
#endif // !defined(ASPIA_GOOGLE_PLAY)
    }

#if defined(Q_OS_ANDROID)
    const QList<Database::Info> list = Database::list();

    Database::select(list.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : list.first().id);
    loadSettings();
#endif // defined(Q_OS_ANDROID)
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
void Application::loadSettings()
{
    Database& db = Database::instance();

    InstanceSettings::select(db.guid());

#if !defined(Q_OS_ANDROID)
    if (UserSettings().isMcpEnabled())
        addWorker(std::make_unique<McpWorker>());
#endif // !defined(Q_OS_ANDROID)
}

//--------------------------------------------------------------------------------------------------
void Application::openHost(const QString& identifier, int session_type, const QString& username,
                           const QString& password)
{
    // The process that talks to the shell that just ran the command line holds the right to bring
    // a window to the foreground; the background instance that receives the request does not, and
    // its SetForegroundWindow would be refused. Hand the right over before asking.
#if defined(Q_OS_WINDOWS)
    AllowSetForegroundWindow(ASFW_ANY);
#endif

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
    // See openHost(): the foreground right belongs to the process the user just ran.
#if defined(Q_OS_WINDOWS)
    AllowSetForegroundWindow(ASFW_ANY);
#endif

    sendMessage(kActivateWindow);
}
