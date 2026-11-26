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
void Application::activateWindow()
{
    sendMessage(kActivateWindow);
}
