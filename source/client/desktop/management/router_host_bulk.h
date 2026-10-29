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

#ifndef CLIENT_DESKTOP_MANAGEMENT_ROUTER_HOST_BULK_H
#define CLIENT_DESKTOP_MANAGEMENT_ROUTER_HOST_BULK_H

#include <QDialog>
#include <QJsonObject>
#include <QList>

#include <functional>

#include "client/router_types.h"

class QAbstractButton;
class QDialogButtonBox;
class QLineEdit;
class QCheckBox;
class QLabel;
class QTableWidget;

// One bulk target: a host the command is sent to.
struct RouterHostBulkTarget
{
    HostId host_id = kInvalidHostId;
    QString display;
};

// Outcome of a single host inside a bulk run. Status is one of:
// "ok", "skipped", "failed", "offline", "cancelled".
struct RouterHostBulkResult
{
    HostId host_id = kInvalidHostId;
    QString display;
    QString status;
    QString detail;
    QString raw;
};

// Sequential runner over RouterSession::setHostConfig: one host at a time, strictly in order.
// In-flight requests finish on cancel; the rest are marked "cancelled".
class RouterHostBulkRunner final : public QObject
{
    Q_OBJECT

public:
    using ParamsFn = std::function<QJsonObject(HostId)>;

    RouterHostBulkRunner(qint64 router_id,
                         const QList<RouterHostBulkTarget>& targets,
                         const QString& command,
                         ParamsFn params_fn,
                         QObject* parent = nullptr);

    void start();
    void cancel();
    bool isRunning() const { return running_; }
    int targetCount() const { return targets_.size(); }
    const QList<RouterHostBulkResult>& results() const { return results_; }

signals:
    void resultReady(int index);
    void finished();

private:
    void runNext();

    qint64 router_id_ = 0;
    QList<RouterHostBulkTarget> targets_;
    QString command_;
    ParamsFn params_fn_;
    QList<RouterHostBulkResult> results_;
    bool running_ = false;
    bool cancelled_ = false;

    Q_DISABLE_COPY_MOVE(RouterHostBulkRunner)
};

// One row of the bulk preview table. Rows with will_run=false are informational
// (skipped/offline) and never reach the runner.
struct RouterHostBulkPreviewRow
{
    QString host;
    QString action;
    QString detail;
    bool will_run = true;
    int target_index = -1;
};

// Single dialog for the whole bulk flow: preview table with Apply/Cancel,
// then live run in the same dialog, then Close.
class RouterHostBulkDialog final : public QDialog
{
    Q_OBJECT

public:
    RouterHostBulkDialog(const QString& title, QWidget* parent = nullptr);

    // Shows the preview; on Apply runs |runner| to completion in the same dialog.
    // Returns Accepted if the run completed, Rejected if cancelled before Apply.
    int execBulk(RouterHostBulkRunner* runner, const QList<RouterHostBulkPreviewRow>& preview);

private slots:
    void onButtonClicked(QAbstractButton* button);
    void onResultReady(int index);
    void onFinished();

private:
    void setRowResult(int row, const QString& status, const QString& detail);

    QTableWidget* table_ = nullptr;
    QDialogButtonBox* buttons_ = nullptr;
    QPushButton* apply_button_ = nullptr;
    QPushButton* cancel_button_ = nullptr;
    QLabel* status_label_ = nullptr;

    RouterHostBulkRunner* runner_ = nullptr;
    QVector<int> row_of_target_;
    bool running_ = false;
    bool completed_ = false;

    Q_DISABLE_COPY_MOVE(RouterHostBulkDialog)
};

// Tiny input for bulk full config: file summary plus the keep-router checkbox.
class RouterHostBulkFullConfigDialog final : public QDialog
{
    Q_OBJECT

public:
    explicit RouterHostBulkFullConfigDialog(const QString& file_summary, QWidget* parent = nullptr);

    bool keepRouter() const;

private slots:
    void onButtonClicked(QAbstractButton* button);

private:
    QCheckBox* check_keep_ = nullptr;

    Q_DISABLE_COPY_MOVE(RouterHostBulkFullConfigDialog)
};

// Tiny input for bulk user removal: name plus the force flag.
class RouterHostBulkRemoveDialog final : public QDialog
{
    Q_OBJECT

public:
    explicit RouterHostBulkRemoveDialog(QWidget* parent = nullptr);

    QString userName() const;
    bool force() const;

private slots:
    void onButtonClicked(QAbstractButton* button);

private:
    QLineEdit* edit_name_ = nullptr;
    QCheckBox* check_force_ = nullptr;

    Q_DISABLE_COPY_MOVE(RouterHostBulkRemoveDialog)
};

#endif // CLIENT_DESKTOP_MANAGEMENT_ROUTER_HOST_BULK_H
