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

#include "client/desktop/management/router_host_dialog.h"

#include <QAbstractButton>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QJsonDocument>
#include <QJsonObject>
#include <QIcon>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTimer>

#include <algorithm>

#include "base/logging.h"
#include "base/crypto/secure_string.h"
#include "base/peer/host_id.h"
#include "client/config.h"
#include "client/database.h"
#include "client/router_controller.h"
#include "client/settings.h"
#include "client/desktop/management/group_combo_box.h"
#include "client/desktop/management/ui_router_host_dialog.h"
#include "common/desktop/msg_box.h"
#include "common/desktop/router_error.h"
#include "common/desktop/password_edit.h"
#include "proto/router_constants.h"
#include "proto/router_manager.h"

//--------------------------------------------------------------------------------------------------
RouterHostDialog::RouterHostDialog(qint64 router_id, const RouterHost& host, QWidget* parent)
    : QDialog(parent),
      ui(std::make_unique<Ui::RouterHostDialog>()),
      router_id_(router_id),
      host_(host)
{
    LOG(INFO) << "Ctor";
    ui->setupUi(this);

    // The workspace picker becomes a check list: a host may belong to several workspaces at
    // once. The combo stays in the ui file and is hidden; the list takes its grid cell.
    workspace_list_ = new QListWidget(this);
    workspace_list_->setObjectName(QStringLiteral("workspace_list"));
    workspace_list_->setMinimumHeight(ui->combo_workspace->minimumHeight());
    ui->combo_workspace->hide();
    ui->gridLayout->replaceWidget(ui->combo_workspace, workspace_list_);

    Settings settings;
    restoreGeometry(settings.dialogGeometry(objectName()));

    ui->edit_display_name->setText(host_.display_name);
    ui->edit_comment->setPlainText(host_.comment);

    ui->edit_password->setShowPasswordButtonVisible(true);

    RouterSession* session = RouterController::session(router_id_);
    if (session && session->config().sessionType() == proto::router::SESSION_TYPE_OPERATOR)
    {
        ui->edit_display_name->setReadOnly(true);
        ui->edit_comment->setReadOnly(true);
        ui->combo_group->setEnabled(false);
    }

    if (!session || session->config().sessionType() != proto::router::SESSION_TYPE_ADMIN)
        workspace_list_->setEnabled(false);

    // Notify options (per host), visible only for ADMIN router sessions.
    checkbox_notify_view_ = new QCheckBox(tr("Notify about viewing"), this);
    checkbox_notify_control_ = new QCheckBox(tr("Notify about control"), this);
    checkbox_notify_files_ = new QCheckBox(tr("Notify about files"), this);

    checkbox_notify_view_->setChecked(false);
    checkbox_notify_control_->setChecked(true);
    checkbox_notify_files_->setChecked(true);

    // Load stored values.
    {
        HostConfig notify_host = HostConfig::forRouterHost(router_id_, host_.host_id, host_.computer_name);
        QString value;
        for (const QString& key : notify_host.notifySettingsKeys())
        {
            value = Database::instance().readSetting(key);
            if (!value.isEmpty())
                break;
        }
        QJsonDocument doc = QJsonDocument::fromJson(value.toUtf8());
        QJsonObject obj = doc.isObject() ? doc.object() : QJsonObject();
        if (obj.contains("view")) checkbox_notify_view_->setChecked(obj.value("view").toBool());
        if (obj.contains("control")) checkbox_notify_control_->setChecked(obj.value("control").toBool());
        if (obj.contains("files")) checkbox_notify_files_->setChecked(obj.value("files").toBool());
    }

    bool is_admin = session && session->config().sessionType() == proto::router::SESSION_TYPE_ADMIN;
    checkbox_notify_view_->setVisible(is_admin);
    checkbox_notify_control_->setVisible(is_admin);
    checkbox_notify_files_->setVisible(is_admin);

    QGridLayout* grid = ui->gridLayout;
    int row = 8;
    grid->addWidget(checkbox_notify_view_, row++, 0, 1, 2);
    grid->addWidget(checkbox_notify_control_, row++, 0, 1, 2);
    grid->addWidget(checkbox_notify_files_, row++, 0, 1, 2);

    // A temporary host id is handed out at random and comes back for another machine, so what was
    // saved under it would be sent to a host the user never gave it to. Such a host is edited like
    // any other, it just has nowhere to keep credentials.
    if (isTempHostId(host_.host_id))
        setCredentialsEnabled(false);

    connect(ui->checkbox_saved_credentials, &QCheckBox::toggled, this, &RouterHostDialog::onSavedCredentialsToggled);
    connect(ui->button_box, &QDialogButtonBox::clicked, this, &RouterHostDialog::onButtonBoxClicked);

    int label_width = 0;
    for (const QLabel* label : { ui->label_workspace, ui->label_group, ui->label_display_name,
                                 ui->label_username, ui->label_password, ui->label_credential })
    {
        label_width = std::max(label_width, label->sizeHint().width());
    }

    ui->gridLayout->setColumnMinimumWidth(0, label_width);

    onSavedCredentialsToggled(ui->checkbox_saved_credentials->isChecked());

    ui->edit_display_name->setFocus();

    connect(&RouterController::instance(), &RouterController::sig_statusChanged, this,
            [this](qint64 router_id, RouterStatus status)
    {
        if (router_id == router_id_ && status != RouterStatus::ONLINE)
            reject();
    });

    QTimer::singleShot(MilliSeconds::zero(), this, &RouterHostDialog::onLoadData);

    // The combos are populated asynchronously. Ok stays disabled until the responses arrive so the
    // user cannot submit before knowing which workspace and group they have selected.
    ui->button_box->button(QDialogButtonBox::Ok)->setEnabled(false);

    if (!session)
    {
        LOG(ERROR) << "No session for router" << router_id_;
        return;
    }

    session->listWorkspaces(RouterSession::CachePolicy::RELOAD, 0,
                            { this, &RouterHostDialog::onWorkspaceListReceived });
}

//--------------------------------------------------------------------------------------------------
RouterHostDialog::~RouterHostDialog()
{
    LOG(INFO) << "Dtor";

    Settings settings;
    settings.setDialogGeometry(objectName(), saveGeometry());
}

//--------------------------------------------------------------------------------------------------
void RouterHostDialog::onWorkspaceListReceived(const RouterWorkspaceList& list)
{
    if (list.error_code != proto::router::kErrorOk)
    {
        LOG(ERROR) << "Unable to get the list of the workspaces:" << list.error_code;
        MsgBox::warning(this, tr("Failed to get list of spaces."));
        reject();
        return;
    }

    const QIcon workspace_icon(":/img/workspace.svg");

    // The checked rows are the membership list the router synchronizes to. With no memberships
    // on record (an older reply) the primary workspace is pre-checked so an edit does not
    // silently claim anything new.
    QList<qint64> assigned;
    if (!host_.memberships.isEmpty())
    {
        for (const auto& membership : std::as_const(host_.memberships))
            assigned.append(membership.first);
    }
    else
    {
        assigned.append(host_.workspace_id);
    }

    auto addItem = [&](const QString& text, qint64 workspace_id)
    {
        auto* item = new QListWidgetItem(workspace_icon, text, workspace_list_);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setData(Qt::UserRole, workspace_id);
        item->setCheckState(assigned.contains(workspace_id) ? Qt::Checked : Qt::Unchecked);
    };

    for (const RouterWorkspace& workspace : std::as_const(list.workspaces))
        addItem(workspace.name, workspace.entry_id);

    fetchGroups();
}

//--------------------------------------------------------------------------------------------------
void RouterHostDialog::onGroupListReceived(const RouterGroupList& list)
{
    if (list.workspace_id != currentWorkspaceId())
        return;

    if (list.error_code != proto::router::kErrorOk)
    {
        LOG(ERROR) << "Unable to get the list of the groups:" << list.error_code;
        MsgBox::warning(this, tr("Failed to get list of groups."));
        reject();
        return;
    }

    QList<GroupComboBox::Entry> entries;
    entries.reserve(list.groups.size());
    for (const RouterGroup& group : std::as_const(list.groups))
    {
        GroupComboBox::Entry& entry = entries.emplaceBack();
        entry.id = group.entry_id;
        entry.parent_id = group.parent_id;
        entry.name = group.name;
    }

    ui->combo_group->loadGroupsWithNone(tr("Not assigned"), entries);
    ui->combo_group->selectGroup(list.workspace_id == host_.workspace_id ? host_.group_id : 0);

    ui->button_box->button(QDialogButtonBox::Ok)->setEnabled(true);
}

//--------------------------------------------------------------------------------------------------
void RouterHostDialog::onHostResultReceived(const proto::router::HostResult& result)
{
    const std::string& error_code = result.error_code();
    if (error_code != proto::router::kErrorOk)
    {
        setEnabled(true);
        MsgBox::warning(this, routerErrorText(error_code));
        return;
    }

    accept();
}

//--------------------------------------------------------------------------------------------------
void RouterHostDialog::onSavedCredentialsToggled(bool checked)
{
    if (checked && ui->edit_username->text().isEmpty() != ui->edit_password->password().isEmpty())
    {
        ui->edit_username->clear();
        ui->edit_password->clear();
    }

    ui->label_username->setVisible(!checked);
    ui->edit_username->setVisible(!checked);
    ui->label_password->setVisible(!checked);
    ui->edit_password->setVisible(!checked);
    ui->label_credential->setVisible(checked);
    ui->combo_credential->setVisible(checked);
    ui->label_credentials_note->setVisible(!checked);
}

//--------------------------------------------------------------------------------------------------
void RouterHostDialog::onButtonBoxClicked(QAbstractButton* button)
{
    QDialogButtonBox::StandardButton standard_button = ui->button_box->standardButton(button);
    if (standard_button != QDialogButtonBox::Ok)
    {
        LOG(INFO) << "[ACTION] Edit host rejected";
        reject();
        return;
    }

    RouterSession* session = RouterController::session(router_id_);
    if (!session)
    {
        LOG(ERROR) << "No session for router" << router_id_;
        reject();
        return;
    }

    if (ui->edit_username->text().isEmpty() != ui->edit_password->password().isEmpty())
    {
        MsgBox::warning(this, tr("Enter both the user name and the password, or leave both empty."));
        return;
    }

    // The checked workspaces become the membership list; the router synchronizes to it and
    // re-derives the primary workspace itself. Groups are not per-membership here: the picker
    // below keeps editing the group of the primary workspace only.
    host_.memberships.clear();
    for (int i = 0; i < workspace_list_->count(); ++i)
    {
        QListWidgetItem* item = workspace_list_->item(i);
        if (item->checkState() == Qt::Checked)
        {
            host_.memberships.append(
                qMakePair(item->data(Qt::UserRole).toLongLong(), static_cast<qint64>(0)));
        }
    }

    RouterHost host = host_;
    host.workspace_id = currentWorkspaceId();
    host.group_id     = ui->combo_group->currentGroupId();
    host.display_name = ui->edit_display_name->text();
    host.comment      = ui->edit_comment->toPlainText();

    // The credentials live on this computer only, so they are not the router's to accept or
    // refuse. Kept for the answer, they would be lost with any error of it. A failure to keep them
    // stops here, with what was typed still in the form.
    if (!saveCredentials())
    {
        MsgBox::warning(this, tr("Failed to save the credentials."));
        return;
    }

    if (session->config().sessionType() == proto::router::SESSION_TYPE_OPERATOR)
    {
        // Save notify settings (if admin visible).
        {
            QJsonObject obj;
            obj.insert("view", checkbox_notify_view_ ? checkbox_notify_view_->isChecked() : false);
            obj.insert("control", checkbox_notify_control_ ? checkbox_notify_control_->isChecked() : true);
            obj.insert("files", checkbox_notify_files_ ? checkbox_notify_files_->isChecked() : true);
            QJsonDocument doc(obj);
            HostConfig notify_host = HostConfig::forRouterHost(router_id_, host_.host_id, host_.computer_name);
            Database::instance().writeSetting(notify_host.notifySettingsKeys().front(), QString::fromUtf8(doc.toJson(QJsonDocument::Compact)));
        }
        LOG(INFO) << "[ACTION] Edit host accepted, credentials saved";
        accept();
        return;
    }

    // Save notify settings.
    {
        QJsonObject obj;
        obj.insert("view", checkbox_notify_view_ ? checkbox_notify_view_->isChecked() : false);
        obj.insert("control", checkbox_notify_control_ ? checkbox_notify_control_->isChecked() : true);
        obj.insert("files", checkbox_notify_files_ ? checkbox_notify_files_->isChecked() : true);
        QJsonDocument doc(obj);
        HostConfig notify_host = HostConfig::forRouterHost(router_id_, host_.host_id, host_.computer_name);
        Database::instance().writeSetting(notify_host.notifySettingsKeys().front(), QString::fromUtf8(doc.toJson(QJsonDocument::Compact)));
    }

    LOG(INFO) << "[ACTION] Edit host accepted, sending request";
    setEnabled(false);
    session->editHost(host, { this, &RouterHostDialog::onHostResultReceived });
}

//--------------------------------------------------------------------------------------------------
void RouterHostDialog::onLoadData()
{
    // A temporary host keeps no credentials, so there is nothing to read for it.
    if (isTempHostId(host_.host_id))
        return;

    Database& db = Database::instance();

    QList<CredentialConfig> saved_credentials;
    const Database::ReadResult credentials_result = db.credentialList(
        &saved_credentials, CredentialConfig::Type::ASPIA);
    if (credentials_result == Database::ReadResult::FAILED)
    {
        LOG(ERROR) << "Unable to read the list of credentials";
        MsgBox::warning(this, tr("Failed to read the list of credentials."));
        reject();
        return;
    }

    // The combo names them by a plain column, so one that did not open is picked like any other.
    if (credentials_result == Database::ReadResult::INCOMPLETE)
        LOG(ERROR) << "Unable to read some of the credentials";

    const QIcon credential_icon(":/img/keys.svg");
    const QIcon unread_credential_icon(":/img/key-corrupted.svg");

    for (const CredentialConfig& credential : std::as_const(saved_credentials))
    {
        ui->combo_credential->addItem(credential.isValid() ? credential_icon : unread_credential_icon,
                                      credential.displayName(),
                                      QVariant::fromValue(credential.id()));
    }

    RouterHostConfig credentials;
    const Database::FindResult credentials_found = db.findRouterHost(router_id_, host_.host_id, &credentials);
    credentials_loaded_ = credentials_found != Database::FindResult::FAILED;

    if (credentials_found == Database::FindResult::FOUND || credentials_found == Database::FindResult::UNREADABLE)
    {
        ui->edit_username->setText(credentials.username());
        ui->edit_password->setPassword(credentials.password());

        if (credentials.credentialId() > 0)
        {
            ui->checkbox_saved_credentials->setChecked(true);
            ui->combo_credential->setCurrentIndex(ui->combo_credential->findData(
                QVariant::fromValue(credentials.credentialId())));
        }
    }

    // Nothing to share until a record of credentials is added. A record that did not open is
    // in the list too, so a host never refers to one the combo does not hold.
    ui->checkbox_saved_credentials->setEnabled(ui->combo_credential->count() > 0);

    onSavedCredentialsToggled(ui->checkbox_saved_credentials->isChecked());

    if (credentials_found == Database::FindResult::FAILED)
    {
        LOG(ERROR) << "Unable to read the credentials of host" << host_.host_id;
        setCredentialsEnabled(false);
        MsgBox::warning(this, tr("Failed to read the credentials of the host."));
    }
    else if (credentials_found == Database::FindResult::UNREADABLE)
    {
        LOG(ERROR) << "Credentials of host" << host_.host_id << "could not be read";
        MsgBox::warning(this, tr("The credentials of the host are damaged. Enter them again."));
    }
}

//--------------------------------------------------------------------------------------------------
qint64 RouterHostDialog::currentWorkspaceId() const
{
    // The primary workspace of the host: the dialog no longer moves it (the router re-derives
    // it from the membership list after every edit), it only feeds the group picker.
    return host_.workspace_id;
}

//--------------------------------------------------------------------------------------------------
void RouterHostDialog::fetchGroups()
{
    ui->combo_group->clearGroups();

    RouterSession* session = RouterController::session(router_id_);
    if (!session)
    {
        LOG(ERROR) << "No session for router" << router_id_;
        return;
    }

    const qint64 workspace_id = currentWorkspaceId();
    const bool is_operator = session->config().sessionType() == proto::router::SESSION_TYPE_OPERATOR;

    ui->combo_group->setEnabled(workspace_id > 0 && !is_operator);

    // A host that belongs to no workspace lies in no group, and the router refuses a group list
    // request without a workspace.
    if (workspace_id <= 0)
    {
        ui->button_box->button(QDialogButtonBox::Ok)->setEnabled(true);
        return;
    }

    ui->button_box->button(QDialogButtonBox::Ok)->setEnabled(false);
    session->listGroups(RouterSession::CachePolicy::RELOAD, workspace_id,
                        { this, &RouterHostDialog::onGroupListReceived });
}

//--------------------------------------------------------------------------------------------------
bool RouterHostDialog::saveCredentials()
{
    if (isTempHostId(host_.host_id))
        return true;

    if (!credentials_loaded_)
        return true;

    Database& db = Database::instance();

    const qint64 credential_id =
        ui->checkbox_saved_credentials->isChecked() ? ui->combo_credential->currentData().toLongLong() : 0;
    const QString username = ui->edit_username->text();
    const SecureString password = ui->edit_password->password();

    RouterHostConfig stored;
    const Database::FindResult stored_found = db.findRouterHost(router_id_, host_.host_id, &stored);
    if (stored_found == Database::FindResult::FAILED)
    {
        LOG(ERROR) << "Unable to read the credentials of host" << host_.host_id;
        return false;
    }

    const bool row_exists = stored_found != Database::FindResult::NOT_FOUND;

    // Remembered by neither a pair nor a record of credentials, the host has no row.
    if (username.isEmpty() && credential_id <= 0)
    {
        // The form of a record that did not open comes up empty, and left that way it says
        // nothing about the row: the user came to edit the host, not to drop credentials they
        // were never shown.
        if (stored_found == Database::FindResult::UNREADABLE)
            return true;

        if (db.removeRouterHost(router_id_, host_.host_id))
            return true;

        LOG(ERROR) << "Unable to remove credentials of host" << host_.host_id;
        return false;
    }

    RouterHostConfig credentials;
    credentials.setRouterId(router_id_);
    credentials.setHostId(host_.host_id);
    credentials.setCredentialId(credential_id);
    credentials.setUsername(username);
    credentials.setPassword(password);

    bool saved = false;

    if (row_exists)
        saved = db.modifyRouterHost(credentials);
    else
        saved = db.addRouterHost(credentials);

    if (!saved)
        LOG(ERROR) << "Unable to save credentials of host" << host_.host_id;

    return saved;
}

//--------------------------------------------------------------------------------------------------
void RouterHostDialog::setCredentialsEnabled(bool enable)
{
    ui->checkbox_saved_credentials->setEnabled(enable);
    ui->label_username->setEnabled(enable);
    ui->edit_username->setEnabled(enable);
    ui->label_password->setEnabled(enable);
    ui->edit_password->setEnabled(enable);
    ui->label_credential->setEnabled(enable);
    ui->combo_credential->setEnabled(enable);
}
