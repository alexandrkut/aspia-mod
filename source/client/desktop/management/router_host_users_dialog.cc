//
// Aspia Project
// Copyright (C) 2016-2026 Dmitry Chapyshev <dmitry@aspia.ru>
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <https://www.gnu.org/licenses/>.
//

#include "client/desktop/management/router_host_users_dialog.h"

#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include "base/auto_qpointer.h"
#include "base/logging.h"
#include "base/peer/router_user.h"
#include "client/desktop/management/router_host_user_dialog.h"
#include "client/router_controller.h"
#include "client/router_session.h"
#include "common/desktop/msg_box.h"
#include "proto/peer.h"

namespace {

QString sessionsToString(quint32 mask)
{
    QStringList parts;
    if (mask & 1) parts << "D";
    if (mask & 4) parts << "F";
    if (mask & 8) parts << "I";
    if (mask & 16) parts << "C";
    if (mask & 32) parts << "T";
    return parts.join(",");
}

QStringList existingNames(const QList<RouterHostUsersDialog::UserInfo>& users)
{
    QStringList names;
    for (const auto& user : users)
        names.append(user.name);
    return names;
}

} // namespace

//--------------------------------------------------------------------------------------------------
RouterHostUsersDialog::RouterHostUsersDialog(qint64 router_id, qint64 host_id, QWidget* parent)
    : QDialog(parent),
      router_id_(router_id),
      host_id_(host_id)
{
    setWindowTitle(tr("Host users"));
    setMinimumSize(400, 300);

    auto* layout = new QVBoxLayout(this);

    list_ = new QListWidget(this);
    layout->addWidget(list_);

    auto* btn_layout = new QHBoxLayout();
    btn_add_ = new QPushButton(tr("Add"), this);
    btn_edit_ = new QPushButton(tr("Edit"), this);
    btn_remove_ = new QPushButton(tr("Remove"), this);
    auto* btn_close = new QPushButton(tr("Close"), this);

    btn_layout->addWidget(btn_add_);
    btn_layout->addWidget(btn_edit_);
    btn_layout->addWidget(btn_remove_);
    btn_layout->addStretch();
    btn_layout->addWidget(btn_close);
    layout->addLayout(btn_layout);

    connect(btn_add_, &QPushButton::clicked, this, &RouterHostUsersDialog::onAdd);
    connect(btn_edit_, &QPushButton::clicked, this, &RouterHostUsersDialog::onEdit);
    connect(btn_remove_, &QPushButton::clicked, this, &RouterHostUsersDialog::onRemove);
    connect(btn_close, &QPushButton::clicked, this, &QDialog::accept);

    fetchUsers();
}

//--------------------------------------------------------------------------------------------------
RouterHostUsersDialog::~RouterHostUsersDialog()
{
    // Nothing
}

//--------------------------------------------------------------------------------------------------
void RouterHostUsersDialog::fetchUsers()
{
    RouterSession* session = RouterController::session(router_id_);
    if (!session)
    {
        MsgBox::warning(this, tr("No session to router."));
        return;
    }

    session->setHostConfig(host_id_, "get_host_users", "{}",
                          {this, [this](const proto::router::HostResult& result)
                          {
                              onUsersReceived(result);
                          }});
}

//--------------------------------------------------------------------------------------------------
void RouterHostUsersDialog::onUsersReceived(const proto::router::HostResult& result)
{
    if (result.error_code() != proto::router::kErrorOk)
    {
        MsgBox::warning(this, tr("Failed to get users: %1").arg(QString::fromStdString(result.error_code())));
        return;
    }

    users_.clear();
    list_->clear();

    QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(result.result()));
    if (!doc.isArray())
        return;

    const QJsonArray arr = doc.array();
    for (const QJsonValue& v : arr)
    {
        const QJsonObject o = v.toObject();
        UserInfo info;
        info.entry_id = o.value("entry_id").toInteger();
        info.name = o.value("name").toString();
        info.group = o.value("group").toString();
        info.sessions = static_cast<quint32>(o.value("sessions").toInteger());
        info.flags = static_cast<quint32>(o.value("flags").toInteger());
        users_.append(info);

        QString label = info.name;
        if (!(info.flags & 1))
            label += tr(" (disabled)");
        label += " [" + sessionsToString(info.sessions) + "]";
        list_->addItem(label);
    }
}

//--------------------------------------------------------------------------------------------------
void RouterHostUsersDialog::onAdd()
{
    AutoQPointer<RouterHostUserDialog> dialog(new RouterHostUserDialog(
        0, QString(), static_cast<quint32>(proto::peer::SESSION_TYPE_ALL),
        User::ENABLED, existingNames(users_), this));
    if (dialog->exec() != QDialog::Accepted)
        return;

    RouterUser user = RouterUser::create(dialog->userName(), dialog->password());
    if (!user.isValid())
    {
        MsgBox::warning(this, tr("Failed to create user credentials."));
        return;
    }

    user.sessions = dialog->sessions();
    user.flags = dialog->flags();

    QJsonObject params;
    params["name"] = user.name;
    params["group"] = user.group;
    params["salt"] = QString::fromLatin1(user.salt.toHex());
    params["verifier"] = QString::fromLatin1(user.verifier.toHex());
    params["sessions"] = static_cast<qint64>(user.sessions);
    params["flags"] = static_cast<qint64>(user.flags);

    RouterSession* session = RouterController::session(router_id_);
    if (!session)
    {
        MsgBox::warning(this, tr("No session to router."));
        return;
    }

    const QString json = QString::fromUtf8(QJsonDocument(params).toJson(QJsonDocument::Compact));
    session->setHostConfig(host_id_, "add_user", json.toStdString(),
                          {this, [this](const proto::router::HostResult& result)
                          {
                              showResult(result, tr("Add"));
                              fetchUsers();
                          }});
}

//--------------------------------------------------------------------------------------------------
void RouterHostUsersDialog::onEdit()
{
    const int idx = list_->currentRow();
    if (idx < 0 || idx >= users_.size())
    {
        MsgBox::warning(this, tr("Select a user first."));
        return;
    }

    const UserInfo& info = users_[idx];

    AutoQPointer<RouterHostUserDialog> dialog(new RouterHostUserDialog(
        info.entry_id, info.name, info.sessions, info.flags, existingNames(users_), this));
    if (dialog->exec() != QDialog::Accepted)
        return;

    QJsonObject params;
    params["entry_id"] = info.entry_id;
    params["name"] = dialog->userName();
    params["sessions"] = static_cast<qint64>(dialog->sessions());
    params["flags"] = static_cast<qint64>(dialog->flags());

    if (dialog->passwordChanged())
    {
        RouterUser user = RouterUser::create(dialog->userName(), dialog->password());
        if (!user.isValid())
        {
            MsgBox::warning(this, tr("Failed to create user credentials."));
            return;
        }
        params["group"] = user.group;
        params["salt"] = QString::fromLatin1(user.salt.toHex());
        params["verifier"] = QString::fromLatin1(user.verifier.toHex());
    }

    RouterSession* session = RouterController::session(router_id_);
    if (!session)
    {
        MsgBox::warning(this, tr("No session to router."));
        return;
    }

    const QString json = QString::fromUtf8(QJsonDocument(params).toJson(QJsonDocument::Compact));
    session->setHostConfig(host_id_, "modify_user", json.toStdString(),
                          {this, [this](const proto::router::HostResult& result)
                          {
                              showResult(result, tr("Edit"));
                              fetchUsers();
                          }});
}

//--------------------------------------------------------------------------------------------------
void RouterHostUsersDialog::onRemove()
{
    const int idx = list_->currentRow();
    if (idx < 0 || idx >= users_.size())
    {
        MsgBox::warning(this, tr("Select a user first."));
        return;
    }

    const UserInfo& info = users_[idx];

    if (MsgBox::question(this, tr("Remove user \"%1\"?").arg(info.name)) != MsgBox::Yes)
        return;

    QJsonObject params;
    params["entry_id"] = info.entry_id;

    RouterSession* session = RouterController::session(router_id_);
    if (!session)
    {
        MsgBox::warning(this, tr("No session to router."));
        return;
    }

    const QString json = QString::fromUtf8(QJsonDocument(params).toJson(QJsonDocument::Compact));
    session->setHostConfig(host_id_, "remove_user", json.toStdString(),
                          {this, [this](const proto::router::HostResult& result)
                          {
                              showResult(result, tr("Remove"));
                              fetchUsers();
                          }});
}

//--------------------------------------------------------------------------------------------------
void RouterHostUsersDialog::showResult(const proto::router::HostResult& result, const QString& action)
{
    if (result.error_code() == proto::router::kErrorOk)
    {
        MsgBox::information(this, tr("%1: success.").arg(action));
    }
    else
    {
        MsgBox::warning(this, tr("%1 failed: %2").arg(action, QString::fromStdString(result.error_code())));
    }
}

//--------------------------------------------------------------------------------------------------
void RouterHostUsersDialog::refreshList()
{
    fetchUsers();
}
