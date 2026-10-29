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

#include "client/desktop/management/router_hosts_widget.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDataStream>
#include <QDateTime>
#include <QEvent>
#include <QHeaderView>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QFileDialog>
#include <QMenu>
#include <QMouseEvent>
#include <QEventLoop>
#include <QProgressDialog>
#include <QPushButton>
#include <QUuid>
#include <QRegularExpressionValidator>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QTimer>
#include <QToolButton>

#include <algorithm>

#include "base/auto_qpointer.h"
#include "base/logging.h"
#include "base/peer/router_user.h"
#include "base/shared_pointer.h"
#include "base/time_types.h"
#include "base/peer/host_id.h"
#include "client/router_controller.h"
#include "client/desktop/file_dialog.h"
#include "client/desktop/management/drag_and_drop.h"
#include "client/desktop/management/host_telemetry_dialog.h"
#include "client/desktop/management/router_host_dialog.h"
#include "client/desktop/management/router_host_bulk.h"
#include "client/desktop/management/router_host_bulk_config_dialog.h"
#include "client/desktop/management/router_host_config_dialog.h"
#include "client/desktop/management/router_host_user_dialog.h"
#include "client/desktop/management/router_host_users_dialog.h"
#include "client/desktop/management/ui_router_hosts_widget.h"
#include "common/desktop/icon_text_button.h"
#include "common/desktop/msg_box.h"
#include "common/desktop/router_error.h"
#include "proto/router_admin.h"
#include "proto/router_constants.h"

namespace {

// Upper sanity bound on the router-reported host count, used only for pagination.
const qint64 kMaxHostCount = 500000;

// The workspace filter item that narrows nothing. The item of the hosts without a workspace is 0.
const qint64 kAllWorkspaces = -1;

} // namespace

//--------------------------------------------------------------------------------------------------
RouterHostsWidget::RouterHostsWidget(QWidget* parent)
    : ContentWidget(Type::ROUTER_HOSTS, parent),
      ui(std::make_unique<Ui::RouterHostsWidget>())
{
    LOG(INFO) << "Ctor";
    ui->setupUi(this);

    // The order the columns are shown in. The header state saved by the user is restored on top
    // of it, so it only decides what the view looks like the first time.
    model_ = new HostListModel({ HostListModel::Column::HOST_ID,
                                 HostListModel::Column::DISPLAY_NAME,
                                 HostListModel::Column::COMPUTER_NAME,
                                 HostListModel::Column::ADDRESS,
                                 HostListModel::Column::COMMENT,
                                 HostListModel::Column::WORKSPACE,
                                 HostListModel::Column::OS,
                                 HostListModel::Column::VERSION,
                                 HostListModel::Column::ARCH,
                                 HostListModel::Column::LAST_CONNECT,
                                 HostListModel::Column::LAST_MODIFY,
                                 HostListModel::Column::STATUS }, this);
    ui->tree_hosts->setSelectionMode(QAbstractItemView::ExtendedSelection);
    ui->tree_hosts->setModel(model_);

    // Turned on again after the model is set: the view wires the header up to the sort of whatever
    // model it has, and at the time the generated setup ran there was none.
    ui->tree_hosts->setSortingEnabled(true);

    connect(ui->tree_hosts->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &RouterHostsWidget::sig_currentChanged);
    connect(ui->tree_hosts, &QAbstractItemView::activated, this, &RouterHostsWidget::sig_activated);

    ui->tree_hosts->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->tree_hosts, &QWidget::customContextMenuRequested,
            this, &RouterHostsWidget::onHostContextMenu);

    ui->tree_hosts->header()->setContextMenuPolicy(Qt::CustomContextMenu);
    ui->tree_hosts->header()->setSectionHidden(model_->sectionOf(HostListModel::Column::COMMENT), true);

    ui->tree_hosts->header()->resizeSection(model_->sectionOf(HostListModel::Column::HOST_ID), 75);
    ui->tree_hosts->header()->resizeSection(model_->sectionOf(HostListModel::Column::DISPLAY_NAME), 120);
    ui->tree_hosts->header()->resizeSection(model_->sectionOf(HostListModel::Column::COMPUTER_NAME), 110);
    ui->tree_hosts->header()->resizeSection(model_->sectionOf(HostListModel::Column::WORKSPACE), 135);
    ui->tree_hosts->header()->resizeSection(model_->sectionOf(HostListModel::Column::OS), 140);
    ui->tree_hosts->header()->resizeSection(model_->sectionOf(HostListModel::Column::VERSION), 100);
    ui->tree_hosts->header()->resizeSection(model_->sectionOf(HostListModel::Column::ARCH), 90);
    ui->tree_hosts->header()->resizeSection(model_->sectionOf(HostListModel::Column::LAST_CONNECT), 150);
    ui->tree_hosts->header()->resizeSection(model_->sectionOf(HostListModel::Column::LAST_MODIFY), 150);

    connect(ui->tree_hosts->header(), &QHeaderView::customContextMenuRequested,
            this, &RouterHostsWidget::onHeaderContextMenu);

    ui->tree_hosts->installEventFilter(this);
    ui->tree_hosts->viewport()->installEventFilter(this);

    // The largest entry is the largest page the router serves (kMaxHostPageSize).
    ui->combo_hosts_page_size->addItem("25", QVariant::fromValue<qint64>(25));
    ui->combo_hosts_page_size->addItem("50", QVariant::fromValue<qint64>(50));
    ui->combo_hosts_page_size->addItem("100", QVariant::fromValue<qint64>(100));
    ui->combo_hosts_page_size->setCurrentIndex(2);
    hosts_page_.setPageSize(ui->combo_hosts_page_size->currentData().toLongLong());

    ui->button_hosts_next->setIconOnRight(true);

    connect(ui->combo_hosts_page_size, &QComboBox::currentIndexChanged,
            this, &RouterHostsWidget::onHostsPageSizeChanged);
    connect(ui->combo_hosts_page, &QComboBox::currentIndexChanged,
            this, &RouterHostsWidget::onHostsPageChanged);
    connect(ui->button_hosts_prev, &QToolButton::clicked, this, &RouterHostsWidget::onHostsPrevClicked);
    connect(ui->button_hosts_next, &QToolButton::clicked, this, &RouterHostsWidget::onHostsNextClicked);

    updateHostsPagination();

    ui->combo_filter_workspace->addItem(tr("All spaces"), QVariant::fromValue<qint64>(kAllWorkspaces));
    ui->combo_filter_workspace->addItem(tr("Not in any space"), QVariant::fromValue<qint64>(0));

    filter_timer_ = new QTimer(this);
    filter_timer_->setSingleShot(true);
    filter_timer_->setInterval(MilliSeconds(300));
    connect(filter_timer_, &QTimer::timeout, this, &RouterHostsWidget::onFilterChanged);

    ui->edit_filter_id->setMaxLength(static_cast<int>(proto::router::kMaxHostIdLength));
    ui->edit_filter_id->setValidator(new QRegularExpressionValidator(QRegularExpression("[0-9]*"), ui->edit_filter_id));

    // Counts UTF-16 units, the bound counts UTF-8 bytes. A unit takes at most three bytes.
    for (QLineEdit* edit : { ui->edit_filter_name, ui->edit_filter_address, ui->edit_filter_os,
                             ui->edit_filter_version, ui->edit_filter_arch })
        edit->setMaxLength(static_cast<int>(proto::router::kMaxHostQueryLength / 3));

    for (QLineEdit* edit : { ui->edit_filter_id, ui->edit_filter_name, ui->edit_filter_address, ui->edit_filter_os,
                             ui->edit_filter_version, ui->edit_filter_arch })
        connect(edit, &QLineEdit::textChanged, filter_timer_, qOverload<>(&QTimer::start));

    connect(ui->combo_filter_workspace, &QComboBox::currentIndexChanged,
            this, &RouterHostsWidget::onFilterChanged);
    connect(ui->button_filter_reset, &QPushButton::clicked, this, &RouterHostsWidget::onFilterReset);

    RouterController& controller = RouterController::instance();
    connect(&controller, &RouterController::sig_hostsChanged, this, [this](qint64 router_id)
    {
        if (router_id == router_id_)
            fetchHosts();
    });
    connect(&controller, &RouterController::sig_workspacesChanged, this, [this](qint64 router_id)
    {
        if (router_id == router_id_)
            fetchWorkspaces();
    });
    connect(&controller, &RouterController::sig_statusChanged, this,
            [this](qint64 router_id, RouterStatus status)
    {
        if (router_id == router_id_ && status != RouterStatus::ONLINE)
        {
            model_->setHosts({});
            workspace_names_.clear();
            updateCountLabel();
        }
    });
}

//--------------------------------------------------------------------------------------------------
RouterHostsWidget::~RouterHostsWidget()
{
    LOG(INFO) << "Dtor";
}

//--------------------------------------------------------------------------------------------------
void RouterHostsWidget::showRouter(qint64 router_id)
{
    router_id_ = router_id;
    hosts_page_.clear();

    model_->setHosts({});
    updateCountLabel();

    clearFilter();
    workspace_names_.clear();
    updateWorkspaceFilter();

    // Workspaces are fetched first so the id -> name map is ready when the host list arrives.
    fetchWorkspaces();
    fetchHosts();
}

//--------------------------------------------------------------------------------------------------
void RouterHostsWidget::setMimeType(const QString& mime_type)
{
    mime_type_ = mime_type;
}

//--------------------------------------------------------------------------------------------------
bool RouterHostsWidget::hasSelectedHost() const
{
    return selectedHosts().size() == 1;
}

//--------------------------------------------------------------------------------------------------
HostId RouterHostsWidget::selectedHostId() const
{
    const QList<RouterHost> hosts = selectedHosts();
    return hosts.size() == 1 ? hosts.front().host_id : kInvalidHostId;
}

//--------------------------------------------------------------------------------------------------
HostConfig RouterHostsWidget::selectedHostConfig() const
{
    const QList<RouterHost> hosts = selectedHosts();
    if (hosts.size() != 1 || hosts.front().host_id == kInvalidHostId)
        return HostConfig();

    const RouterHost& host = hosts.front();

    QString name = host.display_name;
    if (name.isEmpty())
        name = host.computer_name;

    return HostConfig::forRouterHost(router_id_, host.host_id, name);
}

//--------------------------------------------------------------------------------------------------
void RouterHostsWidget::copyCurrentHostRow()
{
    const int row = ui->tree_hosts->currentIndex().row();
    if (row < 0)
        return;

    QString result;
    for (int i = 0; i < model_->columnCount(); ++i)
    {
        const QString text = model_->index(row, i).data().toString();
        if (!text.isEmpty())
            result += text + ' ';
    }
    result.chop(1);

    if (result.isEmpty())
        return;

    if (QClipboard* clipboard = QApplication::clipboard())
        clipboard->setText(result);
}

//--------------------------------------------------------------------------------------------------
void RouterHostsWidget::copyCurrentHostColumn(int column)
{
    const int row = ui->tree_hosts->currentIndex().row();
    if (row < 0 || column < 0 || column >= model_->columnCount())
        return;

    const QString text = model_->index(row, column).data().toString();
    if (text.isEmpty())
        return;

    if (QClipboard* clipboard = QApplication::clipboard())
        clipboard->setText(text);
}

//--------------------------------------------------------------------------------------------------
QByteArray RouterHostsWidget::saveState()
{
    QByteArray buffer;

    {
        QDataStream stream(&buffer, QIODevice::WriteOnly);
        stream.setVersion(QDataStream::Qt_6_10);

        stream << ui->tree_hosts->header()->saveState();
    }

    return buffer;
}

//--------------------------------------------------------------------------------------------------
void RouterHostsWidget::restoreState(const QByteArray& state)
{
    QDataStream stream(state);
    stream.setVersion(QDataStream::Qt_6_10);

    QByteArray hosts_columns_state;
    stream >> hosts_columns_state;

    if (!hosts_columns_state.isEmpty())
    {
        ui->tree_hosts->header()->restoreState(hosts_columns_state);
        ui->tree_hosts->header()->setSectionsClickable(true);
        ui->tree_hosts->header()->setSortIndicatorShown(true);
    }
}

//--------------------------------------------------------------------------------------------------
void RouterHostsWidget::reload()
{
    fetchHosts();
}

//--------------------------------------------------------------------------------------------------
void RouterHostsWidget::save()
{
    saveHostsToFile();
}

//--------------------------------------------------------------------------------------------------
void RouterHostsWidget::onModifyHost()
{
    const QList<RouterHost> hosts = selectedHosts();
    if (hosts.size() != 1)
    {
        LOG(INFO) << "No single selected host";
        return;
    }

    const RouterHost& host = hosts.front();

    AutoQPointer<RouterHostDialog> dialog(new RouterHostDialog(router_id_, host, this));
    if (dialog->exec() == QDialog::Accepted)
        fetchHosts();
}

//--------------------------------------------------------------------------------------------------
void RouterHostsWidget::onDisconnectHost()
{
    const QList<RouterHost> hosts = selectedOnlineHosts();
    if (hosts.isEmpty())
    {
        LOG(INFO) << "No selected online host";
        return;
    }

    const int total = static_cast<int>(hosts.size());

    QString message;
    if (total == 1)
        message = tr("Are you sure you want to disconnect host \"%1\"?").arg(hosts.front().computer_name);
    else
        message = tr("Are you sure you want to disconnect %n hosts?", "", total);

    if (MsgBox::question(this, message) != MsgBox::Yes)
    {
        LOG(INFO) << "[ACTION] Disconnect hosts rejected by user";
        return;
    }

    LOG(INFO) << "[ACTION] Disconnect hosts accepted by user:" << total;
    sendHostRequests(hosts, &RouterSession::disconnectHost, [this](int failed)
    {
        return tr("Failed to disconnect %n of the selected hosts.", "", failed);
    });
}

//--------------------------------------------------------------------------------------------------
void RouterHostsWidget::onRemoveHost()
{
    const QList<RouterHost> hosts = selectedHosts();
    if (hosts.isEmpty())
    {
        LOG(INFO) << "No selected host";
        return;
    }

    const int total = static_cast<int>(hosts.size());

    QString message = tr("Deleting a host will result in all its configuration for connecting "
                         "to the router being deleted, and the application will be uninstalled "
                         "on the host. This operation is irreversible. Are you sure you want to "
                         "do this?");
    if (total > 1)
        message = tr("%n hosts will be deleted.", "", total) + "\n\n" + message;

    if (MsgBox::importantQuestion(this, message, Seconds(10)) == MsgBox::No)
    {
        LOG(INFO) << "[ACTION] Remove hosts rejected by user";
        return;
    }

    LOG(INFO) << "[ACTION] Remove hosts accepted by user:" << total;
    sendHostRequests(hosts, &RouterSession::removeHost, [this](int failed)
    {
        return tr("Failed to remove %n of the selected hosts.", "", failed);
    });
}

//--------------------------------------------------------------------------------------------------
void RouterHostsWidget::onCheckHostUpdates()
{
    const QList<RouterHost> hosts = selectedOnlineHosts();
    if (hosts.isEmpty())
    {
        LOG(INFO) << "No selected online host";
        return;
    }

    LOG(INFO) << "[ACTION] Check host updates requested by user:" << hosts.size();
    sendHostRequests(hosts, &RouterSession::checkHostUpdates, [this](int failed)
    {
        return tr("Failed to check for updates on %n of the selected hosts.", "", failed);
    });
}

//--------------------------------------------------------------------------------------------------
void RouterHostsWidget::onHostTelemetry()
{
    const QList<RouterHost> hosts = selectedHosts();
    if (hosts.size() != 1)
    {
        LOG(INFO) << "No single selected host";
        return;
    }

    const RouterHost& host = hosts.front();

    LOG(INFO) << "[ACTION] Host telemetry requested by user";
    AutoQPointer<HostTelemetryDialog> dialog(new HostTelemetryDialog(router_id_, host, this));
    dialog->exec();
}

//--------------------------------------------------------------------------------------------------
void RouterHostsWidget::onHostUpdateConfig()
{
    const QList<RouterHost> hosts = selectedHosts();
    if (hosts.size() != 1)
    {
        LOG(INFO) << "No single selected host";
        return;
    }

    const RouterHost& host = hosts.front();

    LOG(INFO) << "[ACTION] Host update configuration requested by user";
    AutoQPointer<RouterHostConfigDialog> dialog(new RouterHostConfigDialog(router_id_, host.host_id, this));
    dialog->exec();
}

//--------------------------------------------------------------------------------------------------
void RouterHostsWidget::onHostUsers()
{
    const QList<RouterHost> hosts = selectedHosts();
    if (hosts.size() != 1)
    {
        LOG(INFO) << "No single selected host";
        return;
    }

    const RouterHost& host = hosts.front();

    LOG(INFO) << "[ACTION] Host users requested by user";
    AutoQPointer<RouterHostUsersDialog> dialog(new RouterHostUsersDialog(router_id_, host.host_id, this));
    dialog->exec();
}

//--------------------------------------------------------------------------------------------------
void RouterHostsWidget::onHostFullConfig()
{
    const QList<RouterHost> hosts = selectedHosts();
    if (hosts.size() != 1)
    {
        LOG(INFO) << "No single selected host";
        return;
    }

    const RouterHost& host = hosts.front();

    QString file_name = QFileDialog::getOpenFileName(this, tr("Open host configuration file"),
                                                     QString(), tr("JSON files (*.json);;All files (*)"));
    if (file_name.isEmpty())
        return;

    QFile file(file_name);
    if (!file.open(QIODevice::ReadOnly))
    {
        MsgBox::warning(this, tr("Cannot open file: %1").arg(file.errorString()));
        return;
    }

    const QByteArray data = file.readAll();
    file.close();

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
    {
        MsgBox::warning(this, tr("Invalid JSON: %1").arg(err.errorString()));
        return;
    }

    const QString json = QString::fromUtf8(doc.toJson(QJsonDocument::Compact));
    RouterSession* session = RouterController::session(router_id_);
    if (!session)
    {
        MsgBox::warning(this, tr("No session to router."));
        return;
    }

    QJsonObject params;
    params["config_json"] = json;
    params["keep_local"] = QJsonArray();
    params["dry_run"] = false;

    const QString params_json = QString::fromUtf8(QJsonDocument(params).toJson(QJsonDocument::Compact));
    session->setHostConfig(host.host_id, "apply_full_config", params_json.toStdString(),
                          {this, [this](const proto::router::HostResult& result)
                          {
                              if (result.error_code() == proto::router::kErrorOk)
                                  MsgBox::information(this, tr("Configuration applied successfully."));
                              else
                                  MsgBox::warning(this, tr("Failed to apply: %1").arg(QString::fromStdString(result.error_code())));
                          }});
}

//--------------------------------------------------------------------------------------------------
bool RouterHostsWidget::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == ui->tree_hosts && event->type() == QEvent::KeyPress)
    {
        if (static_cast<QKeyEvent*>(event)->key() == Qt::Key_F2)
        {
            onModifyHost();
            return true;
        }
    }
    else if (watched == ui->tree_hosts->viewport())
    {
        if (event->type() == QEvent::MouseButtonPress)
        {
            QMouseEvent* mouse_event = static_cast<QMouseEvent*>(event);
            if (mouse_event->button() == Qt::LeftButton)
                start_pos_ = mouse_event->pos();
        }
        else if (event->type() == QEvent::MouseMove)
        {
            QMouseEvent* mouse_event = static_cast<QMouseEvent*>(event);
            if (mouse_event->buttons() & Qt::LeftButton)
            {
                const int distance = (mouse_event->pos() - start_pos_).manhattanLength();
                if (distance > QApplication::startDragDistance())
                    startDrag();

                return true;
            }
        }
    }

    return ContentWidget::eventFilter(watched, event);
}

//--------------------------------------------------------------------------------------------------
void RouterHostsWidget::onHostListReceived(const RouterHostList& list)
{
    // Only the "any workspace / any group" response is shown here; per-workspace responses are
    // handled by RouterGroupWidget.
    if (list.workspace_id != 0 || list.group_id != 0)
        return;

    if (list.error_code != proto::router::kErrorOk)
    {
        // An error reply carries no list; treating it as an empty one would remove every host
        // from the tree. Keep what is shown - the next notification triggers another fetch.
        LOG(ERROR) << "Unable to get the list of the hosts:" << list.error_code;
        return;
    }

    const QList<RouterHost> selected = selectedHosts();
    const RouterHost* current = model_->hostAt(ui->tree_hosts->currentIndex().row());
    const HostId current_host_id = current ? current->host_id : kInvalidHostId;

    model_->setWorkspaceNames(workspace_names_);
    model_->setHosts(list.hosts);

    // The page is replaced whole, so the rows the user was on have to be found again by the hosts
    // they were showing.
    QItemSelection selection;
    for (const RouterHost& host : std::as_const(selected))
    {
        const int row = model_->rowOf(host.host_id);
        if (row >= 0)
            selection.select(model_->index(row, 0), model_->index(row, 0));
    }

    QItemSelectionModel* selection_model = ui->tree_hosts->selectionModel();

    const int current_row = model_->rowOf(current_host_id);
    if (current_row >= 0)
        selection_model->setCurrentIndex(model_->index(current_row, 0), QItemSelectionModel::NoUpdate);

    selection_model->select(selection, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);

    const bool page_moved = hosts_page_.setTotalCount(qMin(list.total_count, kMaxHostCount));
    updateHostsPagination();

    // The page the list was fetched for is gone, so the tree was just emptied. Nothing else asks
    // for the page it was moved to, and the user would be left looking at nothing.
    if (page_moved)
        fetchHosts();

    emit sig_currentChanged();
    updateCountLabel();
}

//--------------------------------------------------------------------------------------------------
void RouterHostsWidget::onWorkspaceListReceived(const RouterWorkspaceList& list)
{
    if (list.error_code != proto::router::kErrorOk)
    {
        // An error reply carries no list; applying it would blank the workspace column of every
        // host row. Keep what is shown - the next notification triggers another fetch.
        LOG(ERROR) << "Unable to get the list of the workspaces:" << list.error_code;
        return;
    }

    workspace_names_.clear();
    for (const RouterWorkspace& workspace : std::as_const(list.workspaces))
        workspace_names_.insert(workspace.entry_id, workspace.name);

    model_->setWorkspaceNames(workspace_names_);
    updateWorkspaceFilter();
}

//--------------------------------------------------------------------------------------------------
void RouterHostsWidget::onHostContextMenu(const QPoint& pos)
{
    const QModelIndex index = ui->tree_hosts->indexAt(pos);
    if (index.isValid() && !ui->tree_hosts->selectionModel()->isRowSelected(index.row()))
        ui->tree_hosts->setCurrentIndex(index);

    const int column = index.column();
    emit sig_contextMenu(ui->tree_hosts->viewport()->mapToGlobal(pos), column);
}

//--------------------------------------------------------------------------------------------------
void RouterHostsWidget::onHeaderContextMenu(const QPoint& pos)
{
    QPointer<QHeaderView> header = ui->tree_hosts->header();
    AutoQPointer<QMenu> menu(new QMenu(this));

    for (int i = 1; i < header->count(); ++i)
    {
        ColumnAction* action = new ColumnAction(
            model_->headerData(i, Qt::Horizontal, Qt::DisplayRole).toString(), i, menu);
        action->setChecked(!header->isSectionHidden(i));
        menu->addAction(action);
    }

    ColumnAction* action = dynamic_cast<ColumnAction*>(menu->exec(header->viewport()->mapToGlobal(pos)));
    if (!action || !header)
        return;

    header->setSectionHidden(action->columnIndex(), !action->isChecked());
}

//--------------------------------------------------------------------------------------------------
void RouterHostsWidget::onHostsPageSizeChanged(int /* index */)
{
    hosts_page_.setPageSize(ui->combo_hosts_page_size->currentData().toLongLong());
    hosts_page_.setCurrentPage(0);
    fetchHosts();
}

//--------------------------------------------------------------------------------------------------
void RouterHostsWidget::onHostsPageChanged(int index)
{
    if (index < 0)
        return;

    if (index == hosts_page_.currentPage())
        return;

    hosts_page_.setCurrentPage(index);
    fetchHosts();
}

//--------------------------------------------------------------------------------------------------
void RouterHostsWidget::onHostsPrevClicked()
{
    if (hosts_page_.currentPage() <= 0)
        return;

    hosts_page_.setCurrentPage(hosts_page_.currentPage() - 1);
    fetchHosts();
}

//--------------------------------------------------------------------------------------------------
void RouterHostsWidget::onHostsNextClicked()
{
    if (hosts_page_.currentPage() >= hosts_page_.pageCount() - 1)
        return;

    hosts_page_.setCurrentPage(hosts_page_.currentPage() + 1);
    fetchHosts();
}

//--------------------------------------------------------------------------------------------------
void RouterHostsWidget::onFilterChanged()
{
    filter_timer_->stop();
    hosts_page_.setCurrentPage(0);
    fetchHosts();
}

//--------------------------------------------------------------------------------------------------
void RouterHostsWidget::onFilterReset()
{
    clearFilter();
    onFilterChanged();
}

//--------------------------------------------------------------------------------------------------
void RouterHostsWidget::fetchHosts()
{
    RouterSession* session = RouterController::session(router_id_);
    if (!session)
        return;

    if (session->config().sessionType() != proto::router::SESSION_TYPE_ADMIN)
        return;

    proto::router::HostListRequest request;
    request.set_mode(proto::router::HostListRequest::MODE_ALL);
    request.set_offset(hosts_page_.offset());
    request.set_count(hosts_page_.pageSize());
    *request.mutable_filter() = hostFilter();
    session->listHosts(RouterSession::CachePolicy::RELOAD, std::move(request),
                       { this, &RouterHostsWidget::onHostListReceived });
}

//--------------------------------------------------------------------------------------------------
void RouterHostsWidget::fetchWorkspaces()
{
    RouterSession* session = RouterController::session(router_id_);
    if (!session)
        return;

    if (session->config().sessionType() != proto::router::SESSION_TYPE_ADMIN)
        return;

    session->listWorkspaces(RouterSession::CachePolicy::RELOAD, 0,
                            { this, &RouterHostsWidget::onWorkspaceListReceived });
}

//--------------------------------------------------------------------------------------------------
void RouterHostsWidget::updateWorkspaceFilter()
{
    QComboBox* combo = ui->combo_filter_workspace;
    const qint64 current_id = combo->currentData().toLongLong();

    QList<qint64> workspace_ids = workspace_names_.keys();
    std::sort(workspace_ids.begin(), workspace_ids.end(), [this](qint64 first, qint64 second)
    {
        return workspace_names_.value(first).localeAwareCompare(workspace_names_.value(second)) < 0;
    });

    {
        QSignalBlocker blocker(combo);

        // The first two items are "all workspaces" and "without workspace".
        while (combo->count() > 2)
            combo->removeItem(2);

        for (qint64 workspace_id : std::as_const(workspace_ids))
            combo->addItem(workspace_names_.value(workspace_id), QVariant::fromValue<qint64>(workspace_id));

        const int index = combo->findData(QVariant::fromValue<qint64>(current_id));
        combo->setCurrentIndex(index >= 0 ? index : 0);
    }

    // The hosts shown were fetched for a workspace that is gone.
    if (combo->currentData().toLongLong() != current_id)
        onFilterChanged();
}

//--------------------------------------------------------------------------------------------------
proto::router::HostFilter RouterHostsWidget::hostFilter() const
{
    proto::router::HostFilter filter;

    const qint64 workspace_id = ui->combo_filter_workspace->currentData().toLongLong();
    if (workspace_id != kAllWorkspaces)
        filter.add_workspace_id(workspace_id);

    filter.set_host_id(ui->edit_filter_id->text().trimmed().toStdString());
    filter.set_display_name(ui->edit_filter_name->text().trimmed().toStdString());
    filter.set_address(ui->edit_filter_address->text().trimmed().toStdString());
    filter.set_os_name(ui->edit_filter_os->text().trimmed().toStdString());
    filter.set_version(ui->edit_filter_version->text().trimmed().toStdString());
    filter.set_cpu_arch(ui->edit_filter_arch->text().trimmed().toStdString());
    return filter;
}

//--------------------------------------------------------------------------------------------------
void RouterHostsWidget::clearFilter()
{
    ui->edit_filter_id->clear();
    ui->edit_filter_name->clear();
    ui->edit_filter_address->clear();
    ui->edit_filter_os->clear();
    ui->edit_filter_version->clear();
    ui->edit_filter_arch->clear();
    filter_timer_->stop();

    QSignalBlocker blocker(ui->combo_filter_workspace);
    ui->combo_filter_workspace->setCurrentIndex(0);
}

//--------------------------------------------------------------------------------------------------
void RouterHostsWidget::updateHostsPagination()
{
    const qint64 total_pages = hosts_page_.pageCount();

    QSignalBlocker blocker(ui->combo_hosts_page);
    ui->combo_hosts_page->clear();
    for (qint64 i = 1; i <= total_pages; ++i)
        ui->combo_hosts_page->addItem(QString::number(i));
    ui->combo_hosts_page->setCurrentIndex(static_cast<int>(hosts_page_.currentPage()));

    ui->combo_hosts_page->setEnabled(total_pages > 1);
    ui->button_hosts_prev->setEnabled(hosts_page_.currentPage() > 0);
    ui->button_hosts_next->setEnabled(hosts_page_.currentPage() < total_pages - 1);
}

//--------------------------------------------------------------------------------------------------
void RouterHostsWidget::updateCountLabel()
{
    ui->label_hosts_count->setText(tr("%n host(s)", "", model_->rowCount()));
}

//--------------------------------------------------------------------------------------------------
void RouterHostsWidget::startDrag()
{
    // A Ctrl click takes the pressed row out of the selection, and then there is nothing under the
    // mouse to drag.
    const QModelIndex index = ui->tree_hosts->indexAt(start_pos_);
    if (!index.isValid() || !ui->tree_hosts->selectionModel()->isRowSelected(index.row()))
        return;

    const QList<RouterHost> hosts = selectedHosts();
    if (hosts.isEmpty())
        return;

    RouterHostDrag drag(this);
    drag.setHosts(router_id_, hosts, mime_type_);

    const QIcon icon = index.siblingAtColumn(0).data(Qt::DecorationRole).value<QIcon>();
    drag.setPixmap(icon.pixmap(icon.actualSize(QSize(16, 16))));

    drag.exec(Qt::MoveAction);
}

//--------------------------------------------------------------------------------------------------
QString RouterHostsWidget::workspaceNameById(qint64 workspace_id) const
{
    if (workspace_id <= 0)
        return QString();

    return workspace_names_.value(workspace_id);
}

//--------------------------------------------------------------------------------------------------
QList<RouterHost> RouterHostsWidget::selectedHosts() const
{
    const QModelIndexList rows = ui->tree_hosts->selectionModel()->selectedRows();

    QList<RouterHost> hosts;
    for (const QModelIndex& index : rows)
    {
        if (const RouterHost* host = model_->hostAt(index.row()))
            hosts.append(*host);
    }

    return hosts;
}

//--------------------------------------------------------------------------------------------------
QList<RouterHost> RouterHostsWidget::selectedOnlineHosts() const
{
    QList<RouterHost> hosts = selectedHosts();
    hosts.removeIf([](const RouterHost& host) { return !host.online; });
    return hosts;
}

//--------------------------------------------------------------------------------------------------
void RouterHostsWidget::saveHostsToFile()
{
    LOG(INFO) << "[ACTION] Save hosts to file";

    QString selected_filter;
    QString file_path = FileDialog::getSaveFileName(
        this, tr("Save File"), tr("JSON files (*.json)"), &selected_filter);
    if (file_path.isEmpty() || selected_filter.isEmpty())
    {
        LOG(INFO) << "No selected path";
        return;
    }

    QSaveFile file(file_path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        LOG(INFO) << "Unable to open file:" << file.errorString();
        MsgBox::warning(this, tr("Could not open file for writing."));
        return;
    }

    QJsonArray root_array;

    for (int i = 0; i < model_->rowCount(); ++i)
    {
        const RouterHost& info = *model_->hostAt(i);

        QJsonObject host_object;

        host_object.insert("display_name", info.display_name);
        host_object.insert("computer_name", info.computer_name);
        host_object.insert("operating_system", info.os_name);
        host_object.insert("ip_address", info.address);
        host_object.insert("comment", info.comment);
        host_object.insert("workspace", workspaceNameById(info.workspace_id));
        host_object.insert("architecture", info.cpu_arch);
        host_object.insert("version", info.version);

        if (info.last_connect > 0)
        {
            QString time = QLocale::system().toString(QDateTime::fromSecsSinceEpoch(
                info.last_connect), QLocale::ShortFormat);
            host_object.insert("connect_time", time);
        }

        if (info.last_modify > 0)
        {
            QString time = QLocale::system().toString(QDateTime::fromSecsSinceEpoch(
                info.last_modify), QLocale::ShortFormat);
            host_object.insert("modify_time", time);
        }

        host_object.insert("host_id", QString::number(info.host_id));

        root_array.append(host_object);
    }

    QJsonObject root_object;
    root_object.insert("hosts", root_array);

    const QByteArray json = QJsonDocument(root_object).toJson();
    if (file.write(json) != json.size() || !file.commit())
    {
        LOG(INFO) << "Unable to write file:" << file.errorString();
        MsgBox::warning(this, tr("Unable to write file."));
        return;
    }
}

//--------------------------------------------------------------------------------------------------
void RouterHostsWidget::sendHostRequests(const QList<RouterHost>& hosts, HostRequest request,
                                         const std::function<QString(int)>& error_text)
{
    RouterSession* session = RouterController::session(router_id_);
    if (!session)
        return;

    struct Batch
    {
        int pending = 0;
        int failed = 0;
        std::string error_code;
    };

    const int total = static_cast<int>(hosts.size());

    SharedPointer<Batch> batch(new Batch());
    batch->pending = total;

    for (const RouterHost& host : hosts)
    {
        const HostId host_id = host.host_id;

        (session->*request)(host_id, { this,
            [this, batch, total, error_text, host_id](const proto::router::HostResult& result)
        {
            if (result.error_code() != proto::router::kErrorOk)
            {
                LOG(ERROR) << "Request" << result.command_name() << "for host" << host_id
                           << "failed:" << result.error_code();
                ++batch->failed;
                batch->error_code = result.error_code();
            }

            if (--batch->pending > 0)
                return;

            if (batch->failed > 0)
            {
                if (total == 1)
                    MsgBox::warning(this, routerErrorText(batch->error_code));
                else
                    MsgBox::warning(this, error_text(batch->failed));
            }

            fetchHosts();
        }});
    }
}

namespace {

// Display label for a host in bulk tables.
QString bulkHostLabel(const RouterHost& host)
{
    if (!host.display_name.isEmpty() && !host.computer_name.isEmpty())
        return host.display_name + " (" + host.computer_name + ")";
    if (!host.display_name.isEmpty())
        return host.display_name;
    if (!host.computer_name.isEmpty())
        return host.computer_name;
    return QString::number(host.host_id);
}

QString newBulkId()
{
    return "bulk-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
}

// Runs a fetch runner to completion under a modal progress dialog.
QList<RouterHostBulkResult> execFetchBulk(RouterHostBulkRunner* runner,
                                         const QString& label,
                                         QWidget* parent)
{
    QProgressDialog progress(
        label, RouterHostsWidget::tr("Cancel"), 0, runner->targetCount(), parent);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.setValue(0);

    QEventLoop loop;
    QObject::connect(runner, &RouterHostBulkRunner::resultReady,
                     &progress, [&](int index)
                     {
                         progress.setValue(index + 1);
                         if (progress.wasCanceled())
                             runner->cancel();
                     });
    QObject::connect(runner, &RouterHostBulkRunner::finished, &loop, &QEventLoop::quit);
    runner->start();
    loop.exec();
    progress.setValue(runner->targetCount());
    return runner->results();
}

struct BulkTeleInfo
{
    bool ok = false;
    QString channel;
    QString address;
    QString key;
    bool auto_update = false;
    int frequency = 7;
};

// Telemetry is served from the router database cache, so this is fast and never blocks.
QMap<HostId, BulkTeleInfo> fetchBulkTelemetries(qint64 router_id,
                                               const QList<RouterHost>& hosts,
                                               QWidget* parent)
{
    QMap<HostId, BulkTeleInfo> out;

    RouterSession* session = RouterController::session(router_id);
    if (!session || hosts.isEmpty())
        return out;

    int pending = hosts.size();
    QEventLoop loop;
    QTimer::singleShot(15000, &loop, &QEventLoop::quit);

    for (const RouterHost& host : hosts)
    {
        session->requestHostTelemetry(
            host.host_id,
            {parent, [&, host_id = host.host_id](const proto::router::HostTelemetryResult& result)
             {
                 BulkTeleInfo info;
                 if (result.error_code() == proto::router::kErrorOk)
                 {
                     QJsonDocument doc =
                         QJsonDocument::fromJson(QByteArray::fromStdString(result.json()));
                     if (doc.isObject())
                     {
                         const QJsonObject update = doc.object().value("update").toObject();
                         info.ok = true;
                         info.channel = update.value("channel").toString();
                         info.address = update.value("address").toString();
                         info.key = update.value("public_key").toString();
                         info.auto_update = update.value("auto_update").toBool();
                         info.frequency = update.value("check_frequency").toInt(7);
                     }
                 }
                 out[host_id] = info;
                 if (--pending <= 0)
                     loop.quit();
             }});
    }
    loop.exec();
    return out;
}

} // namespace

//--------------------------------------------------------------------------------------------------
void RouterHostsWidget::onHostsAddUser()
{
    const QList<RouterHost> hosts = selectedHosts();
    if (hosts.isEmpty())
        return;

    AutoQPointer<RouterHostUserDialog> input(new RouterHostUserDialog(
        0, QString(), static_cast<quint32>(proto::peer::SESSION_TYPE_ALL),
        User::ENABLED, QStringList(), this));
    if (input->exec() != QDialog::Accepted)
        return;

    RouterUser user = RouterUser::create(input->userName(), input->password());
    if (!user.isValid())
    {
        MsgBox::warning(this, tr("Failed to create user credentials."));
        return;
    }
    user.sessions = input->sessions();
    user.flags = input->flags();

    const QString bulk_id = newBulkId();
    QJsonObject user_params;
    user_params["name"] = user.name;
    user_params["group"] = user.group;
    user_params["salt"] = QString::fromLatin1(user.salt.toHex());
    user_params["verifier"] = QString::fromLatin1(user.verifier.toHex());
    user_params["sessions"] = static_cast<qint64>(user.sessions);
    user_params["flags"] = static_cast<qint64>(user.flags);
    user_params["bulk_id"] = bulk_id;

    QList<RouterHostBulkTarget> targets;
    for (const RouterHost& host : hosts)
        targets.append({host.host_id, bulkHostLabel(host)});

    AutoQPointer<RouterHostBulkRunner> fetch(new RouterHostBulkRunner(
        router_id_, targets, "get_host_users",
        [](HostId) { return QJsonObject(); }, this));
    const QList<RouterHostBulkResult> fetched =
        execFetchBulk(fetch, tr("Reading user lists..."), this);

    QList<RouterHostBulkTarget> apply_targets;
    QList<RouterHostBulkPreviewRow> preview;
    for (int i = 0; i < hosts.size() && i < fetched.size(); ++i)
    {
        RouterHostBulkPreviewRow row;
        row.host = bulkHostLabel(hosts[i]);
        row.action = tr("Add user \"%1\"").arg(user.name);
        const RouterHostBulkResult& f = fetched[i];
        if (f.status != "ok")
        {
            row.will_run = true;
            row.target_index = apply_targets.size();
            row.detail = tr("List unreadable (%1), will try anyway.").arg(f.detail);
            apply_targets.append(targets[i]);
        }
        else
        {
            bool exists = false;
            QJsonDocument doc = QJsonDocument::fromJson(f.raw.toUtf8());
            if (doc.isArray())
            {
                for (const QJsonValue& v : doc.array())
                {
                    if (v.toObject().value("name").toString().compare(
                            user.name, Qt::CaseInsensitive) == 0)
                    {
                        exists = true;
                        break;
                    }
                }
            }
            if (exists)
            {
                row.will_run = false;
                row.detail = tr("Already exists, skipped.");
            }
            else
            {
                row.will_run = true;
                row.target_index = apply_targets.size();
                apply_targets.append(targets[i]);
            }
        }
        preview.append(row);
    }

    AutoQPointer<RouterHostBulkRunner> runner(new RouterHostBulkRunner(
        router_id_, apply_targets, "add_user",
        [user_params](HostId) { return user_params; }, this));
    AutoQPointer<RouterHostBulkDialog> dialog(
        new RouterHostBulkDialog(tr("Add user on hosts"), this));
    dialog->execBulk(runner, preview);
}

//--------------------------------------------------------------------------------------------------
void RouterHostsWidget::onHostsRemoveUser()
{
    const QList<RouterHost> hosts = selectedHosts();
    if (hosts.isEmpty())
        return;

    AutoQPointer<RouterHostBulkRemoveDialog> input(new RouterHostBulkRemoveDialog(this));
    if (input->exec() != QDialog::Accepted)
        return;

    const QString name = input->userName();
    const bool force = input->force();
    const QString bulk_id = newBulkId();

    QList<RouterHostBulkTarget> targets;
    for (const RouterHost& host : hosts)
        targets.append({host.host_id, bulkHostLabel(host)});

    AutoQPointer<RouterHostBulkRunner> fetch(new RouterHostBulkRunner(
        router_id_, targets, "get_host_users",
        [](HostId) { return QJsonObject(); }, this));
    const QList<RouterHostBulkResult> fetched =
        execFetchBulk(fetch, tr("Reading user lists..."), this);

    QList<RouterHostBulkTarget> apply_targets;
    QMap<HostId, qint64> entry_by_host;
    QList<RouterHostBulkPreviewRow> preview;
    for (int i = 0; i < hosts.size() && i < fetched.size(); ++i)
    {
        RouterHostBulkPreviewRow row;
        row.host = bulkHostLabel(hosts[i]);
        row.action = tr("Remove user \"%1\"").arg(name);
        const RouterHostBulkResult& f = fetched[i];
        if (f.status != "ok")
        {
            row.will_run = true;
            row.target_index = apply_targets.size();
            row.detail = tr("List unreadable (%1), will try anyway.").arg(f.detail);
            apply_targets.append(targets[i]);
        }
        else
        {
            qint64 entry_id = -1;
            bool enabled = false;
            int enabled_total = 0;
            QJsonDocument doc = QJsonDocument::fromJson(f.raw.toUtf8());
            if (doc.isArray())
            {
                for (const QJsonValue& v : doc.array())
                {
                    const QJsonObject o = v.toObject();
                    if (o.value("flags").toInteger() & User::ENABLED)
                        ++enabled_total;
                    if (o.value("name").toString().compare(name, Qt::CaseInsensitive) == 0)
                    {
                        entry_id = o.value("entry_id").toInteger(-1);
                        enabled = o.value("flags").toInteger() & User::ENABLED;
                    }
                }
            }
            if (entry_id < 0)
            {
                row.will_run = false;
                row.detail = tr("No such user, skipped.");
            }
            else
            {
                row.will_run = true;
                row.target_index = apply_targets.size();
                if (enabled && enabled_total <= 1)
                    row.detail = tr("Last enabled user, the host will refuse.");
                apply_targets.append(targets[i]);
                entry_by_host[hosts[i].host_id] = entry_id;
            }
        }
        preview.append(row);
    }

    AutoQPointer<RouterHostBulkRunner> runner(new RouterHostBulkRunner(
        router_id_, apply_targets, "remove_user",
        [&](HostId host_id)
        {
            QJsonObject params;
            if (entry_by_host.contains(host_id))
                params["entry_id"] = entry_by_host[host_id];
            params["name"] = name;
            if (force)
                params["force"] = true;
            params["bulk_id"] = bulk_id;
            return params;
        }, this));
    AutoQPointer<RouterHostBulkDialog> dialog(
        new RouterHostBulkDialog(tr("Remove user from hosts"), this));
    dialog->execBulk(runner, preview);
}

//--------------------------------------------------------------------------------------------------
void RouterHostsWidget::onHostsUpdateConfig()
{
    const QList<RouterHost> hosts = selectedHosts();
    if (hosts.isEmpty())
        return;

    const QMap<HostId, BulkTeleInfo> telemetries = fetchBulkTelemetries(router_id_, hosts, this);

    QSet<QString> channels, servers;
    int auto_on = 0, auto_known = 0;
    QSet<int> freqs;
    for (const RouterHost& host : hosts)
    {
        const BulkTeleInfo info = telemetries.value(host.host_id);
        if (!info.ok)
            continue;
        channels.insert(info.channel.isEmpty() ? tr("(unknown)") : info.channel);
        servers.insert(info.address.isEmpty() ? tr("(default)") : info.address);
        ++auto_known;
        if (info.auto_update)
            ++auto_on;
        freqs.insert(info.frequency);
    }
    QStringList channel_list(channels.values());
    channel_list.sort();
    QStringList server_list(servers.values());
    server_list.sort();
    QStringList freq_list;
    for (int freq : freqs)
        freq_list.append(QString::number(freq));
    freq_list.sort();
    const QString summary = tr("Hosts in run: %1. Channels: %2. Servers: %3. "
                               "Auto-update on: %4 of %5. Frequencies (days): %6.")
        .arg(hosts.size())
        .arg(channels.isEmpty() ? tr("(unknown)") : channel_list.join(", "))
        .arg(servers.isEmpty() ? tr("(unknown)") : server_list.join(", "))
        .arg(auto_on).arg(auto_known)
        .arg(freqs.isEmpty() ? tr("(unknown)") : freq_list.join(", "));

    AutoQPointer<RouterHostBulkConfigDialog> input(
        new RouterHostBulkConfigDialog(summary, this));
    if (input->exec() != QDialog::Accepted)
        return;

    QJsonObject wanted;
    if (input->changeServer())
        wanted["update_server"] = input->server();
    if (input->changePublicKey())
        wanted["update_public_key"] = input->publicKey();
    if (input->changeChannel())
        wanted["update_channel"] = input->channel();
    if (input->changeAutoUpdate())
        wanted["auto_update_enabled"] = input->autoUpdate();
    if (input->changeFrequency())
        wanted["update_check_frequency"] = input->frequency();

    if (wanted.isEmpty())
    {
        MsgBox::information(this, tr("No fields selected, nothing to do."));
        return;
    }

    const QString bulk_id = newBulkId();

    QList<RouterHostBulkTarget> apply_targets;
    QList<RouterHostBulkPreviewRow> preview;
    for (const RouterHost& host : hosts)
    {
        RouterHostBulkPreviewRow row;
        row.host = bulkHostLabel(host);
        const BulkTeleInfo info = telemetries.value(host.host_id);
        if (!info.ok)
        {
            row.will_run = true;
            row.target_index = apply_targets.size();
            row.action = tr("Set selected fields (current settings unknown).");
            apply_targets.append({host.host_id, row.host});
            preview.append(row);
            continue;
        }

        QStringList changes;
        if (wanted.contains("update_server") &&
            wanted["update_server"].toString() != info.address)
        {
            changes.append(tr("server: %1 -> %2")
                .arg(info.address.isEmpty() ? tr("(default)") : info.address,
                     wanted["update_server"].toString().isEmpty() ?
                         tr("(default)") : wanted["update_server"].toString()));
        }
        if (wanted.contains("update_public_key") &&
            wanted["update_public_key"].toString().toLower() != info.key.toLower())
        {
            changes.append(tr("public key: %1 -> %2")
                .arg(info.key.isEmpty() ? tr("(default)") : tr("(set)"),
                     wanted["update_public_key"].toString().isEmpty() ?
                         tr("(default)") : tr("(set)")));
        }
        if (wanted.contains("update_channel") &&
            wanted["update_channel"].toString() != info.channel)
        {
            changes.append(tr("channel: %1 -> %2").arg(info.channel, wanted["update_channel"].toString()));
        }
        if (wanted.contains("auto_update_enabled") &&
            wanted["auto_update_enabled"].toBool() != info.auto_update)
        {
            changes.append(tr("auto-update: %1 -> %2")
                .arg(info.auto_update ? tr("on") : tr("off"),
                     wanted["auto_update_enabled"].toBool() ? tr("on") : tr("off")));
        }
        if (wanted.contains("update_check_frequency") &&
            wanted["update_check_frequency"].toInt() != info.frequency)
        {
            changes.append(tr("frequency: %1 -> %2")
                .arg(info.frequency).arg(wanted["update_check_frequency"].toInt()));
        }

        if (changes.isEmpty())
        {
            row.will_run = false;
            row.action = tr("No changes.");
            row.detail = tr("Already matching, skipped.");
        }
        else
        {
            row.will_run = true;
            row.target_index = apply_targets.size();
            row.action = changes.join("; ");
            apply_targets.append({host.host_id, row.host});
        }
        preview.append(row);
    }

    AutoQPointer<RouterHostBulkRunner> runner(new RouterHostBulkRunner(
        router_id_, apply_targets, "set_update_config",
        [wanted, bulk_id](HostId)
        {
            QJsonObject params = wanted;
            params["bulk_id"] = bulk_id;
            return params;
        }, this));
    AutoQPointer<RouterHostBulkDialog> dialog(
        new RouterHostBulkDialog(tr("Update configuration on hosts"), this));
    dialog->execBulk(runner, preview);
}

//--------------------------------------------------------------------------------------------------
void RouterHostsWidget::onHostsFullConfig()
{
    const QList<RouterHost> hosts = selectedHosts();
    if (hosts.isEmpty())
        return;

    const QString file_name = QFileDialog::getOpenFileName(this, tr("Open host configuration file"),
                                                           QString(),
                                                           tr("JSON files (*.json);;All files (*)"));
    if (file_name.isEmpty())
        return;

    QFile file(file_name);
    if (!file.open(QIODevice::ReadOnly))
    {
        MsgBox::warning(this, tr("Cannot open file: %1").arg(file.errorString()));
        return;
    }
    const QByteArray data = file.readAll();
    file.close();

    QJsonParseError parse_error;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !doc.isObject())
    {
        MsgBox::warning(this, tr("Invalid JSON: %1").arg(parse_error.errorString()));
        return;
    }
    const QJsonObject cfg = doc.object();
    if (cfg.isEmpty() || (!cfg.contains("system") && !cfg.contains("database")))
    {
        MsgBox::warning(this, tr("Not a host configuration file (no system/database sections)."));
        return;
    }

    int file_users = -1;
    if (cfg.contains("database") && cfg["database"].isObject() &&
        cfg["database"].toObject().contains("users"))
    {
        file_users = cfg["database"].toObject()["users"].toArray().size();
    }
    QStringList file_sections;
    if (cfg.contains("system"))
        file_sections.append("system");
    if (cfg.contains("database"))
        file_sections.append("database");

    AutoQPointer<RouterHostBulkFullConfigDialog> input(new RouterHostBulkFullConfigDialog(
        tr("File: %1\nSections: %2. Users in file: %3.")
            .arg(file_name, file_sections.join(", "),
                 file_users < 0 ? tr("(kept)") : QString::number(file_users)),
        this));
    if (input->exec() != QDialog::Accepted)
        return;

    QJsonArray keep_local;
    if (input->keepRouter())
        keep_local.append("database");

    const QString bulk_id = newBulkId();
    const QString config_json =
        QString::fromUtf8(QJsonDocument(cfg).toJson(QJsonDocument::Compact));

    QList<RouterHostBulkTarget> targets;
    for (const RouterHost& host : hosts)
        targets.append({host.host_id, bulkHostLabel(host)});

    AutoQPointer<RouterHostBulkRunner> dry(new RouterHostBulkRunner(
        router_id_, targets, "apply_full_config",
        [&](HostId)
        {
            QJsonObject params;
            params["config_json"] = config_json;
            params["keep_local"] = keep_local;
            params["dry_run"] = true;
            params["bulk_id"] = bulk_id;
            return params;
        }, this));
    const QList<RouterHostBulkResult> dried =
        execFetchBulk(dry, tr("Dry run on hosts..."), this);

    QList<RouterHostBulkTarget> apply_targets;
    QList<RouterHostBulkPreviewRow> preview;
    for (int i = 0; i < hosts.size() && i < dried.size(); ++i)
    {
        RouterHostBulkPreviewRow row;
        row.host = bulkHostLabel(hosts[i]);
        const RouterHostBulkResult& d = dried[i];
        if (d.status != "ok")
        {
            row.will_run = true;
            row.target_index = apply_targets.size();
            row.action = tr("Apply full config (dry run %1, will try anyway).").arg(d.detail);
            apply_targets.append(targets[i]);
        }
        else
        {
            QJsonDocument diff_doc = QJsonDocument::fromJson(d.raw.toUtf8());
            QStringList parts;
            if (diff_doc.isObject())
            {
                const QJsonObject diff = diff_doc.object();
                if (diff.contains("users"))
                {
                    const QJsonObject users = diff["users"].toObject();
                    parts.append(tr("users: %1 -> %2")
                        .arg(users.value("old_count").toInteger())
                        .arg(users.value("new_count").toInteger()));
                }
                if (diff.contains("system"))
                {
                    parts.append(tr("system: %n field(s) differ.",
                                     "", diff["system"].toObject().size()));
                }
            }
            row.will_run = true;
            row.target_index = apply_targets.size();
            row.action = parts.isEmpty() ? tr("Apply full config (no visible diff).")
                                         : parts.join("; ");
            apply_targets.append(targets[i]);
        }
        preview.append(row);
    }

    AutoQPointer<RouterHostBulkRunner> runner(new RouterHostBulkRunner(
        router_id_, apply_targets, "apply_full_config",
        [&](HostId)
        {
            QJsonObject params;
            params["config_json"] = config_json;
            params["keep_local"] = keep_local;
            params["dry_run"] = false;
            params["bulk_id"] = bulk_id;
            return params;
        }, this));
    AutoQPointer<RouterHostBulkDialog> dialog(
        new RouterHostBulkDialog(tr("Full configuration on hosts"), this));
    dialog->execBulk(runner, preview);
}
