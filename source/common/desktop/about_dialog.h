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

#ifndef COMMON_DESKTOP_ABOUT_DIALOG_H
#define COMMON_DESKTOP_ABOUT_DIALOG_H

#include <QDialog>
#include <QStringList>

#include <memory>

namespace Ui {
class AboutDialog;
} // namespace Ui

class AboutDialog final : public QDialog
{
    Q_OBJECT

public:
    // |modifications| are the changes of this fork that concern the program the dialog
    // belongs to: the client knows nothing of the router, and the router knows nothing of
    // the client. The list is empty for a program that was not changed, and the dialog says
    // nothing then.
    explicit AboutDialog(const QString& application_name, const QStringList& modifications,
                         QWidget* parent = nullptr);
    ~AboutDialog() final;

private:
    std::unique_ptr<Ui::AboutDialog> ui;

    Q_DISABLE_COPY_MOVE(AboutDialog)
};

#endif // COMMON_DESKTOP_ABOUT_DIALOG_H
