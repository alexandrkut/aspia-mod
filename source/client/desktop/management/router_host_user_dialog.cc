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

#include "client/desktop/management/router_host_user_dialog.h"

#include <QAbstractButton>
#include <QCheckBox>
#include <QEvent>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "base/auto_qpointer.h"
#include "base/logging.h"
#include "base/peer/user.h"
#include "common/desktop/dialog_button_box.h"
#include "common/desktop/msg_box.h"
#include "common/desktop/password_edit.h"
#include "common/desktop/session_type.h"
#include "proto/peer.h"

namespace {

// A leading '#' and an all-digit name are reserved for the names the program builds itself.
bool isNameAllowed(const QString& username)
{
    if (!User::isValidUserName(username))
        return false;

    if (username.startsWith('#'))
        return false;

    for (QChar character : username)
    {
        if (!character.isDigit())
            return true;
    }

    return false;
}

} // namespace

//--------------------------------------------------------------------------------------------------
RouterHostUserDialog::RouterHostUserDialog(qint64 entry_id,
                                           const QString& name,
                                           quint32 sessions,
                                           quint32 flags,
                                           const QStringList& existing_names,
                                           QWidget* parent)
    : QDialog(parent),
      entry_id_(entry_id),
      original_name_(name),
      existing_names_(existing_names)
{
    setWindowTitle(tr("User Properties"));
    setMinimumWidth(420);

    auto* layout = new QFormLayout(this);

    edit_username_ = new QLineEdit(this);
    edit_username_->setText(name);
    layout->addRow(tr("User Name:"), edit_username_);

    edit_password_ = new PasswordEdit(this);
    layout->addRow(tr("Password:"), edit_password_);

    edit_password_repeat_ = new PasswordEdit(this);
    layout->addRow(tr("Password (repeat):"), edit_password_repeat_);

    checkbox_disable_ = new QCheckBox(tr("Disable User Account"), this);
    checkbox_disable_->setChecked(!(flags & User::ENABLED));
    layout->addRow(QString(), checkbox_disable_);

    auto* sessions_layout = new QVBoxLayout();

    auto* label = new QLabel(tr("Allowed Session Types:"), this);
    sessions_layout->addWidget(label);

    auto* buttons_layout = new QHBoxLayout();
    auto* button_check_all = new QPushButton(tr("Check all"), this);
    auto* button_check_none = new QPushButton(tr("Uncheck all"), this);
    buttons_layout->addWidget(button_check_all);
    buttons_layout->addWidget(button_check_none);
    buttons_layout->addStretch();
    sessions_layout->addLayout(buttons_layout);

    tree_sessions_ = new QTreeWidget(this);
    tree_sessions_->setColumnCount(1);
    tree_sessions_->header()->hide();
    tree_sessions_->setIconSize(QSize(24, 24));
    tree_sessions_->setIndentation(0);
    sessions_layout->addWidget(tree_sessions_);

    layout->addRow(sessions_layout);

    auto add_session = [&](proto::peer::SessionType session_type)
    {
        QTreeWidgetItem* item = new QTreeWidgetItem();

        item->setText(0, sessionName(session_type));
        item->setIcon(0, sessionIcon(session_type));
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setData(0, Qt::UserRole, QVariant(static_cast<int>(session_type)));

        if (sessions & static_cast<quint32>(session_type))
            item->setCheckState(0, Qt::Checked);
        else
            item->setCheckState(0, Qt::Unchecked);

        tree_sessions_->addTopLevelItem(item);
    };

    add_session(proto::peer::SESSION_TYPE_DESKTOP);
    add_session(proto::peer::SESSION_TYPE_TERMINAL);
    add_session(proto::peer::SESSION_TYPE_FILE_TRANSFER);
    add_session(proto::peer::SESSION_TYPE_SYSTEM_INFO);
    add_session(proto::peer::SESSION_TYPE_CHAT);

    auto* button_box =
        new DialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addRow(button_box);

    connect(button_check_all, &QPushButton::clicked,
            this, &RouterHostUserDialog::onCheckAllButtonPressed);
    connect(button_check_none, &QPushButton::clicked,
            this, &RouterHostUserDialog::onCheckNoneButtonPressed);
    connect(button_box, &QDialogButtonBox::clicked,
            this, &RouterHostUserDialog::onButtonBoxClicked);

    connect(edit_username_, &QLineEdit::textEdited, this, [this]()
    {
        if (!account_changed_)
            setAccountChanged(true);
    });

    // A new user always sets the password; an existing one keeps it until a double-click.
    setAccountChanged(entry_id_ == 0);
}

//--------------------------------------------------------------------------------------------------
RouterHostUserDialog::~RouterHostUserDialog()
{
    // Nothing
}

//--------------------------------------------------------------------------------------------------
QString RouterHostUserDialog::userName() const
{
    return edit_username_->text();
}

//--------------------------------------------------------------------------------------------------
bool RouterHostUserDialog::passwordChanged() const
{
    return account_changed_;
}

//--------------------------------------------------------------------------------------------------
SecureString RouterHostUserDialog::password() const
{
    return edit_password_->password();
}

//--------------------------------------------------------------------------------------------------
quint32 RouterHostUserDialog::sessions() const
{
    quint32 sessions = 0;
    for (int i = 0; i < tree_sessions_->topLevelItemCount(); ++i)
    {
        QTreeWidgetItem* item = tree_sessions_->topLevelItem(i);
        if (item->checkState(0) == Qt::Checked)
            sessions |= static_cast<quint32>(item->data(0, Qt::UserRole).toUInt());
    }
    return sessions;
}

//--------------------------------------------------------------------------------------------------
quint32 RouterHostUserDialog::flags() const
{
    quint32 flags = 0;
    if (!checkbox_disable_->isChecked())
        flags |= User::ENABLED;
    return flags;
}

//--------------------------------------------------------------------------------------------------
bool RouterHostUserDialog::eventFilter(QObject* object, QEvent* event)
{
    if (event->type() == QEvent::MouseButtonDblClick &&
        (object == edit_password_ || object == edit_password_repeat_))
    {
        setAccountChanged(true);

        if (object == edit_password_)
            edit_password_->setFocus();
        else if (object == edit_password_repeat_)
            edit_password_repeat_->setFocus();
    }

    return false;
}

//--------------------------------------------------------------------------------------------------
void RouterHostUserDialog::onCheckAllButtonPressed()
{
    for (int i = 0; i < tree_sessions_->topLevelItemCount(); ++i)
        tree_sessions_->topLevelItem(i)->setCheckState(0, Qt::Checked);
}

//--------------------------------------------------------------------------------------------------
void RouterHostUserDialog::onCheckNoneButtonPressed()
{
    for (int i = 0; i < tree_sessions_->topLevelItemCount(); ++i)
        tree_sessions_->topLevelItem(i)->setCheckState(0, Qt::Unchecked);
}

//--------------------------------------------------------------------------------------------------
void RouterHostUserDialog::onButtonBoxClicked(QAbstractButton* button)
{
    auto* button_box = qobject_cast<DialogButtonBox*>(sender());
    if (!button_box || button_box->standardButton(button) != QDialogButtonBox::Ok)
    {
        reject();
        return;
    }

    if (account_changed_)
    {
        QString username = edit_username_->text();
        SecureString password = edit_password_->password();
        SecureString password_repeat = edit_password_repeat_->password();

        if (!isNameAllowed(username))
        {
            MsgBox::warning(this,
                tr("The user name can not be empty and can contain only alphabet"
                   " characters, numbers and ""_"", ""-"", ""."", ""@"" characters. It can"
                   " not consist of digits only."));
            edit_username_->selectAll();
            edit_username_->setFocus();
            return;
        }

        QStringList exist_names;
        for (const QString& existing : existing_names_)
        {
            if (original_name_.compare(existing, Qt::CaseInsensitive) != 0)
                exist_names.append(existing);
        }

        if (exist_names.contains(username, Qt::CaseInsensitive))
        {
            MsgBox::warning(this, tr("The username you entered already exists."));
            edit_username_->selectAll();
            edit_username_->setFocus();
            return;
        }

        if (password != password_repeat)
        {
            MsgBox::warning(this, tr("The passwords you entered do not match."));
            edit_password_->selectAll();
            edit_password_->setFocus();
            return;
        }

        if (!User::isValidPassword(password))
        {
            QString message = password.size() < User::kMinPasswordLength
                ? tr("The password can not be shorter than %n characters.",
                     "", User::kMinPasswordLength)
                : tr("The password can not be longer than %n characters.",
                     "", User::kMaxPasswordLength);

            MsgBox::warning(this, message);
            edit_password_->selectAll();
            edit_password_->setFocus();
            return;
        }

        if (!User::isSafePassword(password))
        {
            QString unsafe =
                tr("Password you entered does not meet the security requirements!");

            QString safe =
                tr("The password must contain lowercase and uppercase characters, "
                   "numbers and should not be shorter than %n characters.",
                   "", User::kSafePasswordLength);

            QString question = tr("Do you want to enter a different password?");

            AutoQPointer<MsgBox> message_box(new MsgBox(MsgBox::Warning,
                tr("Warning"),
                QString("<b>%1</b><br/>%2<br/>%3").arg(unsafe, safe, question),
                MsgBox::Yes | MsgBox::No,
                this));

            if (message_box->exec() == MsgBox::Yes)
            {
                edit_password_->clear();
                edit_password_repeat_->clear();
                edit_password_->setFocus();
                return;
            }
        }
    }

    accept();
}

//--------------------------------------------------------------------------------------------------
void RouterHostUserDialog::setAccountChanged(bool changed)
{
    account_changed_ = changed;

    edit_password_->setEnabled(changed);
    edit_password_repeat_->setEnabled(changed);

    edit_password_->clear();
    edit_password_repeat_->clear();

    edit_password_->setShowPassword(!changed);
    edit_password_repeat_->setShowPassword(!changed);

    if (changed)
    {
        edit_password_->setPlaceholderText(QString());
        edit_password_repeat_->setPlaceholderText(QString());
    }
    else
    {
        QString prompt = tr("Double-click to change");
        edit_password_->setPlaceholderText(prompt);
        edit_password_repeat_->setPlaceholderText(prompt);

        edit_password_->installEventFilter(this);
        edit_password_repeat_->installEventFilter(this);
    }
}
