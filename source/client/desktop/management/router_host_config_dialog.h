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

#ifndef CLIENT_DESKTOP_MANAGEMENT_ROUTER_HOST_CONFIG_DIALOG_H
#define CLIENT_DESKTOP_MANAGEMENT_ROUTER_HOST_CONFIG_DIALOG_H

#include <QDialog>

#include <memory>

#include "client/router_controller.h"
#include "client/router_session.h"

namespace proto::router {
class HostTelemetryResult;
} // namespace proto::router

class QComboBox;
class QLineEdit;
class QCheckBox;
class QSpinBox;
class QLabel;

class RouterHostConfigDialog final : public QDialog
{
    Q_OBJECT

public:
    RouterHostConfigDialog(qint64 router_id, qint64 host_id, QWidget* parent);
    ~RouterHostConfigDialog() final;

private slots:
    void onApply();
    void onTelemetryReceived(const proto::router::HostTelemetryResult& result);

private:
    void requestTelemetry();
    void applyResult(const proto::router::HostResult& result);
    bool validate() const;

    qint64 router_id_ = 0;
    qint64 host_id_ = 0;

    QLineEdit* edit_server_ = nullptr;
    QLineEdit* edit_public_key_ = nullptr;
    QComboBox* combo_channel_ = nullptr;
    QCheckBox* check_auto_update_ = nullptr;
    QSpinBox* spin_frequency_ = nullptr;
    QLabel* label_status_ = nullptr;
};

#endif // CLIENT_DESKTOP_MANAGEMENT_ROUTER_HOST_CONFIG_DIALOG_H
