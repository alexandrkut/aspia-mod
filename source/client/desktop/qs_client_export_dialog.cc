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

#include <QButtonGroup>
#include <QComboBox>
#include <QDir>
#include <QEventLoop>
#include <QFileDialog>
#include <QFile>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QStandardPaths>
#include <QTimer>
#include <QVBoxLayout>

#include <functional>

#include "base/build_config.h"
#include "base/crypto/password_generator.h"
#include "base/crypto/secure_string.h"
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
    QRadioButton* radio_new = nullptr;
    QRadioButton* radio_existing = nullptr;
    QLineEdit* edit_name = nullptr;
    QComboBox* combo_users = nullptr;
    QLineEdit* edit_password = nullptr;
    QPushButton* button_show_password = nullptr;
    QPushButton* button_generate = nullptr;
    QLabel* label_warning = nullptr;
    QLabel* label_users_status = nullptr;
    QLineEdit* edit_address = nullptr;
    QPushButton* button_export = nullptr;
    QPushButton* button_cancel = nullptr;
};

} // namespace Ui

namespace {

constexpr int kRequestTimeoutMs = 15000;
constexpr int kMaxUsersToLoad = 500;

} // namespace

//--------------------------------------------------------------------------------------------------
QsClientExportDialog::QsClientExportDialog(QWidget* parent)
    : QDialog(parent),
      ui(std::make_unique<Ui::QsClientExportDialog>())
{
    LOG(INFO) << "Ctor";

    setWindowTitle(tr("Export Preconfigured QS Client"));
    setModal(true);

    ui->description = new QLabel(tr(
        "A new account is created on the router for this client. The password is generated "
        "automatically and stored only in the exported file. The suffix \"-qs\" is added to "
        "the name."), this);
    ui->description->setWordWrap(true);

    ui->radio_new = new QRadioButton(tr("Create new user"), this);
    ui->radio_existing = new QRadioButton(tr("Use existing user"), this);
    ui->radio_new->setChecked(true);

    auto* mode_layout = new QHBoxLayout();
    mode_layout->addWidget(ui->radio_new);
    mode_layout->addWidget(ui->radio_existing);

    ui->edit_name = new QLineEdit(this);
    ui->edit_name->setMaxLength(static_cast<int>(User::kMaxUserNameLength));

    ui->combo_users = new QComboBox(this);
    ui->combo_users->setVisible(false);

    auto* user_container = new QWidget(this);
    auto* user_layout = new QHBoxLayout(user_container);
    user_layout->setContentsMargins(0, 0, 0, 0);
    user_layout->addWidget(ui->edit_name);
    user_layout->addWidget(ui->combo_users);

    ui->edit_password = new QLineEdit(this);
    ui->edit_password->setEchoMode(QLineEdit::Password);

    ui->button_show_password = new QPushButton(tr("Show"), this);
    ui->button_generate = new QPushButton(tr("Generate password"), this);

    auto* password_container = new QWidget(this);
    auto* password_layout = new QHBoxLayout(password_container);
    password_layout->setContentsMargins(0, 0, 0, 0);
    password_layout->addWidget(ui->edit_password);
    password_layout->addWidget(ui->button_show_password);
    password_layout->addWidget(ui->button_generate);

    ui->label_warning = new QLabel(tr(
        "The password must match the password of this account on the router. "
        "The password on the router is not changed - if it differs, the QS client "
        "will not connect."), this);
    ui->label_warning->setWordWrap(true);
    ui->label_warning->setVisible(false);

    ui->label_users_status = new QLabel(this);
    ui->label_users_status->setWordWrap(true);

    ui->edit_address = new QLineEdit(this);
    ui->edit_address->setMaxLength(64);

    RouterSession* session = RouterController::session();
    if (session)
        ui->edit_address->setText(session->config().address());

    auto* form = new QFormLayout();
    form->addRow(tr("User:"), user_container);
    form->addRow(tr("Password:"), password_container);
    form->addRow(tr("Router address:"), ui->edit_address);

    ui->button_export = new QPushButton(tr("Export..."), this);
    ui->button_cancel = new QPushButton(tr("Cancel"), this);

    auto* buttons = new QHBoxLayout();
    buttons->addStretch(1);
    buttons->addWidget(ui->button_cancel);
    buttons->addWidget(ui->button_export);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(ui->description);
    layout->addLayout(mode_layout);
    layout->addLayout(form);
    layout->addWidget(ui->label_warning);
    layout->addWidget(ui->label_users_status);
    layout->addStretch(1);
    layout->addLayout(buttons);

    auto update_mode = [this]()
    {
        const bool new_mode = ui->radio_new->isChecked();
        ui->edit_name->setVisible(new_mode);
        ui->combo_users->setVisible(!new_mode);
        ui->button_generate->setVisible(new_mode);
        ui->label_warning->setVisible(!new_mode);

        if (new_mode)
        {
            ui->description->setText(tr(
                "A new account is created on the router for this client. The password is stored "
                "only in the exported file. The suffix \"-qs\" is added to the name."));
        }
        else
        {
            ui->description->setText(tr(
                "An existing account is used. The account is not created or changed; only the "
                "password is sealed into the file."));
        }
    };

    connect(ui->radio_new, &QRadioButton::toggled, this, [update_mode](bool)
    {
        update_mode();
    });
    update_mode();

    connect(ui->button_show_password, &QPushButton::clicked, this, [this]()
    {
        if (ui->edit_password->echoMode() == QLineEdit::Password)
        {
            ui->edit_password->setEchoMode(QLineEdit::Normal);
            ui->button_show_password->setText(tr("Hide"));
        }
        else
        {
            ui->edit_password->setEchoMode(QLineEdit::Password);
            ui->button_show_password->setText(tr("Show"));
        }
    });

    connect(ui->button_generate, &QPushButton::clicked, this, [this]()
    {
        PasswordGenerator generator;
        generator.setLength(8);
        generator.setCharacters(PasswordGenerator::UPPER_CASE |
                                PasswordGenerator::LOWER_CASE |
                                PasswordGenerator::DIGITS |
                                PasswordGenerator::SPECIAL);
        ui->edit_password->setText(generator.result().toString());
    });

    connect(ui->button_export, &QPushButton::clicked, this, &QsClientExportDialog::onExport);
    connect(ui->button_cancel, &QPushButton::clicked, this, &QDialog::reject);

    // Load existing Quick Support users from the router (best effort, no crash on failure).
    if (session)
    {
        ui->label_users_status->setText(tr("Loading users..."));

        auto fetch = std::make_shared<std::function<void(qint64)>>();
        *fetch = [this, session, fetch](qint64 offset)
        {
            session->listUsers(offset, proto::router::kMaxUserPageSize,
                RouterCallback<proto::router::UserList>(this,
                    [this, offset, fetch](const proto::router::UserList& reply)
            {
                for (int i = 0; i < reply.user_size(); ++i)
                {
                    RouterUser user = RouterUser::parseFrom(reply.user(i));
                    if (User::isQuickSupportName(user.name) &&
                        user.user_type == proto::router::USER_TYPE_QUICK_SUPPORT &&
                        (user.flags & User::ENABLED))
                    {
                        ui->combo_users->addItem(user.name);
                    }
                }

                const bool more = reply.user_size() == proto::router::kMaxUserPageSize &&
                                  (offset + reply.user_size()) < kMaxUsersToLoad;
                if (more)
                {
                    (*fetch)(offset + reply.user_size());
                }
                else
                {
                    if (ui->combo_users->count() == 0)
                        ui->label_users_status->setText(
                            tr("No Quick Support users found on the router."));
                    else
                        ui->label_users_status->setText(QString());
                }
            }));
        };

        (*fetch)(0);
    }
    else
    {
        ui->label_users_status->setText(tr("No router session."));
    }
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

    const bool new_mode = ui->radio_new->isChecked();

    QString user_name;
    if (new_mode)
    {
        user_name = ui->edit_name->text().trimmed() + QLatin1String("-qs");
        if (!User::isValidUserName(user_name))
        {
            MsgBox::warning(this, tr("The user name is invalid."));
            ui->edit_name->setFocus();
            return;
        }
    }
    else
    {
        if (ui->combo_users->count() == 0)
        {
            MsgBox::warning(this, tr("No Quick Support users found on the router."));
            return;
        }
        user_name = ui->combo_users->currentText();
    }

    const Address address = Address::fromString(
        ui->edit_address->text().trimmed(), kDefaultRouterClientTcpPort);
    if (!address.isValid())
    {
        MsgBox::warning(this, tr("The router address is invalid."));
        ui->edit_address->setFocus();
        return;
    }

    const SecureString password(ui->edit_password->text());
    if (!User::isValidPassword(password))
    {
        MsgBox::warning(this, tr("The password must be 8 to 64 characters long."));
        ui->edit_password->setFocus();
        return;
    }

    if (new_mode)
    {
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
    }
    else
    {
        // Make sure the selected account still exists; do not modify it.
        bool found_checked = false;
        proto::router::UserList found_reply;
        QEventLoop found_loop;
        QTimer found_timeout;
        found_timeout.setSingleShot(true);
        connect(&found_timeout, &QTimer::timeout, &found_loop, &QEventLoop::quit);

        session->findUser(user_name, RouterCallback<proto::router::UserList>(
            this, [&](const proto::router::UserList& reply)
        {
            found_reply = reply;
            found_checked = true;
            found_loop.quit();
        }));

        found_timeout.start(kRequestTimeoutMs);
        found_loop.exec();

        if (!found_checked)
        {
            MsgBox::warning(this, tr("The router did not answer the user lookup."));
            return;
        }

        if (found_reply.user_size() == 0)
        {
            MsgBox::warning(this, tr("The selected user no longer exists on the router."));
            return;
        }
    }

    const QString desktop_path = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    const QString default_path = desktop_path.isEmpty() ?
        QLatin1String("aspia-client-qs-mod.exe") :
        QDir(desktop_path).filePath(QLatin1String("aspia-client-qs-mod.exe"));

    QString file_path = QFileDialog::getSaveFileName(
        this, tr("Export Preconfigured QS Client"), default_path, tr("Executable files (*.exe)"));
    if (file_path.isEmpty())
        return;

    if (!file_path.endsWith(QLatin1String(".exe"), Qt::CaseInsensitive))
        file_path.append(QLatin1String(".exe"));

    PortableClientPackage::Settings settings;
    settings.router_address = address;
    settings.username = user_name;
    settings.password = password;

    if (!PortableClientPackage::exportWithSettings(file_path, settings))
    {
        MsgBox::warning(this, tr("Failed to export the Quick Support client."));
        return;
    }

    if (new_mode)
    {
        RouterUser user = RouterUser::create(user_name, password);
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
    }

    MsgBox::information(this, tr("The Quick Support client has been exported."));
    accept();
}
