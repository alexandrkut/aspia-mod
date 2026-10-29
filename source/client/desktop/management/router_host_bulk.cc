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

#include "client/desktop/management/router_host_bulk.h"

#include <QAbstractButton>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QProgressDialog>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include "base/logging.h"
#include "client/router_controller.h"
#include "client/router_session.h"
#include "common/desktop/msg_box.h"
#include "proto/router_constants.h"

namespace {

// Human-readable status for the results table.
QString statusText(const QString& status)
{
    if (status == "ok")
        return RouterHostBulkDialog::tr("Done");
    if (status == "skipped")
        return RouterHostBulkDialog::tr("Skipped");
    if (status == "failed")
        return RouterHostBulkDialog::tr("Failed");
    if (status == "offline")
        return RouterHostBulkDialog::tr("Offline");
    if (status == "cancelled")
        return RouterHostBulkDialog::tr("Cancelled");
    if (status == "running")
        return RouterHostBulkDialog::tr("Running...");
    return status;
}

} // namespace

//--------------------------------------------------------------------------------------------------
RouterHostBulkRunner::RouterHostBulkRunner(qint64 router_id,
                                           const QList<RouterHostBulkTarget>& targets,
                                           const QString& command,
                                           ParamsFn params_fn,
                                           QObject* parent)
    : QObject(parent),
      router_id_(router_id),
      targets_(targets),
      command_(command),
      params_fn_(std::move(params_fn))
{
    // Nothing
}

//--------------------------------------------------------------------------------------------------
void RouterHostBulkRunner::start()
{
    if (running_ || targets_.isEmpty())
    {
        emit finished();
        return;
    }

    running_ = true;
    cancelled_ = false;
    results_.clear();
    results_.reserve(targets_.size());
    runNext();
}

//--------------------------------------------------------------------------------------------------
void RouterHostBulkRunner::cancel()
{
    cancelled_ = true;
}

//--------------------------------------------------------------------------------------------------
void RouterHostBulkRunner::runNext()
{
    if (cancelled_)
    {
        while (results_.size() < targets_.size())
        {
            RouterHostBulkResult result;
            result.host_id = targets_[results_.size()].host_id;
            result.display = targets_[results_.size()].display;
            result.status = "cancelled";
            results_.append(result);
            emit resultReady(results_.size() - 1);
        }
        running_ = false;
        emit finished();
        return;
    }

    const int next = results_.size();
    if (next >= targets_.size())
    {
        running_ = false;
        emit finished();
        return;
    }

    RouterSession* session = RouterController::session(router_id_);
    if (!session)
    {
        while (results_.size() < targets_.size())
        {
            RouterHostBulkResult result;
            result.host_id = targets_[results_.size()].host_id;
            result.display = targets_[results_.size()].display;
            result.status = "failed";
            result.detail = "no session";
            results_.append(result);
            emit resultReady(results_.size() - 1);
        }
        running_ = false;
        emit finished();
        return;
    }

    const RouterHostBulkTarget target = targets_[next];
    const QJsonObject params = params_fn_(target.host_id);
    const QString json = QString::fromUtf8(QJsonDocument(params).toJson(QJsonDocument::Compact));

    session->setHostConfig(target.host_id, command_.toStdString(), json.toStdString(),
                           {this, [this, next](const proto::router::HostResult& result)
                           {
                               RouterHostBulkResult bulk_result;
                               bulk_result.host_id = targets_[next].host_id;
                               bulk_result.display = targets_[next].display;
                               if (result.error_code() == proto::router::kErrorOk)
                               {
                                   bulk_result.status = "ok";
                               }
                               else if (result.error_code() == proto::router::kErrorHostOffline)
                               {
                                   bulk_result.status = "offline";
                                   bulk_result.detail =
                                       QString::fromStdString(result.error_code());
                               }
                               else
                               {
                                   bulk_result.status = "failed";
                                   bulk_result.detail =
                                       QString::fromStdString(result.error_code());
                               }
                               bulk_result.raw = QString::fromStdString(result.result());
                               results_.append(bulk_result);
                               emit resultReady(next);
                               runNext();
                           }});
}

//--------------------------------------------------------------------------------------------------
RouterHostBulkDialog::RouterHostBulkDialog(const QString& title, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(title);
    setMinimumSize(620, 380);

    auto* layout = new QVBoxLayout(this);

    status_label_ = new QLabel(this);
    layout->addWidget(status_label_);

    table_ = new QTableWidget(this);
    table_->setColumnCount(4);
    table_->setHorizontalHeaderLabels(
        {tr("Host"), tr("Planned action"), tr("Result"), tr("Detail")});
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionMode(QAbstractItemView::NoSelection);
    layout->addWidget(table_);

    buttons_ = new QDialogButtonBox(this);
    apply_button_ = buttons_->addButton(tr("Apply"), QDialogButtonBox::ActionRole);
    cancel_button_ = buttons_->addButton(QDialogButtonBox::Cancel);
    layout->addWidget(buttons_);

    connect(buttons_, &QDialogButtonBox::clicked,
            this, &RouterHostBulkDialog::onButtonClicked);
}

//--------------------------------------------------------------------------------------------------
int RouterHostBulkDialog::execBulk(RouterHostBulkRunner* runner,
                                   const QList<RouterHostBulkPreviewRow>& preview)
{
    runner_ = runner;
    row_of_target_.clear();

    table_->setRowCount(preview.size());
    for (int i = 0; i < preview.size(); ++i)
    {
        table_->setItem(i, 0, new QTableWidgetItem(preview[i].host));
        table_->setItem(i, 1, new QTableWidgetItem(preview[i].action));
        if (preview[i].will_run)
        {
            row_of_target_.append(i);
            table_->setItem(i, 2, new QTableWidgetItem(tr("Pending")));
            table_->setItem(i, 3, new QTableWidgetItem(QString()));
        }
        else
        {
            row_of_target_.append(-1);
            table_->setItem(i, 2, new QTableWidgetItem(statusText("skipped")));
            table_->setItem(i, 3, new QTableWidgetItem(preview[i].detail));
        }
    }

    // Sanity: preview rows that run must line up with the runner targets one to one.
    int run_count = 0;
    for (const auto& row : preview)
    {
        if (row.will_run)
            ++run_count;
    }
    if (run_count == 0)
    {
        status_label_->setText(tr("Nothing to do."));
        apply_button_->setEnabled(false);
    }
    else
    {
        status_label_->setText(tr("%n host(s) selected.", "", run_count));
    }

    running_ = false;
    completed_ = false;
    cancel_button_->setText(tr("Cancel"));

    connect(runner_, &RouterHostBulkRunner::resultReady,
            this, &RouterHostBulkDialog::onResultReady);
    connect(runner_, &RouterHostBulkRunner::finished,
            this, &RouterHostBulkDialog::onFinished);

    return exec();
}

//--------------------------------------------------------------------------------------------------
void RouterHostBulkDialog::onButtonClicked(QAbstractButton* button)
{
    if (buttons_->buttonRole(button) == QDialogButtonBox::ActionRole)
    {
        // Apply: start the run, stay in the dialog.
        if (running_ || completed_)
            return;
        running_ = true;
        apply_button_->setEnabled(false);
        runner_->start();
        return;
    }

    // Cancel/Close.
    if (running_ && !completed_)
    {
        runner_->cancel();
        cancel_button_->setEnabled(false);
        status_label_->setText(tr("Cancelling, waiting for the current host..."));
        return;
    }

    if (completed_)
        accept();
    else
        reject();
}

//--------------------------------------------------------------------------------------------------
void RouterHostBulkDialog::onResultReady(int index)
{
    const QList<RouterHostBulkResult>& results = runner_->results();
    if (index < 0 || index >= results.size())
        return;

    const RouterHostBulkResult& result = results[index];
    if (index < row_of_target_.size() && row_of_target_[index] >= 0)
    {
        setRowResult(row_of_target_[index], result.status, result.detail);
        status_label_->setText(tr("%1 of %2 done.").arg(index + 1).arg(row_of_target_.size()));
    }
}

//--------------------------------------------------------------------------------------------------
void RouterHostBulkDialog::onFinished()
{
    running_ = false;
    completed_ = true;

    int ok = 0, skipped = 0, failed = 0, offline = 0, cancelled = 0;
    for (const RouterHostBulkResult& result : runner_->results())
    {
        if (result.status == "ok")
            ++ok;
        else if (result.status == "failed")
            ++failed;
        else if (result.status == "offline")
            ++offline;
        else if (result.status == "cancelled")
            ++cancelled;
    }
    for (int row : row_of_target_)
    {
        if (row < 0)
            ++skipped;
    }

    status_label_->setText(
        tr("Done: %1, failed: %2, offline: %3, skipped: %4, cancelled: %5.")
            .arg(ok).arg(failed).arg(offline).arg(skipped).arg(cancelled));
    cancel_button_->setText(tr("Close"));
    cancel_button_->setEnabled(true);
}

//--------------------------------------------------------------------------------------------------
void RouterHostBulkDialog::setRowResult(int row, const QString& status, const QString& detail)
{
    table_->setItem(row, 2, new QTableWidgetItem(statusText(status)));
    table_->setItem(row, 3, new QTableWidgetItem(detail));
}

//--------------------------------------------------------------------------------------------------
RouterHostBulkFullConfigDialog::RouterHostBulkFullConfigDialog(const QString& file_summary,
                                                              QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Full configuration on hosts"));
    setMinimumWidth(460);

    auto* layout = new QVBoxLayout(this);

    auto* summary = new QLabel(file_summary, this);
    summary->setWordWrap(true);
    layout->addWidget(summary);

    auto* warning = new QLabel(
        tr("Warning: without the checkbox below, every host switches to the router, "
           "port and access settings from the file. Make sure the file comes from a host "
           "of this router."),
        this);
    warning->setWordWrap(true);
    layout->addWidget(warning);

    check_keep_ = new QCheckBox(
        tr("Keep router connection, port and access settings "
           "(apply only system settings and users)"),
        this);
    check_keep_->setChecked(true);
    layout->addWidget(check_keep_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::clicked,
            this, &RouterHostBulkFullConfigDialog::onButtonClicked);
}

//--------------------------------------------------------------------------------------------------
bool RouterHostBulkFullConfigDialog::keepRouter() const
{
    return check_keep_->isChecked();
}

//--------------------------------------------------------------------------------------------------
void RouterHostBulkFullConfigDialog::onButtonClicked(QAbstractButton* button)
{
    auto* button_box = qobject_cast<QDialogButtonBox*>(sender());
    if (!button_box || button_box->standardButton(button) != QDialogButtonBox::Ok)
        reject();
    else
        accept();
}

//--------------------------------------------------------------------------------------------------
RouterHostBulkRemoveDialog::RouterHostBulkRemoveDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Remove user from hosts"));
    setMinimumWidth(380);

    auto* layout = new QFormLayout(this);

    edit_name_ = new QLineEdit(this);
    layout->addRow(tr("User name:"), edit_name_);

    check_force_ = new QCheckBox(
        tr("Force (remove even with live sessions, last enabled user is still protected)"), this);
    layout->addRow(QString(), check_force_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addRow(buttons);

    connect(buttons, &QDialogButtonBox::clicked,
            this, &RouterHostBulkRemoveDialog::onButtonClicked);
}

//--------------------------------------------------------------------------------------------------
QString RouterHostBulkRemoveDialog::userName() const
{
    return edit_name_->text().trimmed();
}

//--------------------------------------------------------------------------------------------------
bool RouterHostBulkRemoveDialog::force() const
{
    return check_force_->isChecked();
}

//--------------------------------------------------------------------------------------------------
void RouterHostBulkRemoveDialog::onButtonClicked(QAbstractButton* button)
{
    auto* button_box = qobject_cast<QDialogButtonBox*>(sender());
    if (!button_box || button_box->standardButton(button) != QDialogButtonBox::Ok)
    {
        reject();
        return;
    }

    if (edit_name_->text().trimmed().isEmpty())
    {
        MsgBox::warning(this, tr("Enter a user name."));
        edit_name_->setFocus();
        return;
    }

    accept();
}
