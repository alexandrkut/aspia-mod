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

#ifndef CLIENT_DESKTOP_MANAGEMENT_ROUTER_WORKSPACE_DIALOG_H
#define CLIENT_DESKTOP_MANAGEMENT_ROUTER_WORKSPACE_DIALOG_H

#include <QDialog>
#include <QHash>

#include <memory>

#include "base/shared_pointer.h"
#include "client/page_model.h"
#include "client/router_types.h"
#include "client/desktop/management/workspace_edit_model.h"

class QAbstractButton;
class QComboBox;
class QEvent;
class QListWidget;
class QToolButton;

namespace Ui {
class RouterWorkspaceDialog;
} // namespace Ui

namespace proto::router {
class HostResult;
class UserList;
class WorkspaceResult;
} // namespace proto::router

class RouterWorkspaceDialog final : public QDialog
{
    Q_OBJECT

public:
    // workspace_id == 0 means create mode; > 0 means modify mode.
    RouterWorkspaceDialog(qint64 router_id, qint64 workspace_id, QWidget* parent);
    ~RouterWorkspaceDialog() final;

protected:
    // QDialog implementation.
    void changeEvent(QEvent* event) final;

private slots:
    void onWorkspaceListReceived(const RouterWorkspaceList& list);
    void onUserListReceived(const proto::router::UserList& list);
    void onHostListReceived(const RouterHostList& list);
    void onHostsOutsideListReceived(const RouterHostList& list);
    void onHostResultReceived(const proto::router::HostResult& result);
    void onWorkspaceResultReceived(const proto::router::WorkspaceResult& result);
    void onButtonBoxClicked(QAbstractButton* button);
    void onAddClicked();
    void onRemoveClicked();
    void onHostAddClicked();
    void onHostRemoveClicked();

private:
    // The reply of a member lookup, one user per request.
    void applyMemberLookup(const proto::router::UserList& list, qint64 user_id);

    // Membership is an edit of the workspace record and applies on OK; the hosts are moved one by
    // one and apply right away, so a host operation needs a workspace to move the host into. In
    // create mode the first one creates the workspace from the name of the form and runs after
    // the reply.
    void moveHosts(const QList<RouterHost>& hosts, qint64 workspace_id);
    void runPendingHostMoves();

    // The record of the host of a list, empty when the pages no longer carry it.
    RouterHost hostById(quint64 host_id) const;

    void fetchUsers();
    void fetchMemberNames();
    void fetchHosts();
    void refetchLists();
    void rebuildLists();
    void rebuildHostLists();
    void updatePagination(QComboBox* combo, QToolButton* prev, QToolButton* next,
                          const PageModel& page);
    void updateButtonsState();
    void updateLoadingState();
    void updateArrowIcons();
    void updateHostSummary();

    // The name of the form, checked the way the router checks it. Empty when it is not usable.
    QString validatedName();

    std::unique_ptr<Ui::RouterWorkspaceDialog> ui;
    qint64 router_id_ = 0;
    qint64 entry_id_ = 0;
    bool is_admin_ = false;

    std::unique_ptr<WorkspaceEditModel> model_;

    PageModel users_page_;
    PageModel hosts_in_page_;
    // The hosts outside this workspace: the hosts of the other workspaces together with the
    // hosts no workspace claimed. A router that predates the outside mode answers the old
    // free-hosts request instead (see onHostsOutsideListReceived).
    PageModel hosts_out_page_;

    // The pages of hosts as they arrived; the hosts are not part of the model, because a host
    // operation is applied by the router before it is shown.
    QList<RouterHost> hosts_in_;
    QList<RouterHost> hosts_out_;

    // Staged membership changes for the hosts tab in modify mode: host id -> becomes member.
    // Empty means nothing pending; while non-empty pagination/auto-refresh are blocked.
    QHash<quint64, bool> staged_;

    // A batch of host operations waiting for the workspace of a create-mode dialog to be created,
    // or currently in flight.
    struct PendingHostMove
    {
        RouterHost host;
        qint64 workspace_id = 0;
    };
    QList<PendingHostMove> pending_host_moves_;
    bool pending_host_move_ = false;

    struct HostMoveBatch
    {
        int pending = 0;
        int failed = 0;
        int total = 0;
        std::string error_code;
    };
    SharedPointer<HostMoveBatch> host_move_batch_;

    bool closing_ = false;

    // Modify-mode OK chain: save the record first, then send the staged host batch.
    bool apply_hosts_on_save_ = false;
    // Close the dialog after the staged host batch started from OK finishes.
    bool close_dialog_after_host_batch_ = false;

    Q_DISABLE_COPY_MOVE(RouterWorkspaceDialog)
};

#endif // CLIENT_DESKTOP_MANAGEMENT_ROUTER_WORKSPACE_DIALOG_H
