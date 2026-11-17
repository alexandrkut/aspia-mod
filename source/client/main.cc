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

#include <QSysInfo>

#include "version.h"
#include "base/logging.h"
#include "base/sys_info.h"
#include "client/application.h"

#if defined(Q_OS_MACOS)
#include "base/process_util.h"
#endif // defined(Q_OS_MACOS)

#if defined(Q_OS_ANDROID)
#include <QCoreApplication>
#include <QJniObject>
#include <QWidget>

#include "client/android/main_window.h"
#include "common/android/message_dialog.h"
#else
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDir>
#include <QProcess>
#include <QTextStream>

#include <functional>

#if defined(Q_OS_WINDOWS)
#include <qt_windows.h>

#include <fcntl.h>
#include <io.h>
#endif
#include "base/auto_qpointer.h"
#include "base/crypto/secure_string.h"
#include "client/connect_request.h"
#include "client/storage/database.h"
#include "client/storage/master_password.h"
#include "client/storage/user_settings.h"
#include "client/storage/system_settings.h"
#include "client/desktop/app_lock.h"
#include "client/desktop/main_window.h"
#include "client/desktop/update_server_dialog.h"
#include "client/mcp/mcp_worker.h"
#include "client/storage/database.h"
#include "client/storage/database_migration.h"
#include "client/storage/master_password.h"
#include "client/storage/system_settings.h"
#include "client/storage/user_settings.h"
#include "common/desktop/msg_box.h"
#include "proto/peer.h"
#include "common/desktop/update_dialog.h"
#endif // defined(Q_OS_ANDROID)

#if !defined(Q_OS_ANDROID)
namespace {

//--------------------------------------------------------------------------------------------------
bool openOnlyDatabase()
{
    const QList<Database::Info> list = Database::list();
    if (list.size() != 1 || !list.first().auto_unlock)
        return false;

    Database::select(list.first().id);
    return MasterPassword::autoUnlock();
}

//--------------------------------------------------------------------------------------------------
void backupOnStartup()
{
    Database& db = Database::instance();
    if (!db.isBackupOnStartupEnabled())
        return;

    const QString directory = db.backupPath();

    switch (AutoBackup::run(db, directory, db.backupRetention()))
    {
        case Backup::Result::SUCCESS:
            break;

        case Backup::Result::FILE_ERROR:
            MsgBox::warning(nullptr, QApplication::translate(
                "Client", "Unable to create a backup in the directory \"%1\".")
                    .arg(QDir::toNativeSeparators(directory)));
            break;

        case Backup::Result::UNREADABLE_RECORD:
            MsgBox::warning(nullptr, QApplication::translate(
                "Client", "Unable to create a backup. Some records of the database are damaged. Fix or "
                          "delete them."));
            break;

        default:
            MsgBox::warning(nullptr, QApplication::translate("Client", "Unable to create a backup."));
            break;
    }
}

} // namespace
#endif // !defined(Q_OS_ANDROID)

//--------------------------------------------------------------------------------------------------
int main(int argc, char* argv[])
{
#if defined(Q_OS_ANDROID)
    // When the Qt event loop ends, Qt's Android platform calls exit(), which runs the C++ runtime's
    // global destructors (__cxa_finalize). Those tear down std::thread/std::mutex statics while
    // Android's own threads (notably the HWUI render threads "hwuiTask") are still running, so a
    // render thread locks an already-destroyed mutex and the process aborts with
    // "FORTIFY: pthread_mutex_lock called on a destroyed mutex" (SIGABRT). This is a long-standing
    // Qt-on-Android shutdown race. QT_ANDROID_NO_EXIT_CALL makes Qt skip that exit() call, so the
    // global destructors are not run and the race cannot occur (Android reclaims the process).
    // Refs:
    //   https://bugreports.qt.io/browse/QTBUG-85449
    //   https://bugreports.qt.io/browse/QTBUG-82617
    //   https://developernote.com/2022/03/crash-at-std-thread-and-std-mutex-destructors-on-android/
    qputenv("QT_ANDROID_NO_EXIT_CALL", "1");

    // Force the software (raster) widget backing store. The app uses no OpenGL widgets, so the RHI
    // compositor is only pulled in for translucent top-level surfaces (dialogs, bottom sheets, menus).
    // Its QOpenGLContext::makeCurrent aborts with a qFatal (SIGABRT) when the surface is momentarily
    // invalid - e.g. while the on-screen keyboard is shown during a repaint. Raster avoids OpenGL
    // entirely and removes that crash.
    qputenv("QT_WIDGETS_RHI", "0");
#endif // defined(Q_OS_ANDROID)

    Q_INIT_RESOURCE(common);
    Q_INIT_RESOURCE(common_translations);

    LoggingSettings logging_settings;

#if defined(Q_OS_ANDROID)
    // Android defaults to logcat only (LOG_TO_STDOUT). Also write a log file so it can be pulled from
    // devices whose logcat is restricted (e.g. Huawei), then point that file at the external app files
    // dir (/sdcard/Android/data/<package>/files/log) so adb can pull it without root.
    logging_settings.destination = LOG_TO_ALL;

    QJniObject context = QNativeInterface::QAndroidApplication::context();
    if (context.isValid())
    {
        QJniObject files_dir = context.callObjectMethod(
            "getExternalFilesDir", "(Ljava/lang/String;)Ljava/io/File;", nullptr);
        if (files_dir.isValid())
        {
            logging_settings.log_dir =
                files_dir.callObjectMethod<jstring>("getAbsolutePath").toString() + "/log";
        }
    }
#endif // defined(Q_OS_ANDROID)

    ScopedLogging scoped_logging(logging_settings);

    Application::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

#if defined(Q_OS_MACOS)
    // The update is installed by an instance launched through Authorization Services, and without the
    // identity of root the installation is refused.
    ProcessUtil::restartAsRoot(argv);
#endif // defined(Q_OS_MACOS)

#if !defined(Q_OS_ANDROID)
    if (argc == 2 && qstrcmp(argv[1], "--mcp") == 0)
    {
        QCoreApplication application(argc, argv);
        return McpWorker::runBridge();
    }

    if (argc == 3 || argc == 4)
    {
        auto option_value = [](const char* argument, const char* option)
        {
            size_t length = qstrlen(option);
            if (qstrncmp(argument, option, length) != 0)
                return QString();
            return QString::fromLocal8Bit(argument + length);
        };

        QString channel = option_value(argv[1], "--channel=");
        QString locale = option_value(argv[2], "--locale=");
        bool update_server = qstrcmp(argv[1], "--update-server") == 0;

        if ((!channel.isEmpty() || update_server) && !locale.isEmpty())
        {
#if defined(Q_OS_LINUX)
            qputenv("QT_QPA_PLATFORM", "xcb");
#endif // defined(Q_OS_LINUX)

            GuiApplication application(argc, argv);
            application.setLocale(locale);

            if (update_server)
            {
                UpdateServerDialog dialog;
                return dialog.exec() == QDialog::Accepted ? 0 : 1;
            }

            ProxySettings proxy;
            if (argc == 4)
            {
                std::optional<ProxySettings> argument = ProxySettings::proxyFromArgument(
                    option_value(argv[3], "--proxy="));
                if (!argument)
                {
                    LOG(ERROR) << "Invalid proxy argument";
                    return 2;
                }

                proxy = *argument;
            }

            SystemSettings settings;
            UpdateDialog dialog(settings.updateServer(), settings.updatePublicKey(), channel, "client",
                                proxy, UpdateDialog::Action::INSTALL);

            return dialog.exec() == QDialog::Accepted ?
                UpdateDialog::kInstalledExitCode : UpdateDialog::kClosedExitCode;
        }
    }
#endif // !defined(Q_OS_ANDROID)

    Application application(argc, argv);

    LOG(INFO) << "Version:" << ASPIA_VERSION_STRING << "(arch:" << QSysInfo::buildCpuArchitecture() << ")";
#if defined(GIT_CURRENT_BRANCH) && defined(GIT_COMMIT_HASH)
    LOG(INFO) << "Git branch:" << GIT_CURRENT_BRANCH;
    LOG(INFO) << "Git commit:" << GIT_COMMIT_HASH;
#endif
    LOG(INFO) << "OS:" << SysInfo::operatingSystemName()
              << "(version:" << SysInfo::operatingSystemVersion()
              <<  "arch:" << SysInfo::operatingSystemArchitecture() << ")";
    LOG(INFO) << "Qt version:" << QT_VERSION_STR;
    // The command line is logged for diagnostics, but passwords must never reach the log. Build a
    // copy with the values of --password and --master-password replaced by "***" (both the
    // "--flag value" and the "--flag=value" forms); the original arguments are left untouched.
    QStringList logged_arguments = application.arguments();
    for (int i = 0; i < logged_arguments.size(); ++i)
    {
        const QString& argument = logged_arguments.at(i);

        if (argument == QStringLiteral("--password") ||
            argument == QStringLiteral("--master-password"))
        {
            if (i + 1 < logged_arguments.size())
                logged_arguments[i + 1] = QStringLiteral("***");
        }
        else if (argument.startsWith(QStringLiteral("--password=")) ||
                 argument.startsWith(QStringLiteral("--master-password=")))
        {
            const int equals = argument.indexOf(QLatin1Char('='));
            logged_arguments[i] = argument.left(equals + 1) + QStringLiteral("***");
        }
    }

    LOG(INFO) << "Command line:" << logged_arguments;

    if (application.isMigrationFailed())
    {
        const QString message = QApplication::translate(
            "Client", "Unable to move the address book into the new format. The old address book is not "
                      "changed, and the next start tries again. The details are in the log.");

#if defined(Q_OS_ANDROID)
        QWidget window;
        window.showMaximized();
        MessageDialog::info(&window, QApplication::translate("Client", "Aspia Client"), message);
#else
        MsgBox::warning(nullptr, message);
#endif // defined(Q_OS_ANDROID)
        return 1;
    }

#if defined(Q_OS_ANDROID)
    AndroidMainWindow main_window;
    main_window.show();

    return application.exec();
#else
    QCommandLineOption select_database_option("select-database",
        QApplication::translate("Client", "Show the database selection dialog."));

    QCommandLineParser parser;
    parser.setApplicationDescription(QApplication::translate("Client", "Aspia Client"));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption connect_option(
        { QStringLiteral("connect"), QStringLiteral("c") },
        QApplication::translate("Client",
            "Connect to the host of the address book with this record ID or host ID, or with the "
            "name of a record, using the login and the password stored in it, or connect directly "
            "to a host by its address, which needs the login and the password given with "
            "--username and --password."),
        QApplication::translate("Client", "id"));

    const QCommandLineOption session_type_option(
        QStringLiteral("session-type"),
        QApplication::translate("Client",
            "Kind of the session to open: desktop, file, info, chat or terminal "
            "(default: the one from the settings of the application)."),
        QApplication::translate("Client", "type"));

    const QCommandLineOption username_option(
        { QStringLiteral("username"), QStringLiteral("u") },
        QApplication::translate("Client",
            "Log in with this user name instead of the one stored in the host. "
            "Requires --password."),
        QApplication::translate("Client", "name"));

    const QCommandLineOption password_option(
        QStringLiteral("password"),
        QApplication::translate("Client",
            "Log in with this password instead of the stored one. Requires --username. "
            "A password on a command line is visible to the other users of the machine."),
        QApplication::translate("Client", "password"));

    const QCommandLineOption database_option("database",
        QApplication::translate("Client",
            "Open the database with this name or ID instead of the selection dialog."),
        QApplication::translate("Client", "name"));

    const QCommandLineOption master_password_option("master-password",
        QApplication::translate("Client",
            "Unlock the database with this master password instead of asking for it. A password on "
            "a command line is visible to the other users of the machine."),
        QApplication::translate("Client", "password"));

    const QCommandLineOption list_databases_option("list-databases",
        QApplication::translate("Client", "Print the ID and the name of every database and exit."));

    parser.addOption(select_database_option);
    parser.addOption(connect_option);
    parser.addOption(session_type_option);
    parser.addOption(username_option);
    parser.addOption(password_option);
    parser.addOption(database_option);
    parser.addOption(list_databases_option);
    parser.addOption(master_password_option);
    parser.process(application);

    SecureString master_password;
    if (parser.isSet(master_password_option))
    {
        const QString raw = parser.value(master_password_option);
        master_password = SecureString(raw);
    }

    if (parser.isSet(list_databases_option))
    {
#if defined(Q_OS_WINDOWS)
        // A GUI-subsystem binary gets no standard handles from an interactive console, so the
        // list would go nowhere. Attach to the console of the shell that started us and point
        // stdout at it; under a redirection the handles are valid and are left alone.
        if (GetFileType(GetStdHandle(STD_OUTPUT_HANDLE)) == FILE_TYPE_UNKNOWN)
        {
            if (AttachConsole(ATTACH_PARENT_PROCESS))
            {
                FILE* stream = nullptr;
                freopen_s(&stream, "CONOUT$", "w", stdout);

                fputs("\n", stdout);
                fflush(stdout);
            }
        }
#endif
#if defined(Q_OS_WINDOWS)
        const HANDLE console_handle = reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(stdout)));
        DWORD console_mode = 0;
        if (console_handle != INVALID_HANDLE_VALUE &&
            GetConsoleMode(console_handle, &console_mode))
        {
            for (const Database::Info& info : Database::list())
            {
                const QString line = info.id + QChar('\t') + info.name + QStringLiteral("\r\n");
                DWORD written = 0;
                WriteConsoleW(console_handle, line.constData(), static_cast<DWORD>(line.size()),
                             &written, nullptr);
            }
            return 0;
        }
#endif
        QTextStream out(stdout);
        for (const Database::Info& info : Database::list())
            out << info.id << '\t' << info.name << Qt::endl;
        return 0;
    }

    ConnectRequest connect_request;
    QString start_url;
    QString parse_error;

    if (!ConnectRequest::parse(application.arguments().mid(1), &connect_request, &start_url, &parse_error))
    {
        LOG(ERROR) << "Invalid command line:" << parse_error;
        MsgBox::warning(nullptr, parse_error);
        return 2;
    }

    // Exactly one database open: the request goes to its instance without asking.
    if (!parser.isSet(database_option) && !parser.isSet(select_database_option) &&
        !connect_request.isEmpty())
    {
        QString open_id;
        int open_count = 0;
        for (const Database::Info& info : Database::list())
        {
            if (application.hasInstance(info.id)) { open_id = info.id; ++open_count; }
        }
        if (open_count == 1)
        {
            LOG(INFO) << "One database is open, forwarding the command line request to its instance";
            application.setInstanceKey(open_id);
            const int session_type =
                connect_request.sessionType() == proto::peer::SESSION_TYPE_UNKNOWN ?
                    proto::peer::SESSION_TYPE_DESKTOP : connect_request.sessionType();
            application.openHost(connect_request.identifier(), session_type,
                                 connect_request.username(),
                                 connect_request.password().toString());
            application.activateWindow();
            return 0;
        }
        // 0 or 2+ open: fall through to the existing logic (dialog as before).
    }

    if (parser.isSet(database_option) && !parser.isSet(select_database_option))
    {
        QString error;
        const QString id =
            Database::resolveId(parser.value(database_option), Database::list(), &error);
        if (id.isEmpty())
        {
            MsgBox::warning(nullptr, error);
            return 2;
        }

        if (application.hasInstance(id))
        {
            LOG(INFO) << "The database is already open, forwarding to its instance";
            application.setInstanceKey(id);

            if (!connect_request.isEmpty())
            {
                const int session_type =
                    connect_request.sessionType() == proto::peer::SESSION_TYPE_UNKNOWN ?
                        proto::peer::SESSION_TYPE_DESKTOP : connect_request.sessionType();
                application.openHost(connect_request.identifier(), session_type,
                                     connect_request.username(),
                                     connect_request.password().toString());
            }

            application.activateWindow();
            return 0;
        }

        Database::select(id);

        if (!Database::instance().isValid())
        {
            MsgBox::warning(nullptr, QApplication::translate(
                "Client", "Unable to open the database \"%1\".").arg(id));
            return 2;
        }

        if (!MasterPassword::isSet() || !MasterPassword::autoUnlock())
        {
            // A database without a master password has nothing to unlock: the option is
            // meaningless there and the usual flow (the dialog that asks to set a password)
            // takes over instead of reporting a bogus "invalid password".
            if (!master_password.isEmpty() && MasterPassword::isSet())
            {
                if (MasterPassword::unlock(master_password) != MasterPassword::Result::SUCCESS)
                {
                    MsgBox::warning(nullptr, QApplication::translate("Client", "Invalid master password."));
                    return 2;
                }
            }
            else if (AppLock::unlock(true, &connect_request,
                                     master_password.isEmpty() ? nullptr : &master_password) !=
                     AppLock::Result::UNLOCKED)
            {
                return 0;
            }
        }
    }
    else if (parser.isSet(select_database_option) || !openOnlyDatabase())
    {
        const QList<Database::Info> database_list = Database::list();

        if (!master_password.isEmpty() && !parser.isSet(select_database_option) &&
            database_list.size() == 1 && MasterPassword::isSet() &&
            !MasterPassword::autoUnlock())
        {
            Database::select(database_list.first().id);

            if (!Database::instance().isValid())
            {
                MsgBox::warning(nullptr, QApplication::translate(
                    "Client", "Unable to open the database \"%1\".").arg(database_list.first().id));
                return 2;
            }

            if (MasterPassword::unlock(master_password) != MasterPassword::Result::SUCCESS)
            {
                MsgBox::warning(nullptr, QApplication::translate("Client", "Invalid master password."));
                return 2;
            }
        }
        else if (AppLock::unlock(true, &connect_request,
                                 master_password.isEmpty() ? nullptr : &master_password) !=
                 AppLock::Result::UNLOCKED)
        {
            return 0;
        }
    }

    if (!connect_request.isEmpty() &&
        connect_request.sessionType() == proto::peer::SESSION_TYPE_UNKNOWN)
    {
        connect_request.setSessionType(UserSettings().sessionType());

        if (connect_request.sessionType() == proto::peer::SESSION_TYPE_UNKNOWN)
            connect_request.setSessionType(proto::peer::SESSION_TYPE_DESKTOP);
    }

    if (!connect_request.isEmpty())
    {
        LOG(INFO) << "Command line asks for host" << connect_request.identifier()
                  << "with session type" << connect_request.sessionType()
                  << (connect_request.hasCredentials() ?
                         ", credentials given on the command line" : "");
    }

    Database& db = Database::instance();

    application.setInstanceKey(db.guid());

    if (application.isRunning())
    {
        if (!connect_request.isEmpty())
        {
            LOG(INFO) << "Another instance is already running, forwarding host to it";
            application.openHost(connect_request.identifier(), connect_request.sessionType(),
                                 connect_request.username(),
                                 connect_request.password().toString());
        }
        else
        {
            LOG(INFO) << "Another instance is already running, activating its window";
            application.activateWindow();
        }
        return 0;
    }

    application.loadSettings();

    UserSettings().setLastDatabase(db.guid());

    if (!DatabaseMigration::finish(db, DatabaseMigration::oldFilePath()))
        LOG(ERROR) << "Unable to move the router of the old address book, the next start tries again";

    backupOnStartup();

    std::unique_ptr<MainWindow> main_window;

    // The lock destroys the window with everything it shows, and the unlock creates a new one.
    std::function<void()> create_main_window = [&]()
    {
        main_window = std::make_unique<MainWindow>();

        QObject::connect(&application, &Application::sig_windowActivated,
                         main_window.get(), &MainWindow::showAndActivate);
        QObject::connect(&application, &Application::sig_hostOpened, main_window.get(),
                         [main_window = main_window.get()](const QString& identifier, int session_type,
                                                           const QString& username, const QString& password)
        {
            main_window->connectToLocalHost(identifier, session_type, username, password);
        });
        QObject::connect(main_window.get(), &MainWindow::sig_lockRequested, &application, [&]()
        {
            // Until the new window there is no window on the screen, and closing the unlock dialog
            // would otherwise quit the application.
            const bool quit_on_last_window_closed = QApplication::quitOnLastWindowClosed();
            QApplication::setQuitOnLastWindowClosed(false);

            main_window.reset();

            if (AppLock::unlock(false) != AppLock::Result::UNLOCKED)
            {
                QApplication::quit();
                return;
            }

            create_main_window();
            QApplication::setQuitOnLastWindowClosed(quit_on_last_window_closed);
        }, Qt::QueuedConnection);
        QObject::connect(main_window.get(), &MainWindow::sig_changeDatabaseRequested, &application, [&]()
        {
            main_window.reset();
            application.setInstanceKey(QString());

            if (!QProcess::startDetached(QApplication::applicationFilePath(), { "--select-database" }))
                LOG(ERROR) << "Unable to start the database selection";

            QApplication::quit();
        }, Qt::QueuedConnection);

        main_window->show();
        main_window->activateWindow();
    };

    create_main_window();

    if (!connect_request.isEmpty())
    {
        QTimer::singleShot(0, main_window.get(), [window = main_window.get(), connect_request]()
        {
            window->connectToLocalHost(connect_request.identifier(), connect_request.sessionType(),
                                      connect_request.username(),
                                      connect_request.password().toString());
        });
    }

    return application.exec();
#endif // defined(Q_OS_ANDROID)
}
