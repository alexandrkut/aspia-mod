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

#ifndef CLIENT_DESKTOP_MANAGEMENT_ROUTER_HOST_DIALOG_H
#define CLIENT_DESKTOP_MANAGEMENT_ROUTER_HOST_DIALOG_H

#include <QDialog>
#include <QCheckBox>

#include <memory>

#include "client/router_types.h"

class QAbstractButton;

namespace Ui {
class RouterHostDialog;
} // namespace Ui

namespace proto::router {
class HostResult;
} // namespace proto::router

class RouterHostDialog final : public QDialog
{
    Q_OBJECT

public:
    RouterHostDialog(qint64 router_id, const RouterHost& host, QWidget* parent);
    ~RouterHostDialog() final;

    // The edit dialog turns the checked workspace list into the membership list it sends:
    // every kept membership keeps the group the list reply carried (the router reads a zero
    // as a move to the workspace root), a newly checked workspace starts at the root, and
    // the group picker's value applies to the primary workspace only. Pure, so the
    // serialization can be tested without the widget.
    static QList<QPair<qint64, qint64>> buildMemberships(
        const QList<QPair<qint64, qint64>>& existing,
        const QList<qint64>& checked_workspace_ids,
        qint64 primary_workspace_id,
        qint64 primary_group_id);

private slots:
    void onWorkspaceListReceived(const RouterWorkspaceList& list);
    void onGroupListReceived(const RouterGroupList& list);
    void onHostResultReceived(const proto::router::HostResult& result);
    void onSavedCredentialsToggled(bool checked);
    void onButtonBoxClicked(QAbstractButton* button);
    void onWorkspacesButtonClicked();
    void onLoadData();

private:
    qint64 currentWorkspaceId() const;
    void fetchGroups();
    void updateWorkspacesButton();
    bool saveCredentials();
    void setCredentialsEnabled(bool enable);

    std::unique_ptr<Ui::RouterHostDialog> ui;
    qint64 router_id_ = 0;
    RouterHost host_;
    bool credentials_loaded_ = false;

    // The workspace picker: a host may belong to several workspaces at once, so a button in
    // the dialog opens a check list of every space of the router. The selection lives here
    // between the popup and the submit.
    QList<RouterWorkspace> workspaces_;
    QList<qint64> checked_workspace_ids_;

    QCheckBox* checkbox_notify_view_ = nullptr;
    QCheckBox* checkbox_notify_control_ = nullptr;
    QCheckBox* checkbox_notify_files_ = nullptr;

    Q_DISABLE_COPY_MOVE(RouterHostDialog)
};

#endif // CLIENT_DESKTOP_MANAGEMENT_ROUTER_HOST_DIALOG_H
