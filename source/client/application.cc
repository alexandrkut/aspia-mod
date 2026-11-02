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
#include "client/host_url.h"
#include "client/settings.h"
#include "client/workers/router_worker.h"
#include "client/workers/update_worker.h"

#if !defined(Q_OS_ANDROID)
#include "client/mcp/mcp_worker.h"
#endif // !defined(Q_OS_ANDROID)

#include <QIcon>
#include <QList>

#if defined(Q_OS_WINDOWS)
#include <QDir>
#include <QSettings>
#endif // defined(Q_OS_WINDOWS)

#if defined(Q_OS_MACOS)
#include <QFileOpenEvent>
#endif // defined(Q_OS_MACOS)

#if defined(Q_OS_ANDROID)
#include <QJniEnvironment>
#include <QJniObject>
#endif // defined(Q_OS_ANDROID)

namespace {

const char kActivateWindow[] = "activate_window";
const char kOpenUrl[] = "open_url:";
const char kOpenHost[] = "open_host:";

#if defined(Q_OS_ANDROID)
//--------------------------------------------------------------------------------------------------
// Called by ClientActivity.onNewIntent on the Android UI thread when the running application is
// asked to open an aspia:// link.
void onNativeUrlOpened(JNIEnv* /* env */, jclass /* clazz */, jstring url)
{
    QString url_string = QJniObject(url).toString();
    QMetaObject::invokeMethod(Application::instance(), "sig_urlOpened", Qt::QueuedConnection,
                              Q_ARG(QString, url_string));
}
#endif // defined(Q_OS_ANDROID)

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
        else if (message.startsWith(kOpenUrl))
        {
            emit sig_urlOpened(QString::fromUtf8(message.mid(static_cast<qsizetype>(qstrlen(kOpenUrl)))));
        }
        else if (message.startsWith(kOpenHost))
        {
            // "<base64 identifier>:<session_type>:<base64 user name>:<base64 password>". The
            // trailing fields are empty when the record keeps its own credentials. A payload that
            // does not parse is not an answer to anything: it is dropped like any other message
            // the peer made up.
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

            // Half a pair is no pair: the receiver would fall back to the stored credentials, which
            // is not what the sender meant.
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

#if defined(Q_OS_WINDOWS)
    registerUrlHandler();
#endif // defined(Q_OS_WINDOWS)

#if defined(Q_OS_ANDROID)
    QJniEnvironment jni;
    const JNINativeMethod methods[] = {
        { "nativeUrlOpened", "(Ljava/lang/String;)V", reinterpret_cast<void*>(onNativeUrlOpened) }};
    if (!jni.registerNativeMethods("org/aspia/client/ClientActivity", methods, 1))
        LOG(ERROR) << "Unable to register ClientActivity native methods";
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
void Application::activateWindow()
{
    sendMessage(kActivateWindow);
}

//--------------------------------------------------------------------------------------------------
void Application::openUrl(const QString& url)
{
    sendMessage(kOpenUrl + url.toUtf8());
}

//--------------------------------------------------------------------------------------------------
void Application::openHost(const QString& identifier, int session_type, const QString& username,
                           const QString& password)
{
    // Every field but the session type is text that may hold the separator the fields are written
    // with, so all three are base64-encoded. An empty pair reads back as "the credentials of the
    // record", which is what an empty field means here.
    const QByteArray payload =
        identifier.toUtf8().toBase64() + ':' +
        QByteArray::number(session_type) + ':' +
        username.toUtf8().toBase64() + ':' +
        password.toUtf8().toBase64();

    sendMessage(kOpenHost + payload);
}

#if defined(Q_OS_MACOS)
//--------------------------------------------------------------------------------------------------
bool Application::event(QEvent* event)
{
    // macOS delivers clicked aspia:// links to the running instance as FileOpen events
    // (CFBundleURLTypes in Info.plist), not through the command line.
    if (event->type() == QEvent::FileOpen)
    {
        QString url = static_cast<QFileOpenEvent*>(event)->url().toString();
        if (HostUrl::isHostUrl(url))
        {
            LOG(INFO) << "URL open event:" << url;
            emit sig_urlOpened(url);
            return true;
        }
    }

    return GuiApplication::event(event);
}
#endif // defined(Q_OS_MACOS)

#if defined(Q_OS_WINDOWS)
//--------------------------------------------------------------------------------------------------
// static
void Application::registerUrlHandler()
{
    // Register the aspia:// scheme for the current user so links work even without the
    // installer (the MSI additionally registers the scheme machine-wide).
    QString executable = QDir::toNativeSeparators(applicationFilePath());
    QString command = QString("\"%1\" \"%2\"").arg(executable, "%1");

    QSettings settings("HKEY_CURRENT_USER\\Software\\Classes\\aspia", QSettings::NativeFormat);
    if (settings.value("shell/open/command/Default").toString() == command)
        return;

    settings.setValue("Default", "URL:Aspia Protocol");
    settings.setValue("URL Protocol", "");
    settings.setValue("DefaultIcon/Default", QString("\"%1\",0").arg(executable));
    settings.setValue("shell/open/command/Default", command);
}
#endif // defined(Q_OS_WINDOWS)
