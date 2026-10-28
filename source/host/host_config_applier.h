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

#ifndef HOST_HOST_CONFIG_APPLIER_H
#define HOST_HOST_CONFIG_APPLIER_H

#include <QJsonObject>
#include <QString>

#include "base/logging.h"
#include "host/database.h"
#include "host/system_settings.h"

// Applies a configuration command received from the router (issued by an admin) to the host.
// Every command is validated before any write; on any failure the existing configuration is
// preserved. The result carries the error code, the applied values, a diff (old -> new) and,
// where applicable, a snapshot of the previous configuration for rollback.
class HostConfigApplier
{
public:
    struct Result
    {
        QString error_code;       // "ok" | "invalid_request" | "invalid_data" | ...
        QJsonObject applied;      // what was actually written
        QJsonObject diff;         // {key: {"old": ..., "new": ...}}
        bool restart_required = false;
        QString snapshot;         // JSON of the previous configuration (for rollback)
    };

    // Dispatches by command_name. Unknown command -> "invalid_request".
    // |has_active_sessions| is optional: if provided, user-targeting commands are refused when
    // the target user has live client sessions (unless params contains "force": true).
    static Result apply(const std::string& command_name, const QJsonObject& params,
                       const std::string& initiator,
                       const std::function<bool(const QString&)>& has_active_sessions = nullptr);

    static Result applySetUpdateConfig(const QJsonObject& params, const std::string& initiator);
    static Result applyAddUser(const QJsonObject& params, const std::string& initiator);
    static Result applyModifyUser(const QJsonObject& params, const std::string& initiator,
                                const std::function<bool(const QString&)>& has_active_sessions = nullptr);
    static Result applyRemoveUser(const QJsonObject& params, const std::string& initiator,
                                const std::function<bool(const QString&)>& has_active_sessions = nullptr);
    static Result applyGetHostUsers(const QJsonObject& params, const std::string& initiator);
    static Result applySetAccessConfig(const QJsonObject& params, const std::string& initiator);
    static Result applySetOnetimeConfig(const QJsonObject& params, const std::string& initiator);
    static Result applyReplaceUsers(const QJsonObject& params, const std::string& initiator);
    static Result applySetMisc(const QJsonObject& params, const std::string& initiator);
    static Result applyFullConfig(const QJsonObject& params, const std::string& initiator);

    // Snapshot of the current (exportable) configuration, used for rollback and dry-run.
    static QJsonObject snapshotConfig();

private:
    // Validation helpers.
    static bool validateUrl(const QString& url);
    static bool validateKeyHex(const QString& hex);
    static bool validateSessionMask(quint32 mask);
    static bool validateGroup(const QString& group);

    // Error codes (must match the strings in proto/router_constants.h).
    static const char* const kOk;
    static const char* const kInvalidRequest;
    static const char* const kInvalidData;
    static const char* const kNotFound;
    static const char* const kAlreadyExists;
    static const char* const kUserHasSessions;
    static const char* const kLastEnabledUser;
    static const char* const kNotSupported;

    static void audit(const std::string& initiator, const std::string& command, const Result& result);

    Q_DISABLE_COPY_MOVE(HostConfigApplier)
};

#endif // HOST_HOST_CONFIG_APPLIER_H
