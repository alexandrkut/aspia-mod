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

#include "client/desktop/qs_client_export_dialog.h"

#include <QDir>
#include <QEventLoop>
#include <QFileDialog>
#include <QFile>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStandardPaths>
#include <QTimer>
#include <QVBoxLayout>

#include "base/build_config.h"
#include "base/crypto/password_generator.h"
#include "base/logging.h"
#include "base/net/address.h"
#include "base/peer/router_user.h"
#include "client/router/router_controller.h"
#include "client/router/router_session.h"
#include "client/win/portable_package.h"
#include "common/desktop/msg_box.h"
#include "proto/router_admin.h"
#include "proto/router_constants.h"

namespace Ui {

class QsClientExportDialog
{
public:
    QLabel* description = nullptr;
    QLineEdit* edit_name = nullptr;
    QLineEdit* edit_address = nullptr;
    QPushButton* button_export = nullptr;
    QPushButton* button_cancel = nullptr;
};

} // namespace Ui

namespace {

constexpr int kRequestTimeoutMs = 15000;

} // namespace

//--------------------------------------------------------------------------------------------------
QsClientExportDialog::QsClientExportDialog(QWidget* parent)
    : QDialog(parent),
      ui(std::make_unique<Ui::QsClientExportDialog>())
{
    LOG(INFO) << "Ctor";

    setWindowTitle(tr("Export Quick Support Client"));
    setModal(true);

    ui->description = new QLabel(tr(
        "A new account is created on the router for this client. The password is generated "
        "automatically and stored only in the exported file. The suffix \"-qs\" is added to "
        "the name."), this);
    ui->description->setWordWrap(true);

    ui->edit_name = new QLineEdit(this);
    ui->edit_name->setMaxLength(static_cast<int>(User::kMaxUserNameLength));

    ui->edit_address = new QLineEdit(this);
    ui->edit_address->setMaxLength(64);

    RouterSession* session = RouterController::session();
    if (session)
        ui->edit_address->setText(session->config().address());

    auto* form = new QFormLayout();
    form->addRow(tr("Name:"), ui->edit_name);
    form->addRow(tr("Router address:"), ui->edit_address);

    ui->button_export = new QPushButton(tr("Export..."), this);
    ui->button_cancel = new QPushButton(tr("Cancel"), this);

    auto* buttons = new QHBoxLayout();
    buttons->addStretch(1);
    buttons->addWidget(ui->button_cancel);
    buttons->addWidget(ui->button_export);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(ui->description);
    layout->addLayout(form);
    layout->addStretch(1);
    layout->addLayout(buttons);

    connect(ui->button_export, &QPushButton::clicked, this, &QsClientExportDialog::onExport);
    connect(ui->button_cancel, &QPushButton::clicked, this, &QDialog::reject);
}

//--------------------------------------------------------------------------------------------------
QsClientExportDialog::~QsClientExportDialog()
{
    LOG(INFO) << "Dtor";
}

//--------------------------------------------------------------------------------------------------
void QsClientExportDialog::onExport()
{
    RouterSession* session = RouterController::session();
    if (!session || session->config().sessionType() != proto::router::SESSION_TYPE_ADMIN)
    {
        MsgBox::warning(this, tr("An administrator session with the router is required."));
        return;
    }

    const QString user_name = ui->edit_name->text().trimmed() + QLatin1String("-qs");
    if (!User::isValidUserName(user_name))
    {
        MsgBox::warning(this, tr("The user name is invalid."));
        ui->edit_name->setFocus();
        return;
    }

    const Address address = Address::fromString(
        ui->edit_address->text().trimmed(), kDefaultRouterClientTcpPort);
    if (!address.isValid())
    {
        MsgBox::warning(this, tr("The router address is invalid."));
        ui->edit_address->setFocus();
        return;
    }

    // Every export creates a fresh account. Rotating an existing one would cut off the clients
    // that were already handed out, so refuse a name that is taken before writing the file.
    bool name_checked = false;
    proto::router::UserList name_reply;
    QEventLoop name_loop;
    QTimer name_timeout;
    name_timeout.setSingleShot(true);
    connect(&name_timeout, &QTimer::timeout, &name_loop, &QEventLoop::quit);

    session->findUser(user_name, RouterCallback<proto::router::UserList>(
        this, [&](const proto::router::UserList& reply)
    {
        name_reply = reply;
        name_checked = true;
        name_loop.quit();
    }));

    name_timeout.start(kRequestTimeoutMs);
    name_loop.exec();

    if (!name_checked)
    {
        MsgBox::warning(this, tr("The router did not answer the user lookup."));
        return;
    }

    if (name_reply.user_size() > 0)
    {
        MsgBox::warning(this, tr("A user with this name already exists on the router."));
        ui->edit_name->setFocus();
        return;
    }

    const QString desktop_path = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    const QString default_path = desktop_path.isEmpty() ?
        QLatin1String("aspia-mod-qs-client.exe") :
        QDir(desktop_path).filePath(QLatin1String("aspia-mod-qs-client.exe"));

    QString file_path = QFileDialog::getSaveFileName(
        this, tr("Export Quick Support Client"), default_path, tr("Executable files (*.exe)"));
    if (file_path.isEmpty())
        return;

    if (!file_path.endsWith(QLatin1String(".exe"), Qt::CaseInsensitive))
        file_path.append(QLatin1String(".exe"));

    const SecureString password = PasswordGenerator().result();

    PortableClientPackage::Settings settings;
    settings.router_address = address;
    settings.username = user_name;
    settings.password = password;

    if (!PortableClientPackage::exportWithSettings(file_path, settings))
    {
        MsgBox::warning(this, tr("Failed to export the Quick Support client."));
        return;
    }

    RouterUser user = RouterUser::create(user_name, password);
    // An unmodified router sees an ordinary operator; only this router uses the account type to
    // restrict the session to the one-purpose Quick Support client.
    user.sessions = proto::router::SESSION_TYPE_OPERATOR;
    user.flags = User::ENABLED;
    user.user_type = proto::router::USER_TYPE_QUICK_SUPPORT;

    bool finished = false;
    proto::router::UserResult result;

    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);

    session->addUser(user.serialize(), RouterCallback<proto::router::UserResult>(
        this, [&](const proto::router::UserResult& reply)
    {
        result = reply;
        finished = true;
        loop.quit();
    }));

    timeout.start(kRequestTimeoutMs);
    loop.exec();

    if (!finished || result.error_code() != proto::router::kErrorOk)
    {
        QFile::remove(file_path);
        LOG(ERROR) << "Unable to create the QS account on the router:"
                   << (finished ? result.error_code() : "timeout");
        MsgBox::warning(this, tr("Failed to create the user on the router."));
        return;
    }

    MsgBox::information(this, tr("The Quick Support client has been exported."));
    accept();
}
