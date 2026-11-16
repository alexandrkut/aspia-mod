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

#include "client/desktop/management/router_host_bulk_config_dialog.h"

#include <QAbstractButton>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QUrl>
#include <QVBoxLayout>

#include "common/desktop/msg_box.h"

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
RouterHostBulkConfigDialog::RouterHostBulkConfigDialog(const QString& fleet_summary, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Update configuration on hosts"));
    setMinimumWidth(480);

    auto* layout = new QVBoxLayout(this);

    auto* summary = new QLabel(fleet_summary, this);
    summary->setWordWrap(true);
    layout->addWidget(summary);

    auto* form = new QFormLayout();
    layout->addLayout(form);

    check_server_ = new QCheckBox(tr("Update server:"), this);
    edit_server_ = new QLineEdit(this);
    edit_server_->setPlaceholderText(tr("https://... (empty = default)"));
    form->addRow(check_server_, edit_server_);

    check_key_ = new QCheckBox(tr("Public key:"), this);
    edit_key_ = new QLineEdit(this);
    edit_key_->setPlaceholderText(tr("32 bytes hex (empty = default)"));
    form->addRow(check_key_, edit_key_);

    check_channel_ = new QCheckBox(tr("Channel:"), this);
    combo_channel_ = new QComboBox(this);
    combo_channel_->addItem("stable");
    combo_channel_->addItem("beta");
    combo_channel_->addItem("alpha");
    form->addRow(check_channel_, combo_channel_);

    check_auto_ = new QCheckBox(tr("Auto-update:"), this);
    edit_auto_ = new QCheckBox(tr("Enabled"), this);
    form->addRow(check_auto_, edit_auto_);

    check_frequency_ = new QCheckBox(tr("Check frequency:"), this);
    combo_frequency_ = new QComboBox(this);
    combo_frequency_->addItem(tr("Once a day"), 1);
    combo_frequency_->addItem(tr("Once a week"), 7);
    combo_frequency_->addItem(tr("Once a month"), 30);
    combo_frequency_->setCurrentIndex(1);
    form->addRow(check_frequency_, combo_frequency_);

    auto* hint = new QLabel(
        tr("Unchecked fields are left unchanged on every host. "
           "A checked field with an empty value resets it to the default."), this);
    hint->setWordWrap(true);
    layout->addWidget(hint);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::clicked,
            this, &RouterHostBulkConfigDialog::onButtonClicked);

    for (QCheckBox* check : { check_server_, check_key_, check_channel_,
                              check_auto_, check_frequency_ })
    {
        connect(check, &QCheckBox::toggled,
                this, &RouterHostBulkConfigDialog::updateEditors);
    }

    updateEditors();
}

//--------------------------------------------------------------------------------------------------
RouterHostBulkConfigDialog::~RouterHostBulkConfigDialog()
{
    // Nothing
}

//--------------------------------------------------------------------------------------------------
bool RouterHostBulkConfigDialog::changeServer() const { return check_server_->isChecked(); }
QString RouterHostBulkConfigDialog::server() const { return edit_server_->text().trimmed(); }
bool RouterHostBulkConfigDialog::changePublicKey() const { return check_key_->isChecked(); }
QString RouterHostBulkConfigDialog::publicKey() const { return edit_key_->text().trimmed(); }
bool RouterHostBulkConfigDialog::changeChannel() const { return check_channel_->isChecked(); }
QString RouterHostBulkConfigDialog::channel() const { return combo_channel_->currentText(); }
bool RouterHostBulkConfigDialog::changeAutoUpdate() const { return check_auto_->isChecked(); }
bool RouterHostBulkConfigDialog::autoUpdate() const { return edit_auto_->isChecked(); }
bool RouterHostBulkConfigDialog::changeFrequency() const { return check_frequency_->isChecked(); }
int RouterHostBulkConfigDialog::frequency() const { return combo_frequency_->currentData().toInt(); }

//--------------------------------------------------------------------------------------------------
void RouterHostBulkConfigDialog::onButtonClicked(QAbstractButton* button)
{
    auto* button_box = qobject_cast<QDialogButtonBox*>(sender());
    if (!button_box || button_box->standardButton(button) != QDialogButtonBox::Ok)
    {
        reject();
        return;
    }

    if (!validate())
        return;

    accept();
}

//--------------------------------------------------------------------------------------------------
void RouterHostBulkConfigDialog::updateEditors()
{
    edit_server_->setEnabled(check_server_->isChecked());
    edit_key_->setEnabled(check_key_->isChecked());
    combo_channel_->setEnabled(check_channel_->isChecked());
    edit_auto_->setEnabled(check_auto_->isChecked());
    combo_frequency_->setEnabled(check_frequency_->isChecked());
}

//--------------------------------------------------------------------------------------------------
bool RouterHostBulkConfigDialog::validate() const
{
    if (changeServer() && !isValidUrl(server()))
    {
        MsgBox::warning(const_cast<RouterHostBulkConfigDialog*>(this),
                        tr("Invalid server URL."));
        return false;
    }
    if (changePublicKey() && !isValidKeyHex(publicKey()))
    {
        MsgBox::warning(const_cast<RouterHostBulkConfigDialog*>(this),
                        tr("Invalid public key (must be 32 bytes hex)."));
        return false;
    }
    // A key with no server is rejected by the host. When the server field itself is not
    // changed, every host keeps its own and judges for itself.
    if (changePublicKey() && !publicKey().isEmpty() && changeServer() && server().isEmpty())
    {
        MsgBox::warning(const_cast<RouterHostBulkConfigDialog*>(this),
                        tr("A public key without a server is not allowed."));
        return false;
    }
    return true;
}
