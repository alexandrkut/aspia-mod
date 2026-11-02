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

#include "client/desktop/management/router_host_config_dialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QUrl>
#include <QVBoxLayout>

#include "base/logging.h"
#include "client/router_controller.h"
#include "client/router_session.h"
#include "common/desktop/msg_box.h"
#include "proto/router_admin.h"
#include "proto/router_client.h"

namespace {

bool isValidUrl(const QString& url)
{
    if (url.isEmpty())
        return true;
    const QUrl parsed(url, QUrl::StrictMode);
    return parsed.isValid() && !parsed.host().isEmpty() &&
           (parsed.scheme() == "https" || parsed.scheme() == "http");
}

bool isValidKeyHex(const QString& hex)
{
    if (hex.isEmpty())
        return true;
    const QByteArray bytes = QByteArray::fromHex(hex.toLower().toLatin1());
    return bytes.size() == 32 && bytes.toHex() == hex.toLower().toLatin1();
}

} // namespace

//--------------------------------------------------------------------------------------------------
RouterHostConfigDialog::RouterHostConfigDialog(qint64 router_id, qint64 host_id, QWidget* parent)
    : QDialog(parent),
      router_id_(router_id),
      host_id_(host_id)
{
    setWindowTitle(tr("Host update configuration"));
    setMinimumWidth(420);

    auto* layout = new QFormLayout(this);

    edit_server_ = new QLineEdit(this);
    edit_server_->setPlaceholderText(tr("https://... (empty = default)"));
    layout->addRow(tr("Update server:"), edit_server_);

    edit_public_key_ = new QLineEdit(this);
    edit_public_key_->setPlaceholderText(tr("32 bytes hex (empty = default)"));
    layout->addRow(tr("Public key:"), edit_public_key_);

    combo_channel_ = new QComboBox(this);
    combo_channel_->addItem("stable");
    combo_channel_->addItem("beta");
    combo_channel_->addItem("alpha");
    layout->addRow(tr("Channel:"), combo_channel_);

    check_auto_update_ = new QCheckBox(tr("Auto-update enabled"), this);
    layout->addRow("", check_auto_update_);

    combo_frequency_ = new QComboBox(this);
    combo_frequency_->addItem(tr("Once a day"), 1);
    combo_frequency_->addItem(tr("Once a week"), 7);
    combo_frequency_->addItem(tr("Once a month"), 30);
    combo_frequency_->setCurrentIndex(1);
    layout->addRow(tr("Check frequency:"), combo_frequency_);

    label_status_ = new QLabel(tr("Loading current settings..."), this);
    layout->addRow("", label_status_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &RouterHostConfigDialog::onApply);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addRow(buttons);

    requestTelemetry();
}

//--------------------------------------------------------------------------------------------------
RouterHostConfigDialog::~RouterHostConfigDialog()
{
    // Nothing
}

//--------------------------------------------------------------------------------------------------
void RouterHostConfigDialog::requestTelemetry()
{
    RouterSession* session = RouterController::session(router_id_);
    if (!session)
    {
        label_status_->setText(tr("No session to router."));
        return;
    }

    session->requestHostTelemetry(host_id_, {this, &RouterHostConfigDialog::onTelemetryReceived});
}

//--------------------------------------------------------------------------------------------------
void RouterHostConfigDialog::onTelemetryReceived(const proto::router::HostTelemetryResult& result)
{
    if (result.error_code() != proto::router::kErrorOk)
    {
        label_status_->setText(tr("Failed to read telemetry: %1").arg(QString::fromStdString(result.error_code())));
        return;
    }

    const QByteArray json = QByteArray::fromStdString(result.json());
    QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isObject())
    {
        label_status_->setText(tr("Telemetry is not JSON."));
        return;
    }

    const QJsonObject update = doc.object().value("update").toObject();
    if (!update.isEmpty())
    {
        edit_server_->setText(update.value("address").toString());
        check_auto_update_->setChecked(update.value("auto_update").toBool());
        const int frequency_index =
            combo_frequency_->findData(update.value("check_frequency").toInt(7));
        combo_frequency_->setCurrentIndex(frequency_index >= 0 ? frequency_index : 1);
        combo_channel_->setCurrentText(update.value("channel").toString("stable"));
        const QString key = update.value("public_key").toString();
        if (!key.isEmpty())
            edit_public_key_->setText(key);
    }

    label_status_->setText(tr("Current settings loaded."));
}

//--------------------------------------------------------------------------------------------------
bool RouterHostConfigDialog::validate() const
{
    if (!isValidUrl(edit_server_->text().trimmed()))
    {
        MsgBox::warning(const_cast<RouterHostConfigDialog*>(this), tr("Invalid server URL."));
        return false;
    }
    if (!isValidKeyHex(edit_public_key_->text().trimmed()))
    {
        MsgBox::warning(const_cast<RouterHostConfigDialog*>(this), tr("Invalid public key (must be 32 bytes hex)."));
        return false;
    }
    return true;
}

//--------------------------------------------------------------------------------------------------
void RouterHostConfigDialog::onApply()
{
    if (!validate())
        return;

    RouterSession* session = RouterController::session(router_id_);
    if (!session)
    {
        MsgBox::warning(this, tr("No session to router."));
        return;
    }

    QJsonObject params;
    params["update_server"] = edit_server_->text().trimmed();
    params["update_public_key"] = edit_public_key_->text().trimmed();
    params["update_channel"] = combo_channel_->currentText();
    params["auto_update_enabled"] = check_auto_update_->isChecked();
    params["update_check_frequency"] = combo_frequency_->currentData().toInt();

    const QString json = QString::fromUtf8(QJsonDocument(params).toJson(QJsonDocument::Compact));
    session->setHostConfig(host_id_, "set_update_config", json.toStdString(),
                          {this, [this](const proto::router::HostResult& result)
                          {
                              applyResult(result);
                          }});
}

//--------------------------------------------------------------------------------------------------
void RouterHostConfigDialog::applyResult(const proto::router::HostResult& result)
{
    if (result.error_code() == proto::router::kErrorOk)
    {
        MsgBox::information(this, tr("Configuration applied successfully."));
        accept();
    }
    else
    {
        MsgBox::warning(this, tr("Failed to apply: %1").arg(QString::fromStdString(result.error_code())));
    }
}
