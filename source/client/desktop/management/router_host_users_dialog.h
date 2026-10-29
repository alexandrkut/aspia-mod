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

#ifndef CLIENT_DESKTOP_MANAGEMENT_ROUTER_HOST_USERS_DIALOG_H
#define CLIENT_DESKTOP_MANAGEMENT_ROUTER_HOST_USERS_DIALOG_H

#include <QDialog>

#include <memory>

#include "base/peer/router_user.h"
#include "client/router_controller.h"
#include "client/router_session.h"

namespace proto::router {
class HostResult;
class HostTelemetryResult;
} // namespace proto::router

class QListWidget;
class QListWidgetItem;
class QPushButton;

class RouterHostUsersDialog final : public QDialog
{
    Q_OBJECT

public:
    RouterHostUsersDialog(qint64 router_id, qint64 host_id, QWidget* parent);
    ~RouterHostUsersDialog() final;

    struct UserInfo {
        qint64 entry_id = 0;
        QString name;
        QString group;
        quint32 sessions = 0;
        quint32 flags = 0;
    };

private slots:
    void onAdd();
    void onEdit();
    void onRemove();
    void onUsersReceived(const proto::router::HostResult& result);

private:
    void fetchUsers();
    void removeUser(int index);
    void refreshList();
    void showResult(const proto::router::HostResult& result, const QString& action);

    qint64 router_id_ = 0;
    qint64 host_id_ = 0;

    QList<UserInfo> users_;
    QListWidget* list_ = nullptr;
    QPushButton* btn_add_ = nullptr;
    QPushButton* btn_edit_ = nullptr;
    QPushButton* btn_remove_ = nullptr;
};

#endif // CLIENT_DESKTOP_MANAGEMENT_ROUTER_HOST_USERS_DIALOG_H
