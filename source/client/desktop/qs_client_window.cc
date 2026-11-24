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

#include "client/desktop/qs_client_window.h"

#include <QComboBox>
#include <QEvent>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include "base/logging.h"
#include "base/peer/host_id.h"
#include "client/config.h"
#include "client/desktop/client_window.h"
#include "client/desktop/desktop/desktop_window.h"
#include "client/desktop/file_transfer/file_transfer_window.h"
#include "client/router/router_controller.h"
#include "client/router/router_types.h"
#include "common/desktop/msg_box.h"
#include "common/desktop/two_factor_code_dialog.h"

namespace {

constexpr int kDesktopSession = 0;
constexpr int kFileTransferSession = 1;

} // namespace

//--------------------------------------------------------------------------------------------------
QsClientWindow::QsClientWindow(QWidget* parent)
    : QWidget(parent)
{
    LOG(INFO) << "Ctor";

    setWindowTitle(QStringLiteral("Клиент QS Aspia-mod"));
    setWindowIcon(QIcon(QStringLiteral(":/img/aspia-qs.ico")));
    setWindowFlags(Qt::Dialog | Qt::WindowCloseButtonHint);

    edit_id_ = new QLineEdit(this);
    edit_id_->setPlaceholderText(tr("123 456 789"));
    edit_id_->setMaxLength(32);

    combo_session_ = new QComboBox(this);
    combo_session_->addItem(tr("Desktop"), kDesktopSession);
    combo_session_->addItem(tr("File transfer"), kFileTransferSession);

    button_connect_ = new QPushButton(tr("Connect"), this);
    button_connect_->setDefault(true);
    button_connect_->setEnabled(false);

    label_status_ = new QLabel(tr("Connecting to the router..."), this);
    label_status_->setWordWrap(true);

    auto* form = new QFormLayout();
    form->addRow(tr("Host ID:"), edit_id_);
    form->addRow(tr("Session:"), combo_session_);

    auto* buttons = new QHBoxLayout();
    buttons->addStretch(1);
    buttons->addWidget(button_connect_);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(label_status_);
    layout->addLayout(form);
    layout->addLayout(buttons);

    setMinimumWidth(320);

    connect(button_connect_, &QPushButton::clicked, this, &QsClientWindow::onConnect);
    connect(edit_id_, &QLineEdit::returnPressed, this, &QsClientWindow::onConnect);
    connect(&RouterController::instance(), &RouterController::sig_statusChanged,
            this, &QsClientWindow::onRouterStatusChanged);
    connect(&RouterController::instance(), &RouterController::sig_twoFactorRequired,
            this, &QsClientWindow::onTwoFactorRequired);

    onRouterStatusChanged(RouterController::status());
}

//--------------------------------------------------------------------------------------------------
QsClientWindow::~QsClientWindow()
{
    LOG(INFO) << "Dtor";
}

//--------------------------------------------------------------------------------------------------
void QsClientWindow::onRouterStatusChanged(RouterStatus status)
{
    switch (status)
    {
        case RouterStatus::ONLINE:
            label_status_->setText(tr("Enter the ID shown by the support host."));
            button_connect_->setEnabled(true);
            break;

        case RouterStatus::UNREADABLE:
            label_status_->setText(tr("The router data is damaged."));
            button_connect_->setEnabled(false);
            break;

        case RouterStatus::CONNECTING:
            label_status_->setText(tr("Connecting to the router..."));
            button_connect_->setEnabled(false);
            break;

        case RouterStatus::TWO_FACTOR:
            label_status_->setText(tr("Waiting for the two-factor code."));
            button_connect_->setEnabled(false);
            break;

        case RouterStatus::OFFLINE:
        default:
            label_status_->setText(tr("The router is unavailable. Retrying..."));
            button_connect_->setEnabled(false);
            break;
    }
}

//--------------------------------------------------------------------------------------------------
void QsClientWindow::onTwoFactorRequired()
{
    // A Quick Support account has 2FA off on this router, but the same client also works against an
    // unmodified router where the operator role may still ask for a code.
    TwoFactorPrompt* prompt = RouterController::twoFactorPrompt();
    if (!prompt || prompt->blockedSeconds() > 0)
        return;

    QPointer<TwoFactorPrompt> guarded(prompt);
    TwoFactorCodeDialog dialog(prompt->codeRefused(), this);
    if (dialog.exec() == QDialog::Accepted && guarded)
        guarded->submitCode(dialog.code());
}

//--------------------------------------------------------------------------------------------------
void QsClientWindow::onConnect()
{
    const QString text = edit_id_->text();
    QString digits;
    for (const QChar& character : text)
    {
        if (character.isDigit())
            digits.append(character);
    }

    bool ok = false;
    const HostId host_id = digits.toULongLong(&ok);
    if (!ok || !isTempHostId(host_id))
    {
        MsgBox::warning(this, tr("Enter a valid Quick Support ID."));
        edit_id_->setFocus();
        return;
    }

    if (RouterController::status() != RouterStatus::ONLINE)
    {
        MsgBox::warning(this, tr("The router is not connected yet."));
        return;
    }

    const int session_kind = combo_session_->currentData().toInt();
    const proto::peer::SessionType session_type =
        session_kind == kFileTransferSession ?
            proto::peer::SESSION_TYPE_FILE_TRANSFER : proto::peer::SESSION_TYPE_DESKTOP;

    HostConfig host = HostConfig::forRouterHost(host_id, QString());
    host.setPortable(true);

    ClientWindow* client_window = nullptr;
    if (session_type == proto::peer::SESSION_TYPE_FILE_TRANSFER)
    {
        client_window = new FileTransferWindow();
    }
    else
    {
        client_window = new DesktopWindow(defaultDesktopConfig());
    }

    client_window->setWindowTitle(tr("Quick Support %1").arg(host.address()));
    client_window->setWindowIcon(windowIcon());

    if (!client_window->connectToHost(host, ProfileConfig(), QString()))
    {
        client_window->deleteLater();
        return;
    }

    client_window_ = client_window;
    client_window->installEventFilter(this);
    client_window->show();
    hide();
}

//--------------------------------------------------------------------------------------------------
bool QsClientWindow::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == client_window_ && event->type() == QEvent::Close)
    {
        // The session window is gone, so the only window left would be hidden and the program would
        // stay in the tray with no way to start another connection.
        client_window_->deleteLater();
        client_window_ = nullptr;

        show();
        raise();
        activateWindow();
    }

    return QWidget::eventFilter(obj, event);
}
