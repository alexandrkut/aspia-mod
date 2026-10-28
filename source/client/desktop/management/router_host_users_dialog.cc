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
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include "base/logging.h"
#include "base/peer/router_user.h"
#include "client/router_controller.h"
#include "client/router_session.h"
#include "common/desktop/msg_box.h"

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

quint32 stringToSessions(const QString& s)
{
    quint32 mask = 0;
    const QStringList parts = s.split(',', Qt::SkipEmptyParts);
    for (const QString& p : parts)
    {
        if (p == "D") mask |= 1;
        else if (p == "F") mask |= 4;
        else if (p == "I") mask |= 8;
        else if (p == "C") mask |= 16;
        else if (p == "T") mask |= 32;
    }
    return mask;
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
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Add user"), tr("User name:"), QLineEdit::Normal, "", &ok);
    if (!ok || name.trimmed().isEmpty())
        return;

    const QString password = QInputDialog::getText(this, tr("Add user"), tr("Password:"), QLineEdit::Password, "", &ok);
    if (!ok || password.isEmpty())
        return;

    const QString sessions_str = QInputDialog::getText(this, tr("Add user"),
        tr("Sessions (D=Desktop, F=File, I=Info, C=Chat, T=Terminal):"), QLineEdit::Normal, "D,F,I,C,T", &ok);
    if (!ok)
        return;

    quint32 sessions = stringToSessions(sessions_str);
    if (sessions == 0)
        sessions = 63; // SESSION_TYPE_ALL

    addUser(name.trimmed(), password, sessions);
}

//--------------------------------------------------------------------------------------------------
void RouterHostUsersDialog::addUser(const QString& name, const QString& password, quint32 sessions)
{
    RouterUser user = RouterUser::create(name, SecureString(password));
    if (!user.isValid())
    {
        MsgBox::warning(this, tr("Failed to create user credentials."));
        return;
    }

    user.sessions = sessions;
    user.flags = User::ENABLED;

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

    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Edit user"), tr("User name:"),
        QLineEdit::Normal, info.name, &ok);
    if (!ok || name.trimmed().isEmpty())
        return;

    const QString password = QInputDialog::getText(this, tr("Edit user"),
        tr("New password (empty = keep current):"), QLineEdit::Password, "", &ok);
    if (!ok)
        return;

    const QString sessions_str = QInputDialog::getText(this, tr("Edit user"),
        tr("Sessions (D=Desktop, F=File, I=Info, C=Chat, T=Terminal):"),
        QLineEdit::Normal, sessionsToString(info.sessions), &ok);
    if (!ok)
        return;

    quint32 sessions = stringToSessions(sessions_str);
    if (sessions == 0)
        sessions = info.sessions;

    QJsonObject params;
    params["entry_id"] = info.entry_id;
    params["name"] = name.trimmed();
    params["sessions"] = static_cast<qint64>(sessions);

    if (!password.isEmpty())
    {
        RouterUser user = RouterUser::create(name.trimmed(), SecureString(password));
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
