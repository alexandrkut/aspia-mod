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
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QProgressDialog>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include "base/logging.h"
#include "base/peer/user.h"
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


// Display label for a host in bulk tables.
QString bulkHostLabel(const RouterHost& host)
{
    if (!host.display_name.isEmpty() && !host.computer_name.isEmpty())
        return host.display_name + " (" + host.computer_name + ")";
    if (!host.display_name.isEmpty())
        return host.display_name;
    if (!host.computer_name.isEmpty())
        return host.computer_name;
    return QString::number(host.host_id);
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
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(table_);

    auto* detail_label = new QLabel(tr("Details:"), this);
    layout->addWidget(detail_label);

    detail_view_ = new QPlainTextEdit(this);
    detail_view_->setReadOnly(true);
    detail_view_->setMaximumHeight(90);
    layout->addWidget(detail_view_);

    connect(table_, &QTableWidget::itemSelectionChanged,
            this, &RouterHostBulkDialog::onSelectionChanged);

    buttons_ = new QDialogButtonBox(this);
    apply_button_ = buttons_->addButton(tr("Apply"), QDialogButtonBox::ActionRole);
    cancel_button_ = buttons_->addButton(QDialogButtonBox::Cancel);
    layout->addWidget(buttons_);

    connect(buttons_, &QDialogButtonBox::clicked,
            this, &RouterHostBulkDialog::onButtonClicked);
}

//--------------------------------------------------------------------------------------------------
int RouterHostBulkDialog::execBulk(RouterHostBulkRunner* runner,
                                   const QList<RouterHostBulkPreviewRow>& preview,
                                   const QString& confirm_word)
{
    runner_ = runner;
    confirm_word_ = confirm_word.trimmed();
    row_of_target_.clear();

    auto set_cell = [this](int row, int column, const QString& text)
    {
        auto* item = new QTableWidgetItem(text);
        item->setToolTip(text);
        table_->setItem(row, column, item);
    };

    table_->setRowCount(preview.size());
    for (int i = 0; i < preview.size(); ++i)
    {
        set_cell(i, 0, preview[i].host);
        set_cell(i, 1, preview[i].action);
        if (preview[i].will_run)
        {
            // The map is indexed by TARGET number: the runner's result index goes straight
            // into it. A skipped row must not append anything, or every index after the
            // first skip would shift and that row would never be updated (bug of 13.11.2026:
            // rows after a skipped one kept "Pending" forever).
            row_of_target_.append(i);
            set_cell(i, 2, tr("Pending"));
            set_cell(i, 3, QString());
        }
        else
        {
            set_cell(i, 2, statusText("skipped"));
            set_cell(i, 3, preview[i].detail);
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

    // Optional confirmation gate: Apply stays disabled until the word is typed.
    if (!confirm_word_.isEmpty())
    {
        auto* hint = new QLabel(
            tr("Type %1 to enable Apply.").arg(confirm_word_), this);
        hint->setWordWrap(true);
        layout()->addWidget(hint);

        confirm_edit_ = new QLineEdit(this);
        layout()->addWidget(confirm_edit_);

        const bool has_work = (run_count > 0);
        apply_button_->setEnabled(false);
        connect(confirm_edit_, &QLineEdit::textChanged, this,
                [this, has_work](const QString&)
        {
            if (running_ || completed_)
                return;
            const bool ok = has_work &&
                (confirm_edit_->text().trimmed().compare(confirm_word_, Qt::CaseSensitive) == 0);
            apply_button_->setEnabled(ok);
        });
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
    if (index < row_of_target_.size())
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
    // The preview rows that never entered the map are the skipped ones.
    skipped = table_->rowCount() - static_cast<int>(row_of_target_.size());

    status_label_->setText(
        tr("Done: %1, failed: %2, offline: %3, skipped: %4, cancelled: %5.")
            .arg(ok).arg(failed).arg(offline).arg(skipped).arg(cancelled));
    cancel_button_->setText(tr("Close"));
    cancel_button_->setEnabled(true);
}

//--------------------------------------------------------------------------------------------------
void RouterHostBulkDialog::setRowResult(int row, const QString& status, const QString& detail)
{
    const QString status_text = statusText(status);
    auto* status_item = new QTableWidgetItem(status_text);
    status_item->setToolTip(status_text);
    table_->setItem(row, 2, status_item);

    auto* detail_item = new QTableWidgetItem(detail);
    detail_item->setToolTip(detail);
    table_->setItem(row, 3, detail_item);

    if (table_->currentRow() == row)
        onSelectionChanged();
}

//--------------------------------------------------------------------------------------------------
void RouterHostBulkDialog::onSelectionChanged()
{
    const int row = table_->currentRow();
    if (row < 0 || row >= table_->rowCount())
    {
        detail_view_->clear();
        return;
    }

    const QString host = table_->item(row, 0) ? table_->item(row, 0)->toolTip() : QString();
    const QString action = table_->item(row, 1) ? table_->item(row, 1)->toolTip() : QString();
    const QString result = table_->item(row, 2) ? table_->item(row, 2)->toolTip() : QString();
    const QString detail = table_->item(row, 3) ? table_->item(row, 3)->toolTip() : QString();

    QString text = tr("Host: %1\nPlanned: %2\nResult: %3").arg(host, action, result);
    if (!detail.isEmpty())
        text += tr("\nDetail: %1").arg(detail);
    detail_view_->setPlainText(text);
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
RouterHostBulkUserPickDialog::RouterHostBulkUserPickDialog(const QList<RouterHost>& hosts,
                                                          const QList<RouterHostBulkResult>& fetched,
                                                          QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Remove user from hosts"));
    setMinimumSize(620, 420);

    auto* layout = new QVBoxLayout(this);

    // Aggregate unique user names (case-insensitive) across readable hosts.
    int offline_count = 0;
    int unreadable_count = 0;
    int readable_count = 0;
    int queried_count = 0;

    QMap<QString, int> name_index;
    QStringList name_hosts_list; // placeholder
    QVector<QStringList> name_hosts;
    QVector<int> name_total;
    QVector<int> name_enabled;

    int fetched_index = 0;
    for (int i = 0; i < hosts.size(); ++i)
    {
        if (!hosts[i].online)
        {
            ++offline_count;
            continue;
        }
        ++queried_count;
        if (fetched_index >= fetched.size())
        {
            ++unreadable_count;
            ++fetched_index;
            continue;
        }
        const RouterHostBulkResult& f = fetched[fetched_index++];
        if (f.status != "ok")
        {
            ++unreadable_count;
            continue;
        }
        ++readable_count;

        QJsonDocument doc = QJsonDocument::fromJson(f.raw.toUtf8());
        if (!doc.isArray())
        {
            ++unreadable_count;
            --readable_count;
            continue;
        }

        const QString host_label = bulkHostLabel(hosts[i]);
        for (const QJsonValue& v : doc.array())
        {
            const QJsonObject o = v.toObject();
            const QString name = o.value("name").toString();
            if (name.isEmpty())
                continue;
            const QString key = name.toLower();
            if (!name_index.contains(key))
            {
                name_index[key] = names_.size();
                names_.append(name);
                name_hosts.append(QStringList());
                name_total.append(0);
                name_enabled.append(0);
            }
            const int idx = name_index[key];
            name_hosts[idx].append(host_label);
            ++name_total[idx];
            if (o.value("flags").toInteger() & User::ENABLED)
                ++name_enabled[idx];
        }
    }

    auto* summary = new QLabel(
        tr("Queried hosts: %1, offline: %2, unreadable: %3")
            .arg(queried_count).arg(offline_count).arg(unreadable_count),
        this);
    layout->addWidget(summary);

    table_ = new QTableWidget(this);
    table_->setColumnCount(3);
    table_->setHorizontalHeaderLabels(
        {tr("User"), tr("Hosts"), tr("Status")});
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    layout->addWidget(table_);

    table_->setRowCount(names_.size());
    for (int i = 0; i < names_.size(); ++i)
    {
        auto* name_item = new QTableWidgetItem(names_[i]);
        name_item->setFlags(name_item->flags() | Qt::ItemIsUserCheckable);
        name_item->setCheckState(Qt::Unchecked);
        table_->setItem(i, 0, name_item);

        const QString hosts_text = tr("%1 of %2").arg(name_total[i]).arg(readable_count);
        auto* hosts_item = new QTableWidgetItem(hosts_text);
        hosts_item->setToolTip(name_hosts[i].join(QStringLiteral(", ")));
        table_->setItem(i, 1, hosts_item);

        QString status;
        if (name_total[i] > 0 && name_enabled[i] == name_total[i])
            status = tr("enabled");
        else if (name_enabled[i] == 0)
            status = tr("disabled");
        else
            status = tr("enabled on some hosts");
        auto* status_item = new QTableWidgetItem(status);
        table_->setItem(i, 2, status_item);
    }

    auto* warning = new QLabel(
        tr("Warning: removing a user is irreversible. The password cannot be "
           "restored, only recreated."),
        this);
    warning->setWordWrap(true);
    layout->addWidget(warning);

    check_force_ = new QCheckBox(
        tr("Force (remove even with live sessions; last enabled user is still protected)"),
        this);
    check_force_->setChecked(false);
    layout->addWidget(check_force_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Next step"));
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

//--------------------------------------------------------------------------------------------------
QStringList RouterHostBulkUserPickDialog::selectedNames() const
{
    QStringList out;
    if (!table_)
        return out;
    for (int i = 0; i < table_->rowCount(); ++i)
    {
        const QTableWidgetItem* item = table_->item(i, 0);
        if (item && item->checkState() == Qt::Checked)
        {
            if (i < names_.size())
                out.append(names_[i]);
        }
    }
    return out;
}

//--------------------------------------------------------------------------------------------------
bool RouterHostBulkUserPickDialog::force() const
{
    return check_force_ ? check_force_->isChecked() : false;
}
