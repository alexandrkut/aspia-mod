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

#ifndef CLIENT_DESKTOP_MAIN_WINDOW_H
#define CLIENT_DESKTOP_MAIN_WINDOW_H

#include <QByteArray>
#include <QMainWindow>
#include <QPointer>

#include <functional>
#include <memory>

#include "base/peer/host_id.h"

#include "client/desktop/tab.h"
#include "client/mcp/mcp_controller.h"

namespace Ui {
class MainWindow;
} // namespace Ui

namespace proto::peer {
enum SessionType : int;
} // namespace proto::peer

class AppLock;
class ClientWindow;
class Tab;
class ManagementTab;
class SearchDialog;
class QLineEdit;
class HostConfig;
class LocalHostConfig;

class MainWindow final
    : public QMainWindow,
      public McpController::MainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() final;

    bool hasSessions() const;

public slots:
    void showAndActivate();
    void connectToUrl(const QString& url);

    // Connects to the address book record |identifier| names, which is a record ID (digits) or the
    // ID of a host, or to the host |identifier| names by its address, which is connected to
    // directly. Called for the --connect argument of the command line, both for the instance
    // that was started with it and for the one another instance forwarded the request to. The
    // credentials of the record are used unless |username| and |password| carry a pair of their
    // own, which is what --username and --password produce. A host named by its address has no
    // record and no stored credentials, so the pair is required for it. |session_type| is a
    // proto::peer::SessionType value.
    void connectToLocalHost(const QString& identifier, int session_type, const QString& username,
                            const QString& password);

signals:
    // The state of the window is saved by then, and the window is expected to be destroyed.
    void sig_lockRequested();

private:
    // What an identifier of the command line was resolved to: a record of the local address book,
    // a host that lives only in the book of a router and has no record of its own here, or a host
    // that is in no book at all and is named by its address.
    struct ConnectTarget
    {
        qint64 entry_id = 0;
        qint64 router_id = 0;
        HostId host_id = kInvalidHostId;
        QString name;

        // The address of a host that is connected to directly. Empty when the identifier named
        // a record or a host of a router.
        QString address;
    };

    // Finds what |identifier| names, looking first for a record ID of the address book and then
    // for a host ID - in the local book first and in the books of the routers after it. An
    // identifier that is an address names a host that is in no book and is connected to directly.
    bool resolveTarget(const QString& identifier, ConnectTarget* target) const;

    // Asks the routers whether one of them has a host with the ID of the command line, and opens
    // the session through the one that does. Used when nothing stored here knows the ID: the
    // address book only holds the hosts of a router that were fetched into it at some point.
    void connectToRouterHost(const QString& identifier, proto::peer::SessionType session_type,
                             const QString& username, const SecureString& password);

    // The message of a host that is nowhere to be found. |router_offline| tells that the router
    // that could have had it did not come online, so the host is not known to be missing there.
    void reportUnknownHost(const QString& identifier, bool router_offline = false);

    // Runs |action| once the router is online, or right away when it already is or when the host
    // needs no router at all. A request from the command line arrives before any router connection
    // exists, and a session opened then is only told the router is offline. |on_timeout| is called
    // when the router does not come online in time: the request is left without an answer then,
    // and the caller is told about it.
    void runWhenRouterOnline(qint64 router_id, std::function<void()> action,
                             std::function<void()> on_timeout = std::function<void()>());

    // How long a connection asked for on the command line waits for the router of the host before
    // it is given up on.
    static constexpr int kRouterWaitTimeout = 60000;

    // How long a router is given to answer whether it has the host that was asked for. A router
    // that says nothing is counted as one that has no host, for a request of the command line is
    // not to hang with nothing to report.
    static constexpr int kRouterSearchTimeout = 30000;

protected:
    // QMainWindow implementation.
    void closeEvent(QCloseEvent* event) final;

    // McpController::MainWindow implementation.
    QList<McpController::ClientInfo> mcpClients() const final;
    McpController::DesktopWindow* mcpDesktopWindow(qint64 client_id) const final;
    McpController::FileTransferWindow* mcpFileTransferWindow(qint64 client_id) const final;
    McpController::TerminalWindow* mcpTerminalWindow(qint64 client_id) const final;
    McpController::SystemInfoWindow* mcpSystemInfoWindow(qint64 client_id) const final;
    McpController::ChatWindow* mcpChatWindow(qint64 client_id) const final;
    QString mcpClientDialog(qint64 client_id) const final;
    void mcpCloseClient(qint64 client_id) final;
    bool mcpShowClient(qint64 client_id) final;
    bool mcpAsksCredentials() const final;
    void mcpConnectLocalHost(qint64 entry_id, proto::peer::SessionType session_type) final;
    void mcpConnectRouterHost(const HostConfig& host, proto::peer::SessionType session_type) final;

private slots:
    void onAfterThemeChanged();
    void onQuickConnect();
    void onCredentials();
    void onSettings();
    void onLock();
    void onHelp();
    void onAbout();
    void onCurrentTabChanged(int index);
    void onCloseTab(int index);
    void onSearchTextChanged(const QString& text);
    void onFindAction();
    void onConnect(const HostConfig& host, proto::peer::SessionType session_type);
    void onTabDetachRequested(int index, const QPoint& global_pos);
    void onTabDragMove(const QPoint& global_pos);
    void onTabDragFinished(const QPoint& global_pos);
    void onTabFullscreenRequested(bool enabled);
    void onTabMinimizeRequested();
    void onTabShowRequested();
    void onAlwaysOnTop(bool checked);
    void onMcpConnector(bool enable);
    void onMcpClientsChanged(const QStringList& clients);
    void onLockRequested();

private:
    void saveWindowState();
    void addTab(Tab* tab, const QString& title, const QIcon& icon);
    bool tabBarHitTest(const QPoint& global_pos) const;
    void hideCloseButtonForTab(int index);
    Tab* tabAt(int index);
    bool activateTab(Tab::Type type);
    bool showTab(Tab* tab);
    ManagementTab* managementTab() const;
    ClientWindow* clientWindow(qint64 client_id) const;
    void showSearchDialog();
    void syncSearchField();
    void installTabActions(Tab* tab);
    void removeTabActions();
    void updateSeparatorVisibility();
    QMenu* menuForActionGroup(Tab::ActionRole group) const;

    std::unique_ptr<Ui::MainWindow> ui;

    QLineEdit* search_field_ = nullptr;
    QAction* search_action_ = nullptr;
    SearchDialog* search_dialog_ = nullptr;
    AppLock* app_lock_ = nullptr;
    Tab* active_tab_ = nullptr;
    QList<QPointer<QAction>> tab_toolbar_actions_;
    QList<QPair<QMenu*, QList<QPointer<QAction>>>> tab_menu_actions_;

    McpController* mcp_controller_ = nullptr;
    QString window_title_;

    Q_DISABLE_COPY_MOVE(MainWindow)
};

#endif // CLIENT_DESKTOP_MAIN_WINDOW_H
