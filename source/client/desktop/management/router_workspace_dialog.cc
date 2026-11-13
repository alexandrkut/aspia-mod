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

#include "client/desktop/management/router_workspace_dialog.h"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QComboBox>
#include <QEvent>
#include <QIcon>
#include <QListWidgetItem>
#include <QPushButton>
#include <QScrollBar>
#include <QSet>
#include <QSignalBlocker>
#include <QToolButton>

#include "base/logging.h"
#include "client/router_controller.h"
#include "client/settings.h"
#include "client/desktop/management/ui_router_workspace_dialog.h"
#include "common/desktop/msg_box.h"
#include "common/desktop/router_error.h"
#include "proto/router_admin.h"
#include "proto/router_client.h"
#include "proto/router_constants.h"

namespace {

constexpr qint64 kPageSize = 50;
constexpr qint64 kAnyGroup = -1;
constexpr qint64 kNoWorkspace = 0;

} // namespace

//--------------------------------------------------------------------------------------------------
RouterWorkspaceDialog::RouterWorkspaceDialog(
    qint64 router_id, qint64 workspace_id, QWidget* parent)
    : QDialog(parent),
      ui(std::make_unique<Ui::RouterWorkspaceDialog>()),
      router_id_(router_id),
      entry_id_(workspace_id),
      model_(std::make_unique<WorkspaceEditModel>(workspace_id))
{
    LOG(INFO) << "Ctor";
    ui->setupUi(this);
    updateArrowIcons();

    ui->list_hosts_available->setSelectionMode(QAbstractItemView::ExtendedSelection);
    ui->list_hosts_in_workspace->setSelectionMode(QAbstractItemView::ExtendedSelection);

    Settings settings;
    restoreGeometry(settings.dialogGeometry(objectName()));

    // Counts characters, the bound counts UTF-8 bytes. A non-ASCII name is caught before sending.
    ui->edit_name->setMaxLength(static_cast<int>(proto::router::kMaxEntryNameLength));

    users_page_.setPageSize(kPageSize);
    hosts_in_page_.setPageSize(kPageSize);
    hosts_out_page_.setPageSize(kPageSize);

    connect(ui->buttonbox, &QDialogButtonBox::clicked, this, &RouterWorkspaceDialog::onButtonBoxClicked);
    connect(ui->button_add, &QToolButton::clicked, this, &RouterWorkspaceDialog::onAddClicked);
    connect(ui->button_remove, &QToolButton::clicked, this, &RouterWorkspaceDialog::onRemoveClicked);
    connect(ui->button_host_add, &QToolButton::clicked, this, &RouterWorkspaceDialog::onHostAddClicked);
    connect(ui->button_host_remove, &QToolButton::clicked, this, &RouterWorkspaceDialog::onHostRemoveClicked);
    connect(ui->list_available, &QListWidget::itemSelectionChanged,
            this, &RouterWorkspaceDialog::updateButtonsState);
    connect(ui->list_with_access, &QListWidget::itemSelectionChanged,
            this, &RouterWorkspaceDialog::updateButtonsState);
    connect(ui->list_hosts_available, &QListWidget::itemSelectionChanged,
            this, &RouterWorkspaceDialog::updateButtonsState);
    connect(ui->list_hosts_in_workspace, &QListWidget::itemSelectionChanged,
            this, &RouterWorkspaceDialog::updateButtonsState);

    connect(ui->combo_users_page, &QComboBox::currentIndexChanged, this, [this](int index)
    {
        if (index < 0 || index == users_page_.currentPage())
            return;
        users_page_.setCurrentPage(index);
        fetchUsers();
    });
    connect(ui->button_users_prev, &QToolButton::clicked, this, [this]()
    {
        users_page_.setCurrentPage(users_page_.currentPage() - 1);
        fetchUsers();
    });
    connect(ui->button_users_next, &QToolButton::clicked, this, [this]()
    {
        users_page_.setCurrentPage(users_page_.currentPage() + 1);
        fetchUsers();
    });

    connect(ui->combo_hosts_in_page, &QComboBox::currentIndexChanged, this, [this](int index)
    {
        if (index < 0 || index == hosts_in_page_.currentPage())
            return;
        hosts_in_page_.setCurrentPage(index);
        fetchHosts();
    });
    connect(ui->button_hosts_in_prev, &QToolButton::clicked, this, [this]()
    {
        hosts_in_page_.setCurrentPage(hosts_in_page_.currentPage() - 1);
        fetchHosts();
    });
    connect(ui->button_hosts_in_next, &QToolButton::clicked, this, [this]()
    {
        hosts_in_page_.setCurrentPage(hosts_in_page_.currentPage() + 1);
        fetchHosts();
    });

    connect(ui->combo_hosts_out_page, &QComboBox::currentIndexChanged, this, [this](int index)
    {
        if (index < 0 || index == hosts_out_page_.currentPage())
            return;
        hosts_out_page_.setCurrentPage(index);
        fetchHosts();
    });
    connect(ui->button_hosts_out_prev, &QToolButton::clicked, this, [this]()
    {
        hosts_out_page_.setCurrentPage(hosts_out_page_.currentPage() - 1);
        fetchHosts();
    });
    connect(ui->button_hosts_out_next, &QToolButton::clicked, this, [this]()
    {
        hosts_out_page_.setCurrentPage(hosts_out_page_.currentPage() + 1);
        fetchHosts();
    });

    RouterController& controller = RouterController::instance();
    connect(&controller, &RouterController::sig_statusChanged, this,
            [this](qint64 router_id, RouterStatus status)
    {
        if (router_id == router_id_ && status != RouterStatus::ONLINE)
            reject();
    });

    // The dialog can stay open for a long time, so the snapshots it edits on top of track the
    // changes made from other consoles.
    connect(&controller, &RouterController::sig_usersChanged, this, [this](qint64 router_id)
    {
        if (router_id != router_id_)
            return;

        fetchUsers();
        fetchMemberNames();
    });

    // The save is built on top of the server snapshot (the membership and the revision inside the
    // model), so the snapshot must follow concurrent changes.
    connect(&controller, &RouterController::sig_workspacesChanged, this, [this](qint64 router_id)
    {
        if (router_id != router_id_)
            return;

        RouterSession* session = RouterController::session(router_id_);
        if (session)
        {
            session->listWorkspaces(RouterSession::CachePolicy::RELOAD, 0,
                                    { this, &RouterWorkspaceDialog::onWorkspaceListReceived });
        }
    });

    connect(&controller, &RouterController::sig_hostsChanged, this, [this](qint64 router_id)
    {
        if (router_id == router_id_)
            fetchHosts();
    });

    RouterSession* session = RouterController::session(router_id_);
    if (!session)
    {
        LOG(ERROR) << "No session for router" << router_id_;
        return;
    }

    // Which workspace holds a host is an administrator's call, so anybody else does not get the
    // tab at all: the lists it shows are refused for such a session anyway.
    is_admin_ = session->config().sessionType() == proto::router::SESSION_TYPE_ADMIN;
    ui->tab_widget->setTabVisible(ui->tab_widget->indexOf(ui->tab_hosts), is_admin_);

    updateLoadingState();

    // The whole list is fetched so the other names are available for the uniqueness check; in
    // modify mode the entry matching entry_id_ also populates the form.
    session->listWorkspaces(RouterSession::CachePolicy::USE_CACHE, 0,
                            { this, &RouterWorkspaceDialog::onWorkspaceListReceived });
    fetchUsers();
    fetchHosts();
}

//--------------------------------------------------------------------------------------------------
RouterWorkspaceDialog::~RouterWorkspaceDialog()
{
    LOG(INFO) << "Dtor";

    Settings settings;
    settings.setDialogGeometry(objectName(), saveGeometry());
}

//--------------------------------------------------------------------------------------------------
void RouterWorkspaceDialog::changeEvent(QEvent* event)
{
    QDialog::changeEvent(event);

    if (event->type() == QEvent::LayoutDirectionChange)
        updateArrowIcons();
}

//--------------------------------------------------------------------------------------------------
void RouterWorkspaceDialog::onWorkspaceListReceived(const RouterWorkspaceList& list)
{
    // A reply arriving while the dialog is already going away must not repopulate it (or stack
    // another message box) - same guard in the other handlers.
    if (closing_)
        return;

    if (list.error_code != proto::router::kErrorOk)
    {
        LOG(ERROR) << "Unable to get the list of the workspaces:" << list.error_code;
        if (!model_->isLoaded() && !closing_)
        {
            closing_ = true;
            MsgBox::warning(this, tr("Failed to get list of spaces."));
            reject();
        }
        return;
    }

    const bool initial_load = !model_->isLoaded();

    QList<WorkspaceEditModel::WorkspaceInfo> workspaces;
    workspaces.reserve(list.workspaces.size());
    for (const RouterWorkspace& workspace : std::as_const(list.workspaces))
    {
        WorkspaceEditModel::WorkspaceInfo& info = workspaces.emplaceBack();
        info.entry_id = workspace.entry_id;
        info.revision = workspace.revision;
        info.name = workspace.name;
        info.comment = workspace.comment;
        for (qint64 user_id : std::as_const(workspace.user_ids))
            info.access_ids.insert(user_id);
    }

    if (!model_->applyWorkspaceList(workspaces))
    {
        // The workspace being edited is gone (deleted from another console). Every further
        // action would fail with NotFound, so the dialog closes right away.
        LOG(ERROR) << "Edited workspace" << entry_id_ << "no longer exists";
        closing_ = true;
        MsgBox::warning(this, tr("The space was deleted from another console."));
        reject();
        return;
    }

    if (entry_id_ > 0)
    {
        if (initial_load)
        {
            ui->edit_name->setText(model_->serverName());
            ui->edit_comment->setPlainText(model_->serverComment());
        }
        else
        {
            if (!ui->edit_name->isModified())
                ui->edit_name->setText(model_->serverName());
            if (!ui->edit_comment->document()->isModified())
                ui->edit_comment->setPlainText(model_->serverComment());
        }
    }

    fetchMemberNames();
    updateLoadingState();
}

//--------------------------------------------------------------------------------------------------
void RouterWorkspaceDialog::onUserListReceived(const proto::router::UserList& list)
{
    if (closing_)
        return;

    if (list.error_code() != proto::router::kErrorOk)
    {
        LOG(ERROR) << "Unable to get the list of the users:" << list.error_code();
        if (!model_->isLoaded() && !closing_)
        {
            // Same as the workspace list: an unusable dialog with no explanation otherwise.
            closing_ = true;
            MsgBox::warning(this, tr("Failed to get list of users."));
            reject();
        }
        // On a refetch keep the page we already have.
        return;
    }

    QList<WorkspaceEditModel::User> users;
    users.reserve(list.user_size());
    for (int i = 0; i < list.user_size(); ++i)
    {
        const proto::router::User& user = list.user(i);

        WorkspaceEditModel::User& entry = users.emplaceBack();
        entry.entry_id = user.entry_id();
        entry.name     = QString::fromStdString(user.name());
    }

    model_->applyUserPage(users);

    const bool page_moved = users_page_.setTotalCount(list.total_count());
    if (page_moved)
        fetchUsers();

    updateLoadingState();
}

//--------------------------------------------------------------------------------------------------
void RouterWorkspaceDialog::onHostListReceived(const RouterHostList& list)
{
    if (closing_)
        return;

    if (list.error_code != proto::router::kErrorOk)
    {
        LOG(ERROR) << "Unable to get the list of the hosts:" << list.error_code;
        return;
    }

    hosts_in_ = list.hosts;
    if (hosts_in_page_.setTotalCount(list.total_count))
        fetchHosts();

    rebuildHostLists();
}

//--------------------------------------------------------------------------------------------------
// The left list: every host outside this workspace. A router that predates the outside mode
// answers invalid_request and the dialog falls back to the free-hosts list of the old dialog;
// that fallback request echoes workspace 0, and the echo is what tells the two answers apart
// (the outside reply echoes the workspace it was asked about).
void RouterWorkspaceDialog::onHostsOutsideListReceived(const RouterHostList& list)
{
    if (closing_)
        return;

    if (list.error_code == proto::router::kErrorInvalidRequest &&
        list.workspace_id != kNoWorkspace)
    {
        LOG(WARNING) << "Router refused the outside-workspace host list (" << list.error_code
                     << "), falling back to the free-hosts list";

        proto::router::HostListRequest free_request;
        free_request.set_mode(proto::router::HostListRequest::MODE_FILTERED);
        free_request.set_workspace_id(kNoWorkspace);
        free_request.set_group_id(0);
        free_request.set_offset(hosts_out_page_.offset());
        free_request.set_count(hosts_out_page_.pageSize());

        RouterSession* session = RouterController::session(router_id_);
        if (session)
        {
            session->listHosts(RouterSession::CachePolicy::RELOAD, std::move(free_request),
                               { this, &RouterWorkspaceDialog::onHostsOutsideListReceived });
        }
        return;
    }

    if (list.error_code != proto::router::kErrorOk)
    {
        LOG(ERROR) << "Unable to get the list of the hosts outside the space:"
                   << list.error_code;
        return;
    }

    hosts_out_ = list.hosts;
    if (hosts_out_page_.setTotalCount(list.total_count))
        fetchHosts();

    rebuildHostLists();
}

//--------------------------------------------------------------------------------------------------
void RouterWorkspaceDialog::onHostResultReceived(const proto::router::HostResult& result)
{
    if (closing_)
        return;

    // A batch of host moves is in flight: account for this reply in the shared counter and
    // finish the batch (single refetch + single warning) only on the last reply.
    if (host_move_batch_)
    {
        if (result.error_code() != proto::router::kErrorOk)
        {
            LOG(ERROR) << "Host move failed:" << result.error_code();
            ++host_move_batch_->failed;
            host_move_batch_->error_code = result.error_code();
        }

        if (--host_move_batch_->pending > 0)
            return;

        const int failed = host_move_batch_->failed;
        const int total = host_move_batch_->total;
        const std::string error_code = host_move_batch_->error_code;
        host_move_batch_.reset();
        pending_host_move_ = false;

        ui->tab_hosts->setEnabled(true);

        if (failed > 0)
        {
            if (total == 1)
                MsgBox::warning(this, routerErrorText(error_code));
            else
                MsgBox::warning(this, tr("Failed to move %n of %1 hosts.", "", failed).arg(total));
        }

        // Both lists change with a move, and a refetch is the only thing that tells which page
        // the hosts ended up on.
        fetchHosts();
        return;
    }

    ui->tab_hosts->setEnabled(true);

    const std::string& error_code = result.error_code();
    if (error_code != proto::router::kErrorOk)
    {
        LOG(ERROR) << "Host move failed:" << error_code;
        MsgBox::warning(this, routerErrorText(error_code));
    }

    fetchHosts();
}

//--------------------------------------------------------------------------------------------------
void RouterWorkspaceDialog::onWorkspaceResultReceived(const proto::router::WorkspaceResult& result)
{
    if (closing_)
        return;

    const std::string& error_code = result.error_code();

    if (error_code == proto::router::kErrorOk)
    {
        // A creation made for a host operation: the dialog stays open on the workspace it just
        // created, and the host move that asked for it runs now. The model is bound to the
        // workspace it edits, so the dialog continues on one built for the new record - the old
        // one would judge the name of the workspace as taken by somebody else and would save
        // against a revision it never saw.
        if (pending_host_move_ && entry_id_ == 0)
        {
            entry_id_ = result.entry_id();
            // The moves were filed against the workspace before it had an id (0 read as
            // "remove"): now that it exists, every pending claim into it is retargeted.
            for (PendingHostMove& move : pending_host_moves_)
            {
                if (move.workspace_id == 0)
                    move.workspace_id = entry_id_;
            }
            LOG(INFO) << "[ACTION] Workspace created for a host operation (entry_id:"
                      << entry_id_ << ")";

            model_ = std::make_unique<WorkspaceEditModel>(entry_id_);
            setEnabled(true);
            refetchLists();
            runPendingHostMoves();
            return;
        }

        LOG(INFO) << "[ACTION] Workspace saved";
        accept();
        close();
        return;
    }

    pending_host_moves_.clear();
    pending_host_move_ = false;

    if (error_code == proto::router::kErrorConflict)
    {
        LOG(ERROR) << "Workspace save rejected: concurrent change";
        setEnabled(true);
        refetchLists();
        MsgBox::warning(this, tr("The space was changed from another console. The lists are "
                                 "being refreshed - check the changes and save again."));
        return;
    }

    LOG(ERROR) << "Workspace save failed:" << error_code;
    setEnabled(true);
    MsgBox::warning(this, routerErrorText(error_code));
}

//--------------------------------------------------------------------------------------------------
void RouterWorkspaceDialog::onButtonBoxClicked(QAbstractButton* button)
{
    QDialogButtonBox::StandardButton standard_button = ui->buttonbox->standardButton(button);
    if (standard_button != QDialogButtonBox::Ok)
    {
        LOG(INFO) << "[ACTION] Action rejected";
        reject();
        close();
        return;
    }

    const QString new_name = validatedName();
    if (new_name.isEmpty())
        return;

    // The request is assembled from the model without mutating it: a failed save retries
    // against the same state.
    RouterWorkspace workspace;
    workspace.entry_id = entry_id_;
    workspace.name = new_name;
    workspace.comment = ui->edit_comment->toPlainText();

    // The revision the edit was based on: the router rejects the save with "conflict" when the
    // workspace changed meanwhile, instead of silently overwriting the concurrent change.
    workspace.revision = model_->baseRevision();
    workspace.user_ids = model_->accessUserIdsForSave();

    RouterSession* session = RouterController::session(router_id_);
    if (!session)
    {
        LOG(ERROR) << "No session for router" << router_id_;
        return;
    }

    setEnabled(false);

    LOG(INFO) << "[ACTION] Submitting workspace (entry_id:" << entry_id_
              << ", access entries:" << workspace.user_ids.size() << ")";
    if (entry_id_ > 0)
    {
        session->modifyWorkspace(workspace, { this, &RouterWorkspaceDialog::onWorkspaceResultReceived });
    }
    else
    {
        session->addWorkspace(workspace, { this, &RouterWorkspaceDialog::onWorkspaceResultReceived });
    }
}

//--------------------------------------------------------------------------------------------------
void RouterWorkspaceDialog::onAddClicked()
{
    QListWidgetItem* item = ui->list_available->currentItem();
    if (!item)
        return;

    const qint64 user_id = item->data(Qt::UserRole).toLongLong();
    model_->grantUser(user_id);
    fetchMemberNames();
    rebuildLists();
}

//--------------------------------------------------------------------------------------------------
void RouterWorkspaceDialog::onRemoveClicked()
{
    QListWidgetItem* item = ui->list_with_access->currentItem();
    if (!item)
        return;

    model_->revokeUser(item->data(Qt::UserRole).toLongLong());
    rebuildLists();
}

//--------------------------------------------------------------------------------------------------
void RouterWorkspaceDialog::onHostAddClicked()
{
    QList<RouterHost> hosts;
    for (QListWidgetItem* item : ui->list_hosts_available->selectedItems())
    {
        const RouterHost host = hostById(item->data(Qt::UserRole).toULongLong());
        if (host.host_id != kInvalidHostId)
            hosts.append(host);
    }
    if (hosts.isEmpty())
        return;

    moveHosts(hosts, entry_id_);
}

//--------------------------------------------------------------------------------------------------
void RouterWorkspaceDialog::onHostRemoveClicked()
{
    // A refetch that arrives while the question is shown rebuilds the list and deletes the items,
    // so the ids are taken before asking.
    QList<quint64> host_ids;
    for (QListWidgetItem* item : ui->list_hosts_in_workspace->selectedItems())
        host_ids.append(item->data(Qt::UserRole).toULongLong());

    if (host_ids.isEmpty())
        return;

    if (host_ids.size() == 1)
    {
        if (MsgBox::question(this, tr("Are you sure you want to remove the host from the space?")) == MsgBox::No)
        {
            LOG(INFO) << "Action is rejected by user";
            return;
        }
    }
    else
    {
        if (MsgBox::question(this, tr("Are you sure you want to remove %n hosts from the space?",
                                      "", host_ids.size())) == MsgBox::No)
        {
            LOG(INFO) << "Action is rejected by user";
            return;
        }
    }

    QList<RouterHost> hosts;
    for (quint64 host_id : std::as_const(host_ids))
    {
        const RouterHost host = hostById(host_id);
        if (host.host_id != kInvalidHostId)
            hosts.append(host);
    }
    if (hosts.isEmpty())
        return;

    moveHosts(hosts, kNoWorkspace);
}

//--------------------------------------------------------------------------------------------------
void RouterWorkspaceDialog::applyMemberLookup(const proto::router::UserList& list, qint64 user_id)
{
    if (closing_)
        return;

    if (list.error_code() != proto::router::kErrorOk)
    {
        // The name of a member is a display detail: without it the entry is shown by its id and
        // the next refetch asks again.
        LOG(ERROR) << "Unable to look up user" << user_id << ":" << list.error_code();
        return;
    }

    if (list.user_size() == 0)
    {
        model_->applyMissingUser(user_id);
    }
    else
    {
        WorkspaceEditModel::User user;
        user.entry_id = list.user(0).entry_id();
        user.name     = QString::fromStdString(list.user(0).name());
        model_->applyMemberUser(user);
    }

    if (model_->isLoaded())
        rebuildLists();
}

//--------------------------------------------------------------------------------------------------
void RouterWorkspaceDialog::moveHosts(const QList<RouterHost>& hosts, qint64 workspace_id)
{
    if (hosts.isEmpty())
        return;

    RouterSession* session = RouterController::session(router_id_);
    if (!session)
    {
        LOG(ERROR) << "No session for router" << router_id_;
        return;
    }

    QList<PendingHostMove> moves;
    for (const RouterHost& host : hosts)
    {
        if (host.host_id == kInvalidHostId)
            continue;
        PendingHostMove move;
        move.host = host;
        move.workspace_id = workspace_id;
        moves.append(move);
    }
    if (moves.isEmpty())
        return;

    if (pending_host_move_)
    {
        LOG(WARNING) << "Host move already in progress";
        return;
    }

    pending_host_moves_ = moves;
    pending_host_move_ = true;

    // A host cannot be claimed for a workspace that does not exist yet, so the creation runs
    // first and the move follows its reply.
    if (entry_id_ == 0)
    {
        const QString new_name = validatedName();
        if (new_name.isEmpty())
        {
            pending_host_moves_.clear();
            pending_host_move_ = false;
            return;
        }

        RouterWorkspace workspace;
        workspace.name = new_name;
        workspace.comment = ui->edit_comment->toPlainText();
        workspace.user_ids = model_->accessUserIdsForSave();

        setEnabled(false);
        ui->tab_hosts->setEnabled(false);

        LOG(INFO) << "[ACTION] Creating workspace for a host operation";
        session->addWorkspace(workspace, { this, &RouterWorkspaceDialog::onWorkspaceResultReceived });
        return;
    }

    runPendingHostMoves();
}

//--------------------------------------------------------------------------------------------------
void RouterWorkspaceDialog::runPendingHostMoves()
{
    RouterSession* session = RouterController::session(router_id_);
    if (!session || pending_host_moves_.isEmpty())
    {
        pending_host_moves_.clear();
        pending_host_move_ = false;
        ui->tab_hosts->setEnabled(true);
        return;
    }

    const int total = static_cast<int>(pending_host_moves_.size());

    host_move_batch_ = SharedPointer<HostMoveBatch>(new HostMoveBatch());
    host_move_batch_->pending = total;
    host_move_batch_->failed = 0;
    host_move_batch_->total = total;

    ui->tab_hosts->setEnabled(false);

    const QList<PendingHostMove> moves = pending_host_moves_;
    pending_host_moves_.clear();

    for (const PendingHostMove& pending : moves)
    {
        // The record is sent back with the membership of THIS workspace changed: a claim adds the
        // host at the workspace root (unless it is already a member), a remove drops only this
        // membership - every other workspace of the host stays (drag & drop adds, it does not move).
        // The scalar fields stay untouched: the router re-derives the primary membership itself.
        RouterHost host = pending.host;
        const qint64 target_workspace_id = pending.workspace_id;

        if (target_workspace_id == kNoWorkspace)
        {
            for (qsizetype i = host.memberships.size() - 1; i >= 0; --i)
            {
                if (host.memberships.at(i).first == entry_id_)
                    host.memberships.removeAt(i);
            }

            // Removing the last membership releases the host. The scalar has to say so too: with
            // it left at the workspace the host just lost, the router reads the edit as a no-op
            // (an empty list keeps the scalar move rules and nothing re-derives a primary the
            // request does not name).
            if (host.memberships.isEmpty())
            {
                host.workspace_id = kNoWorkspace;
                host.group_id = 0;
            }
        }
        else
        {
            bool present = false;
            for (const auto& membership : std::as_const(host.memberships))
            {
                if (membership.first == target_workspace_id)
                {
                    present = true;
                    break;
                }
            }

            if (!present)
                host.memberships.append(qMakePair(target_workspace_id, static_cast<qint64>(0)));
        }

        LOG(INFO) << "[ACTION] Updating workspaces of host" << host.host_id;
        session->editHost(host, { this, &RouterWorkspaceDialog::onHostResultReceived });
    }
}

//--------------------------------------------------------------------------------------------------
RouterHost RouterWorkspaceDialog::hostById(quint64 host_id) const
{
    for (const RouterHost& host : std::as_const(hosts_in_))
    {
        if (host.host_id == host_id)
            return host;
    }
    for (const RouterHost& host : std::as_const(hosts_out_))
    {
        if (host.host_id == host_id)
            return host;
    }
    return RouterHost();
}

//--------------------------------------------------------------------------------------------------
void RouterWorkspaceDialog::fetchUsers()
{
    RouterSession* session = RouterController::session(router_id_);
    if (!session)
        return;

    session->listUsers(users_page_.offset(), users_page_.pageSize(),
                       { this, &RouterWorkspaceDialog::onUserListReceived });
}

//--------------------------------------------------------------------------------------------------
void RouterWorkspaceDialog::fetchMemberNames()
{
    RouterSession* session = RouterController::session(router_id_);
    if (!session)
        return;

    for (qint64 user_id : model_->unresolvedMemberIds())
    {
        session->findUser(user_id, { this, [this, user_id](const proto::router::UserList& list)
        {
            applyMemberLookup(list, user_id);
        } });
    }
}

//--------------------------------------------------------------------------------------------------
void RouterWorkspaceDialog::fetchHosts()
{
    RouterSession* session = RouterController::session(router_id_);
    if (!session || !is_admin_)
        return;

    if (entry_id_ > 0)
    {
        proto::router::HostListRequest request;
        request.set_mode(proto::router::HostListRequest::MODE_FILTERED);
        request.set_workspace_id(entry_id_);
        request.set_group_id(kAnyGroup);
        request.set_offset(hosts_in_page_.offset());
        request.set_count(hosts_in_page_.pageSize());
        session->listHosts(RouterSession::CachePolicy::RELOAD, std::move(request),
                           { this, &RouterWorkspaceDialog::onHostListReceived });

        // The left list: every host outside this workspace - the hosts of the other spaces
        // together with the hosts no space claimed.
        proto::router::HostListRequest outside_request;
        outside_request.set_mode(proto::router::HostListRequest::MODE_OUTSIDE_WORKSPACE);
        outside_request.set_workspace_id(entry_id_);
        outside_request.set_group_id(0);
        outside_request.set_offset(hosts_out_page_.offset());
        outside_request.set_count(hosts_out_page_.pageSize());
        session->listHosts(RouterSession::CachePolicy::RELOAD, std::move(outside_request),
                           { this, &RouterWorkspaceDialog::onHostsOutsideListReceived });
        return;
    }

    // A workspace that does not exist yet contains nothing, so "outside" it is every host of
    // the router. MODE_ALL is admin-only and this dialog fetches only under is_admin_ above;
    // the reply echoes workspace 0, so onHostsOutsideListReceived treats it as the outside
    // list without falling back anywhere.
    proto::router::HostListRequest all_request;
    all_request.set_mode(proto::router::HostListRequest::MODE_ALL);
    all_request.set_offset(hosts_out_page_.offset());
    all_request.set_count(hosts_out_page_.pageSize());
    session->listHosts(RouterSession::CachePolicy::RELOAD, std::move(all_request),
                       { this, &RouterWorkspaceDialog::onHostsOutsideListReceived });
}

//--------------------------------------------------------------------------------------------------
void RouterWorkspaceDialog::refetchLists()
{
    RouterSession* session = RouterController::session(router_id_);
    if (!session)
        return;

    session->listWorkspaces(RouterSession::CachePolicy::RELOAD, 0,
                            { this, &RouterWorkspaceDialog::onWorkspaceListReceived });
    fetchUsers();
    fetchMemberNames();
    fetchHosts();
}

//--------------------------------------------------------------------------------------------------
void RouterWorkspaceDialog::rebuildLists()
{
    const auto captureState = [](QListWidget* list, QVariant* selected, int* scroll)
    {
        QListWidgetItem* item = list->currentItem();
        if (item)
            *selected = item->data(Qt::UserRole);
        *scroll = list->verticalScrollBar()->value();
    };

    const auto restoreState = [](QListWidget* list, const QVariant& selected, int scroll)
    {
        if (selected.isValid())
        {
            for (int i = 0; i < list->count(); ++i)
            {
                if (list->item(i)->data(Qt::UserRole) == selected)
                {
                    list->setCurrentItem(list->item(i));
                    break;
                }
            }
        }
        list->verticalScrollBar()->setValue(scroll);
    };

    QVariant selected_with_access, selected_available;
    int scroll_with_access = 0, scroll_available = 0;
    captureState(ui->list_with_access, &selected_with_access, &scroll_with_access);
    captureState(ui->list_available, &selected_available, &scroll_available);

    ui->list_with_access->clear();
    ui->list_available->clear();

    const auto add_user_item = [](QListWidget* list, const WorkspaceEditModel::User& user)
    {
        // A member whose name has not arrived yet is shown by its id.
        const QString text = user.name.isEmpty() ? QString::number(user.entry_id) : user.name;

        QListWidgetItem* item = new QListWidgetItem(QIcon(":/img/user.svg"), text);
        item->setData(Qt::UserRole, user.entry_id);
        list->addItem(item);
    };

    for (const WorkspaceEditModel::User& user : model_->memberUsers())
        add_user_item(ui->list_with_access, user);
    for (const WorkspaceEditModel::User& user : model_->availableUsers())
        add_user_item(ui->list_available, user);

    ui->list_with_access->sortItems();
    ui->list_available->sortItems();

    restoreState(ui->list_with_access, selected_with_access, scroll_with_access);
    restoreState(ui->list_available, selected_available, scroll_available);

    updatePagination(ui->combo_users_page, ui->button_users_prev, ui->button_users_next,
                     users_page_);

    rebuildHostLists();
}

//--------------------------------------------------------------------------------------------------
void RouterWorkspaceDialog::rebuildHostLists()
{
    const auto fill = [](QListWidget* list, const QList<RouterHost>& hosts)
    {
        QSet<quint64> selected_ids;
        for (QListWidgetItem* item : list->selectedItems())
            selected_ids.insert(item->data(Qt::UserRole).toULongLong());

        QVariant current;
        if (QListWidgetItem* item = list->currentItem())
            current = item->data(Qt::UserRole);
        const int scroll = list->verticalScrollBar()->value();

        list->clear();

        for (const RouterHost& host : hosts)
        {
            const QString name = host.computer_name.isEmpty()
                ? QString::number(host.host_id)
                : QString("%1 (%2)").arg(host.host_id).arg(host.computer_name);

            QListWidgetItem* item = new QListWidgetItem(QIcon(":/img/computer.svg"), name);
            item->setData(Qt::UserRole, host.host_id);
            list->addItem(item);
        }

        list->sortItems();

        for (int i = 0; i < list->count(); ++i)
        {
            if (selected_ids.contains(list->item(i)->data(Qt::UserRole).toULongLong()))
                list->item(i)->setSelected(true);
        }

        if (current.isValid())
        {
            for (int i = 0; i < list->count(); ++i)
            {
                if (list->item(i)->data(Qt::UserRole) == current)
                {
                    list->setCurrentItem(list->item(i));
                    break;
                }
            }
        }
        list->verticalScrollBar()->setValue(scroll);
    };

    fill(ui->list_hosts_in_workspace, hosts_in_);
    fill(ui->list_hosts_available, hosts_out_);

    updatePagination(ui->combo_hosts_in_page, ui->button_hosts_in_prev, ui->button_hosts_in_next,
                     hosts_in_page_);
    updatePagination(ui->combo_hosts_out_page, ui->button_hosts_out_prev,
                     ui->button_hosts_out_next, hosts_out_page_);

    updateButtonsState();
}

//--------------------------------------------------------------------------------------------------
void RouterWorkspaceDialog::updatePagination(QComboBox* combo, QToolButton* prev, QToolButton* next,
                                             const PageModel& page)
{
    const qint64 total_pages = page.pageCount();

    QSignalBlocker blocker(combo);
    combo->clear();
    for (qint64 i = 1; i <= total_pages; ++i)
        combo->addItem(QString::number(i));
    combo->setCurrentIndex(static_cast<int>(page.currentPage()));

    combo->setEnabled(total_pages > 1);
    prev->setEnabled(page.currentPage() > 0);
    next->setEnabled(page.currentPage() < total_pages - 1);
}

//--------------------------------------------------------------------------------------------------
void RouterWorkspaceDialog::updateButtonsState()
{
    ui->button_add->setEnabled(ui->list_available->currentItem() != nullptr);
    ui->button_remove->setEnabled(ui->list_with_access->currentItem() != nullptr);
    ui->button_host_add->setEnabled(!ui->list_hosts_available->selectedItems().isEmpty());
    ui->button_host_remove->setEnabled(!ui->list_hosts_in_workspace->selectedItems().isEmpty());
}

//--------------------------------------------------------------------------------------------------
void RouterWorkspaceDialog::updateLoadingState()
{
    const bool ready = model_->isLoaded();

    ui->edit_name->setEnabled(ready);
    ui->edit_comment->setEnabled(ready);
    ui->list_available->setEnabled(ready);
    ui->list_with_access->setEnabled(ready);
    ui->list_hosts_available->setEnabled(ready);
    ui->list_hosts_in_workspace->setEnabled(ready);

    if (QPushButton* ok_button = ui->buttonbox->button(QDialogButtonBox::Ok))
        ok_button->setEnabled(ready);

    if (ready)
    {
        rebuildLists();
    }
    else
    {
        ui->button_add->setEnabled(false);
        ui->button_remove->setEnabled(false);
        ui->button_host_add->setEnabled(false);
        ui->button_host_remove->setEnabled(false);
    }
}

//--------------------------------------------------------------------------------------------------
void RouterWorkspaceDialog::updateArrowIcons()
{
    const QIcon to_members(
        isRightToLeft() ? ":/img/arrow-green-left.svg" : ":/img/arrow-green-right.svg");
    const QIcon to_available(
        isRightToLeft() ? ":/img/arrow-red-right.svg" : ":/img/arrow-red-left.svg");

    ui->button_add->setIcon(to_members);
    ui->button_host_add->setIcon(to_members);
    ui->button_remove->setIcon(to_available);
    ui->button_host_remove->setIcon(to_available);
}

//--------------------------------------------------------------------------------------------------
QString RouterWorkspaceDialog::validatedName()
{
    const QString new_name = ui->edit_name->text().trimmed();

    if (new_name.isEmpty())
    {
        LOG(ERROR) << "Empty workspace name";
        MsgBox::warning(this, tr("Space name cannot be empty."));
        ui->tab_widget->setCurrentWidget(ui->tab_general);
        ui->edit_name->setFocus();
        return QString();
    }

    for (const QString& existing : model_->otherNames())
    {
        if (new_name.compare(existing, Qt::CaseInsensitive) == 0)
        {
            LOG(ERROR) << "Workspace name already exists:" << new_name;
            MsgBox::warning(this, tr("A space with the specified name already exists."));
            ui->tab_widget->setCurrentWidget(ui->tab_general);
            ui->edit_name->selectAll();
            ui->edit_name->setFocus();
            return QString();
        }
    }

    return new_name;
}
