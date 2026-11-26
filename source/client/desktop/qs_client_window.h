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

#ifndef CLIENT_DESKTOP_QS_CLIENT_WINDOW_H
#define CLIENT_DESKTOP_QS_CLIENT_WINDOW_H

#include <QPointer>
#include <QWidget>

class QComboBox;
class QEvent;
class QLabel;
class QLineEdit;
class QPushButton;
class ClientWindow;

enum class RouterStatus : int;

// The only window of a preconfigured Quick Support client. It asks for the temporary ID the support
// host shows and for the kind of session to open; there is no address book, no user list and no
// application settings to get lost in.
class QsClientWindow final : public QWidget
{
    Q_OBJECT

public:
    explicit QsClientWindow(QWidget* parent = nullptr);
    ~QsClientWindow() final;

    void setUserName(const QString& value) { user_name_ = value; }

private slots:
    void onConnect();
    void onRouterStatusChanged(RouterStatus status);
    void onTwoFactorRequired();

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    QLineEdit* edit_id_ = nullptr;
    QComboBox* combo_session_ = nullptr;
    QPushButton* button_connect_ = nullptr;
    QLabel* label_status_ = nullptr;
    QPointer<ClientWindow> client_window_;

    QString user_name_;

    Q_DISABLE_COPY_MOVE(QsClientWindow)
};

#endif // CLIENT_DESKTOP_QS_CLIENT_WINDOW_H
