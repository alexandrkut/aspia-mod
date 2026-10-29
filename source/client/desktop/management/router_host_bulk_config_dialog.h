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

#ifndef CLIENT_DESKTOP_MANAGEMENT_ROUTER_HOST_BULK_CONFIG_DIALOG_H
#define CLIENT_DESKTOP_MANAGEMENT_ROUTER_HOST_BULK_CONFIG_DIALOG_H

#include <QDialog>

class QAbstractButton;
class QCheckBox;
class QComboBox;
class QLineEdit;
class QSpinBox;

// Bulk update-server settings: uniform values for the fleet, but every field has its own
// "change" checkbox. Unchecked fields are omitted from the params, so hosts keep them.
class RouterHostBulkConfigDialog final : public QDialog
{
    Q_OBJECT

public:
    explicit RouterHostBulkConfigDialog(const QString& fleet_summary, QWidget* parent = nullptr);
    ~RouterHostBulkConfigDialog() final;

    bool changeServer() const;
    QString server() const;
    bool changePublicKey() const;
    QString publicKey() const;
    bool changeChannel() const;
    QString channel() const;
    bool changeAutoUpdate() const;
    bool autoUpdate() const;
    bool changeFrequency() const;
    int frequency() const;

private slots:
    void onButtonClicked(QAbstractButton* button);
    void updateEditors();

private:
    bool validate() const;

    QCheckBox* check_server_ = nullptr;
    QLineEdit* edit_server_ = nullptr;
    QCheckBox* check_key_ = nullptr;
    QLineEdit* edit_key_ = nullptr;
    QCheckBox* check_channel_ = nullptr;
    QComboBox* combo_channel_ = nullptr;
    QCheckBox* check_auto_ = nullptr;
    QCheckBox* edit_auto_ = nullptr;
    QCheckBox* check_frequency_ = nullptr;
    QSpinBox* spin_frequency_ = nullptr;

    Q_DISABLE_COPY_MOVE(RouterHostBulkConfigDialog)
};

#endif // CLIENT_DESKTOP_MANAGEMENT_ROUTER_HOST_BULK_CONFIG_DIALOG_H
