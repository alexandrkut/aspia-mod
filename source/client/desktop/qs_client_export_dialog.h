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

#ifndef CLIENT_DESKTOP_QS_CLIENT_EXPORT_DIALOG_H
#define CLIENT_DESKTOP_QS_CLIENT_EXPORT_DIALOG_H

#include <QDialog>

#include <memory>

namespace Ui {
class QsClientExportDialog;
} // namespace Ui

// Exports a self-contained copy of aspia_client.exe with the router address and credentials baked
// into it. In the "create new user" mode a fresh Quick Support account is created on the router
// (reusing a name would rotate the password and break the clients exported earlier); in the "use
// existing user" mode the selected account is left untouched and only the password is sealed into
// the file.
class QsClientExportDialog final : public QDialog
{
    Q_OBJECT

public:
    explicit QsClientExportDialog(QWidget* parent = nullptr);
    ~QsClientExportDialog() final;

private slots:
    void onExport();

private:
    std::unique_ptr<Ui::QsClientExportDialog> ui;

    Q_DISABLE_COPY_MOVE(QsClientExportDialog)
};

#endif // CLIENT_DESKTOP_QS_CLIENT_EXPORT_DIALOG_H
