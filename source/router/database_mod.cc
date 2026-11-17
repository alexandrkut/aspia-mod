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

#include "router/database.h"

#include <QDir>
#include <QFileInfo>

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "base/logging.h"
#include "base/files/base_paths.h"
#include "base/sql/sql_query.h"
#include "base/sql/sql_transaction.h"
#include "base/time_types.h"
#include "proto/router_client.h"
#include "proto/router_constants.h"

namespace {

} // namespace

std::string_view Database::addHostMembership(HostId host_id, qint64 workspace_id, qint64 group_id)
{
    if (!isValid())
    {
        LOG(ERROR) << "Database is not valid";
        return proto::router::kErrorInternalError;
    }

    if (host_id == kInvalidHostId || workspace_id <= 0 || group_id < 0)
    {
        LOG(ERROR) << "Invalid membership add:" << host_id << workspace_id << group_id;
        return proto::router::kErrorInvalidData;
    }

    SqlTransaction transaction(db_);
    if (!transaction.begin(SqlTransaction::Mode::IMMEDIATE))
    {
        LOG(ERROR) << "Unable to start transaction:" << db_.lastError();
        return proto::router::kErrorInternalError;
    }

    SqlQuery workspace_check(db_, "SELECT 1 FROM workspaces WHERE id=?");
    workspace_check.addInt64(workspace_id);

    const SqlQuery::StepResult check_step = workspace_check.next();
    if (check_step == SqlQuery::StepResult::FAILED)
    {
        LOG(ERROR) << "Unable to execute query:" << db_.lastError();
        return proto::router::kErrorInternalError;
    }

    if (check_step != SqlQuery::StepResult::ROW)
    {
        LOG(ERROR) << "Workspace not found:" << workspace_id;
        return proto::router::kErrorConflict;
    }

    // An existing membership keeps its group: adding is idempotent, moving the group is a
    // separate edit of the place within one workspace.
    SqlQuery insert(db_,
        "INSERT OR IGNORE INTO host_workspaces (host_id, workspace_id, group_id) VALUES (?, ?, ?)");
    insert.addUInt64(host_id);
    insert.addInt64(workspace_id);
    insert.addInt64(group_id);

    if (!insert.exec())
    {
        LOG(ERROR) << "Unable to execute query:" << db_.lastError();
        return proto::router::kErrorInternalError;
    }

    refreshPrimaryMembership(host_id);

    SqlQuery touch(db_,
        "UPDATE hosts SET revision=revision+1, last_modify=? WHERE id=?");
    touch.addInt64(secondsSinceEpoch());
    touch.addUInt64(host_id);

    if (!touch.exec())
    {
        LOG(ERROR) << "Unable to execute query:" << db_.lastError();
        return proto::router::kErrorInternalError;
    }

    if (!transaction.commit())
    {
        LOG(ERROR) << "Unable to commit transaction:" << db_.lastError();
        return proto::router::kErrorInternalError;
    }

    return proto::router::kErrorOk;
}


template <typename List>
void Database::attachMemberships(const std::vector<quint64>& host_ids, List* list) const
{
    if (host_ids.empty() || !list)
        return;

    std::string placeholders;
    placeholders.reserve(host_ids.size() * 2);
    for (size_t i = 0; i < host_ids.size(); ++i)
        placeholders += i ? ",?" : "?";

    // One query for the whole page instead of one per host.
    SqlQuery query(db_,
        "SELECT host_id, workspace_id, group_id FROM host_workspaces WHERE host_id IN (" +
        placeholders + ") ORDER BY host_id, rowid");
    for (quint64 host_id : host_ids)
        query.addUInt64(host_id);

    std::unordered_map<quint64, std::vector<std::pair<qint64, qint64>>> memberships;
    for (;;)
    {
        const SqlQuery::StepResult step = query.next();
        if (step != SqlQuery::StepResult::ROW)
        {
            if (step == SqlQuery::StepResult::FAILED)
                LOG(ERROR) << "Unable to execute query:" << db_.lastError();
            break;
        }
        memberships[query.columnUInt64(0)].emplace_back(query.columnInt64(1),
                                                       query.columnInt64(2));
    }

    // The scalar workspace_id/group_id on each host stay the primary membership; this fills the
    // repeated memberships with every workspace the host belongs to.
    auto* hosts = list->mutable_host();
    for (int i = 0; i < hosts->size(); ++i)
    {
        proto::router::Host* host = hosts->Mutable(i);
        const auto it = memberships.find(host->host_id());
        if (it == memberships.end())
            continue;

        for (const auto& membership : it->second)
        {
            proto::router::HostMembership* added = host->add_memberships();
            added->set_workspace_id(membership.first);
            added->set_group_id(membership.second);
        }
    }
}


std::string_view Database::hostCountOutside(qint64 workspace_id, qint64* count) const
{
    CHECK(count);

    *count = 0;

    if (!isValid())
    {
        LOG(ERROR) << "Database is not valid";
        return proto::router::kErrorInternalError;
    }

    // Same scope as the matching hostsOutside() overload.
    const std::string sql =
        "SELECT COUNT(*) FROM hosts WHERE NOT EXISTS (SELECT 1 FROM host_workspaces hw WHERE "
        "hw.host_id = hosts.id AND hw.workspace_id = ?)";

    SqlQuery query(db_, sql);
    if (!query.isValid())
    {
        LOG(ERROR) << "Unable to execute query:" << db_.lastError();
        return proto::router::kErrorInternalError;
    }

    query.addInt64(workspace_id);

    // See the unfiltered overload: a missing row is an error, not an empty scope.
    if (query.next() != SqlQuery::StepResult::ROW)
    {
        LOG(ERROR) << "Unable to execute query:" << db_.lastError();
        return proto::router::kErrorInternalError;
    }

    *count = query.columnInt64(0);
    return proto::router::kErrorOk;
}


std::vector<qint64> Database::hostWorkspaceIds(HostId host_id) const
{
    std::vector<qint64> workspace_ids;

    if (!isValid())
    {
        LOG(ERROR) << "Database is not valid";
        return workspace_ids;
    }

    SqlQuery query(db_, "SELECT workspace_id FROM host_workspaces WHERE host_id=? ORDER BY rowid");
    query.addUInt64(host_id);

    for (;;)
    {
        const SqlQuery::StepResult step = query.next();
        if (step != SqlQuery::StepResult::ROW)
        {
            if (step == SqlQuery::StepResult::FAILED)
                LOG(ERROR) << "Unable to execute query:" << db_.lastError();
            break;
        }
        workspace_ids.emplace_back(query.columnInt64(0));
    }

    return workspace_ids;
}


void Database::hostsOutside(qint64 workspace_id, qint64 offset, qint64 count,
    proto::router::HostList* out) const
{
    if (!isValid())
    {
        LOG(ERROR) << "Database is not valid";
        out->set_error_code(proto::router::kErrorInternalError);
        return;
    }

    if (!isPageValid(offset, count, proto::router::kMaxHostPageSize))
    {
        LOG(ERROR) << "Invalid host list page: offset" << offset << "count" << count;
        out->set_error_code(proto::router::kErrorInvalidRequest);
        return;
    }

    // The complement of the membership scope above: a host qualifies when no membership row
    // ties it to the workspace. That reads from one workspace's point of view as "everywhere
    // else" - the hosts of the other workspaces and the hosts without any workspace.
    const std::string sql =
        "SELECT id, revision, workspace_id, group_id, display_name, computer_name, cpu_arch, "
        "version, os_name, address, comment, last_connect, last_modify FROM hosts "
        "WHERE NOT EXISTS (SELECT 1 FROM host_workspaces hw WHERE hw.host_id = hosts.id AND "
        "hw.workspace_id = ?) ORDER BY id LIMIT ? OFFSET ?";

    SqlQuery query(db_, sql);
    if (!query.isValid())
    {
        LOG(ERROR) << "Unable to execute query:" << db_.lastError();
        out->set_error_code(proto::router::kErrorInternalError);
        return;
    }

    query.addInt64(workspace_id);
    query.addInt64(count);
    query.addInt64(offset);

    std::vector<quint64> page_ids;
    page_ids.reserve(static_cast<size_t>(count));

    for (;;)
    {
        const SqlQuery::StepResult step = query.next();
        if (step == SqlQuery::StepResult::FAILED)
        {
            // An error reply must not carry the partial list scanned so far.
            LOG(ERROR) << "Unable to execute query:" << db_.lastError();
            out->clear_host();
            out->set_error_code(proto::router::kErrorInternalError);
            return;
        }

        if (step == SqlQuery::StepResult::DONE)
            break;

        proto::router::Host* host = out->add_host();
        host->set_host_id(query.columnUInt64(0));
        host->set_revision(query.columnInt64(1));
        host->set_workspace_id(query.columnInt64(2));
        host->set_group_id(query.columnInt64(3));
        host->set_display_name(query.columnTextView(4));
        host->set_computer_name(query.columnTextView(5));
        host->set_cpu_arch(query.columnTextView(6));
        host->set_version(query.columnTextView(7));
        host->set_os_name(query.columnTextView(8));
        host->set_address(query.columnTextView(9));
        host->set_comment(query.columnTextView(10));
        host->set_last_connect(query.columnInt64(11));
        host->set_last_modify(query.columnInt64(12));
        page_ids.emplace_back(host->host_id());
    }

    attachMemberships(page_ids, out);

    out->set_error_code(proto::router::kErrorOk);
}


void Database::refreshPrimaryMembership(HostId host_id)
{
    // The scalar columns on |hosts| mirror the primary membership - the first row added, the
    // lowest rowid - so scalar-based reads and an old client keep seeing a single workspace.
    // Revision and last_modify belong to the caller: only operator edits and the cascades that
    // touch those fields move them.
    qint64 workspace_id = 0;
    qint64 group_id = 0;

    SqlQuery primary(db_,
        "SELECT workspace_id, group_id FROM host_workspaces WHERE host_id=? ORDER BY rowid LIMIT 1");
    primary.addUInt64(host_id);

    if (primary.next() == SqlQuery::StepResult::ROW)
    {
        workspace_id = primary.columnInt64(0);
        group_id = primary.columnInt64(1);
    }

    SqlQuery update(db_, "UPDATE hosts SET workspace_id=?, group_id=? WHERE id=?");
    update.addInt64(workspace_id);
    update.addInt64(group_id);
    update.addUInt64(host_id);

    if (!update.exec())
        LOG(ERROR) << "Unable to execute query:" << db_.lastError();
}


std::string_view Database::removeHostMembership(HostId host_id, qint64 workspace_id)
{
    if (!isValid())
    {
        LOG(ERROR) << "Database is not valid";
        return proto::router::kErrorInternalError;
    }

    if (host_id == kInvalidHostId || workspace_id <= 0)
    {
        LOG(ERROR) << "Invalid membership remove:" << host_id << workspace_id;
        return proto::router::kErrorInvalidData;
    }

    SqlTransaction transaction(db_);
    if (!transaction.begin(SqlTransaction::Mode::IMMEDIATE))
    {
        LOG(ERROR) << "Unable to start transaction:" << db_.lastError();
        return proto::router::kErrorInternalError;
    }

    SqlQuery del(db_, "DELETE FROM host_workspaces WHERE host_id=? AND workspace_id=?");
    del.addUInt64(host_id);
    del.addInt64(workspace_id);

    if (!del.exec())
    {
        LOG(ERROR) << "Unable to execute query:" << db_.lastError();
        return proto::router::kErrorInternalError;
    }

    refreshPrimaryMembership(host_id);

    SqlQuery touch(db_,
        "UPDATE hosts SET revision=revision+1, last_modify=? WHERE id=?");
    touch.addInt64(secondsSinceEpoch());
    touch.addUInt64(host_id);

    if (!touch.exec())
    {
        LOG(ERROR) << "Unable to execute query:" << db_.lastError();
        return proto::router::kErrorInternalError;
    }

    if (!transaction.commit())
    {
        LOG(ERROR) << "Unable to commit transaction:" << db_.lastError();
        return proto::router::kErrorInternalError;
    }

    return proto::router::kErrorOk;
}


bool Database::updateHostVersion(HostId host_id, std::string_view version)
{
    if (!isValid())
    {
        LOG(ERROR) << "Database is not valid";
        return false;
    }

    SqlQuery query(db_, "UPDATE hosts SET version=? WHERE id=?");
    query.addText(version);
    query.addUInt64(host_id);

    if (!query.exec())
    {
        LOG(ERROR) << "Unable to execute query:" << db_.lastError();
        return false;
    }

    return db_.changes() > 0;
}

template void Database::attachMemberships<proto::router::HostList>(
    const std::vector<quint64>& host_ids, proto::router::HostList* list) const;
template void Database::attachMemberships<proto::router::HostSearchResult>(
    const std::vector<quint64>& host_ids, proto::router::HostSearchResult* list) const;
