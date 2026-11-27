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

#include "common/desktop/no_wheel_combo_filter.h"

#include <QAbstractScrollArea>
#include <QComboBox>
#include <QCoreApplication>
#include <QEvent>
#include <QString>
#include <QWidget>

namespace {

class NoWheelComboFilter final : public QObject
{
public:
    explicit NoWheelComboFilter(QCoreApplication* app)
        : QObject(app)
    {
        setObjectName(QStringLiteral("no_wheel_combo_filter"));
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() == QEvent::Wheel)
        {
            QComboBox* combo = qobject_cast<QComboBox*>(watched);
            if (combo)
            {
                // The combo must not change its current item from a stray wheel, but the page
                // around it has to scroll. Forward the same event to the viewport of the nearest
                // enclosing scroll area; its target is the viewport, not the combo, so the filter
                // does not see it as a combo event again.
                for (QWidget* widget = combo->parentWidget(); widget;
                     widget = widget->parentWidget())
                {
                    QAbstractScrollArea* scroll_area = qobject_cast<QAbstractScrollArea*>(widget);
                    if (!scroll_area || !scroll_area->viewport())
                        continue;

                    QCoreApplication::sendEvent(scroll_area->viewport(), event);
                    break;
                }

                return true;
            }
        }

        return QObject::eventFilter(watched, event);
    }
};

} // namespace

void installNoWheelComboFilter(QCoreApplication* app)
{
    if (!app || app->findChild<QObject*>(QStringLiteral("no_wheel_combo_filter")))
        return;

    // Parenting to the application makes the filter live exactly as long as it does, and installing
    // it on the application object lets it see the events of every QObject. No per-widget or .ui
    // changes are needed.
    app->installEventFilter(new NoWheelComboFilter(app));
}
