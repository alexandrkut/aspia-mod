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

#include "host/host_config_applier.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSet>

#include "base/build_config.h"
#include "base/logging.h"
#include "base/net/address.h"
#include "base/crypto/srp_math.h"
#include "base/peer/user.h"
#include "base/time_types.h"
#include "host/database.h"
#include "host/system_settings.h"

namespace {
constexpr const char* kInitiatorKey = "initiator";
} // namespace

const char* const HostConfigApplier::kOk = "ok";
const char* const HostConfigApplier::kInvalidRequest = "invalid_request";
const char* const HostConfigApplier::kInvalidData = "invalid_data";
const char* const HostConfigApplier::kNotFound = "not_found";
const char* const HostConfigApplier::kAlreadyExists = "already_exists";
const char* const HostConfigApplier::kUserHasSessions = "user_has_sessions";
const char* const HostConfigApplier::kLastEnabledUser = "last_enabled_user";
const char* const HostConfigApplier::kNotSupported = "not_supported";

//--------------------------------------------------------------------------------------------------
HostConfigApplier::Result HostConfigApplier::apply(const std::string& command_name,
                                                  const QJsonObject& params,
                                                  const std::string& initiator,
                                                  const std::function<bool(const QString&)>& has_active_sessions)
{
    Result result;

    if (command_name == "set_update_config")
        result = applySetUpdateConfig(params, initiator);
    else if (command_name == "add_user")
        result = applyAddUser(params, initiator);
    else if (command_name == "modify_user")
        result = applyModifyUser(params, initiator, has_active_sessions);
    else if (command_name == "remove_user")
        result = applyRemoveUser(params, initiator, has_active_sessions);
    else if (command_name == "get_host_users")
        result = applyGetHostUsers(params, initiator);
    else if (command_name == "set_access_config")
        result = applySetAccessConfig(params, initiator);
    else if (command_name == "set_onetime_config")
        result = applySetOnetimeConfig(params, initiator);
    else if (command_name == "replace_users")
        result = applyReplaceUsers(params, initiator);
    else if (command_name == "set_misc")
        result = applySetMisc(params, initiator);
    else if (command_name == "apply_full_config")
        result = applyFullConfig(params, initiator);
    else
    {
        result.error_code = kInvalidRequest;
        return result;
    }

    // Fill the result payload for the router response if the specific method did not set it.
    if (result.result.isEmpty())
    {
        if (!result.diff.isEmpty())
            result.result = QString::fromUtf8(QJsonDocument(result.diff).toJson(QJsonDocument::Compact));
        else if (!result.applied.isEmpty())
            result.result = QString::fromUtf8(QJsonDocument(result.applied).toJson(QJsonDocument::Compact));
        else if (!result.snapshot.isEmpty())
            result.result = result.snapshot;
    }

    audit(initiator, command_name, result);
    return result;
}

//--------------------------------------------------------------------------------------------------
// Validation helpers
bool HostConfigApplier::validateUrl(const QString& url)
{
    if (url.isEmpty())
        return true; // empty = default server
    const QUrl parsed(url, QUrl::StrictMode);
    return parsed.isValid() && !parsed.host().isEmpty() &&
           (parsed.scheme() == "https" || parsed.scheme() == "http");
}

bool HostConfigApplier::validateKeyHex(const QString& hex)
{
    if (hex.isEmpty())
        return true; // empty = default key
    const QByteArray bytes = QByteArray::fromHex(hex.toLower().toLatin1());
    return bytes.size() == 32 && bytes.toHex() == hex.toLower().toLatin1();
}

bool HostConfigApplier::validateSessionMask(quint32 mask)
{
    // Known bits: DESKTOP(1)|FILE_TRANSFER(4)|SYSTEM_INFO(8)|CHAT(16)|TERMINAL(32) = 63
    constexpr quint32 kKnownMask = 0x3F;
    return (mask & ~kKnownMask) == 0;
}

bool HostConfigApplier::validateGroup(const QString& group)
{
    // Group is an SRP group name; must be one SrpMath knows.
    return !group.isEmpty() && SrpMath::pairByGroup(group).has_value();
}

//--------------------------------------------------------------------------------------------------
QJsonObject HostConfigApplier::snapshotConfig()
{
    QJsonObject root;
    SystemSettings settings;
    root["system"] = QJsonObject{
        {"update_channel", settings.updateChannel()},
        {"preferred_video_capturer", static_cast<qint64>(settings.preferredVideoCapturer())},
        {"application_shutdown_disabled", settings.isApplicationShutdownDisabled()},
        {"auto_update_enabled", settings.isAutoUpdateEnabled()},
        {"update_check_frequency", settings.updateCheckFrequency()},
        {"update_server", settings.updateServer()},
        {"update_public_key", QString::fromLatin1(settings.updatePublicKey().toHex())},
    };

    Database& db = Database::instance();
    QJsonObject db_obj;
    db_obj["tcp_port"] = db.tcpPort();
    db_obj["router_enabled"] = db.isRouterEnabled();
    db_obj["router_address"] = db.routerAddress().toString();
    db_obj["router_public_key"] = QString::fromLatin1(db.routerPublicKey().toHex());
    db_obj["connect_confirmation"] = db.connectConfirmation();
    db_obj["no_user_action"] = static_cast<int>(db.noUserAction());
    db_obj["auto_confirmation_interval"] = static_cast<qint64>(db.autoConfirmationInterval().count());
    db_obj["one_time_password"] = db.oneTimePassword();
    db_obj["one_time_password_expire"] = static_cast<qint64>(db.oneTimePasswordExpire().count());
    db_obj["one_time_password_length"] = db.oneTimePasswordLength();
    db_obj["one_time_password_characters"] = static_cast<qint64>(db.oneTimePasswordCharacters());

    QJsonArray users_array;
    for (const User& u : db.userList())
    {
        QJsonObject uo;
        uo["name"] = u.name;
        uo["group"] = u.group;
        uo["salt"] = QString::fromLatin1(u.salt.toHex());
        uo["verifier"] = QString::fromLatin1(u.verifier.toHex());
        uo["sessions"] = static_cast<qint64>(u.sessions);
        uo["flags"] = static_cast<qint64>(u.flags);
        users_array.append(uo);
    }
    db_obj["users"] = users_array;
    root["database"] = db_obj;
    return root;
}

//--------------------------------------------------------------------------------------------------
void HostConfigApplier::audit(const std::string& initiator, const std::string& command, const Result& result)
{
    LOG(INFO) << "Host config command '" << command << "' from '" << initiator
              << "': " << result.error_code.toStdString()
              << (result.restart_required ? " (restart required)" : "");
}

//--------------------------------------------------------------------------------------------------
// applySetUpdateConfig
HostConfigApplier::Result HostConfigApplier::applySetUpdateConfig(const QJsonObject& params, const std::string& /*initiator*/)
{
    Result result;
    SystemSettings settings;

    const QString server = params.value("update_server").toString(settings.updateServer()).trimmed();
    const QString key_hex = params.value("update_public_key").toString(QString::fromLatin1(settings.updatePublicKey().toHex())).trimmed().toLower();
    const QString channel = params.value("update_channel").toString(settings.updateChannel()).trimmed();
    const bool auto_update = params.value("auto_update_enabled").toBool(settings.isAutoUpdateEnabled());
    const int freq = params.value("update_check_frequency").toInt(settings.updateCheckFrequency());

    if (!validateUrl(server))
    {
        result.error_code = kInvalidData;
        return result;
    }
    if (!validateKeyHex(key_hex))
    {
        result.error_code = kInvalidData;
        return result;
    }
    if (server.isEmpty() && !key_hex.isEmpty())
    {
        result.error_code = kInvalidData;
        return result;
    }

    // diff
    if (settings.updateServer() != server)
        result.diff["update_server"] = QJsonObject{{"old", settings.updateServer()}, {"new", server}};
    if (settings.updatePublicKey().toHex() != key_hex.toLatin1())
        result.diff["update_public_key"] = QJsonObject{{"old", QString::fromLatin1(settings.updatePublicKey().toHex())}, {"new", key_hex}};
    if (settings.updateChannel() != channel)
        result.diff["update_channel"] = QJsonObject{{"old", settings.updateChannel()}, {"new", channel}};
    if (settings.isAutoUpdateEnabled() != auto_update)
        result.diff["auto_update_enabled"] = QJsonObject{{"old", settings.isAutoUpdateEnabled()}, {"new", auto_update}};
    if (settings.updateCheckFrequency() != freq)
        result.diff["update_check_frequency"] = QJsonObject{{"old", settings.updateCheckFrequency()}, {"new", freq}};

    // snapshot
    result.snapshot = QString::fromUtf8(QJsonDocument(QJsonObject{{"system", QJsonObject{
        {"update_server", settings.updateServer()},
        {"update_public_key", QString::fromLatin1(settings.updatePublicKey().toHex())},
        {"update_channel", settings.updateChannel()},
        {"auto_update_enabled", settings.isAutoUpdateEnabled()},
        {"update_check_frequency", settings.updateCheckFrequency()},
    }}}).toJson(QJsonDocument::Compact));

    // apply
    settings.setUpdateServer(server);
    if (!key_hex.isEmpty())
        settings.setUpdatePublicKey(QByteArray::fromHex(key_hex.toLatin1()));
    settings.setUpdateChannel(channel);
    settings.setAutoUpdateEnabled(auto_update);
    settings.setUpdateCheckFrequency(freq);
    settings.sync();
    LOG(INFO) << "Update config applied: server=" << server << " channel=" << channel
              << " auto=" << auto_update << " freq=" << freq << " file=" << settings.filePath();

    result.applied = QJsonObject{
        {"update_server", server},
        {"update_public_key", key_hex},
        {"update_channel", channel},
        {"auto_update_enabled", auto_update},
        {"update_check_frequency", freq},
    };
    result.error_code = kOk;
    return result;
}

//--------------------------------------------------------------------------------------------------
HostConfigApplier::Result HostConfigApplier::applyAddUser(const QJsonObject& params, const std::string& /*initiator*/)
{
    Result result;
    Database& db = Database::instance();
    if (!db.isValid())
    {
        result.error_code = kInvalidRequest;
        return result;
    }

    const QString name = params.value("name").toString().trimmed();
    const QString group = params.value("group").toString().trimmed();
    const QString salt_hex = params.value("salt").toString().trimmed().toLower();
    const QString verifier_hex = params.value("verifier").toString().trimmed().toLower();
    const quint32 sessions = static_cast<quint32>(params.value("sessions").toInteger());
    const quint32 flags = static_cast<quint32>(params.value("flags").toInteger());

    if (name.isEmpty() || !validateGroup(group))
    {
        result.error_code = kInvalidData;
        return result;
    }
    if (name.startsWith('#'))
    {
        result.error_code = kInvalidData;
        return result;
    }
    if (!validateSessionMask(sessions))
    {
        result.error_code = kInvalidData;
        return result;
    }
    if (flags != User::ENABLED)
    {
        result.error_code = kInvalidData;
        return result;
    }
    const QByteArray salt = QByteArray::fromHex(salt_hex.toLatin1());
    const QByteArray verifier = QByteArray::fromHex(verifier_hex.toLatin1());
    if (salt.size() != 64 || verifier.isEmpty() || verifier.size() > 1024) // salt is 64 bytes; verifier size depends on group
    {
        result.error_code = kInvalidData;
        return result;
    }
    if (salt.toHex() != salt_hex.toLatin1() || verifier.toHex() != verifier_hex.toLatin1())
    {
        result.error_code = kInvalidData;
        return result;
    }

    // Verify the verifier is valid for the chosen group.
    std::optional<SrpMath::NgPair> ng = SrpMath::pairByGroup(group);
    if (!ng.has_value() || !SrpMath::verify_v(BigNum::fromByteArray(verifier), BigNum::fromStdString(ng->first)))
    {
        result.error_code = kInvalidData;
        return result;
    }

    // Duplicate name check
    for (const User& u : db.userList())
    {
        if (u.name.compare(name, Qt::CaseInsensitive) == 0)
        {
            result.error_code = kAlreadyExists;
            return result;
        }
    }

    User user;
    user.name = name;
    user.group = group;
    user.salt = salt;
    user.verifier = verifier;
    user.sessions = sessions;
    user.flags = flags;

    if (!db.addUser(user))
    {
        result.error_code = kInvalidData;
        return result;
    }

    result.applied = QJsonObject{
        {"name", name},
        {"group", group},
        {"sessions", static_cast<qint64>(sessions)},
        {"flags", static_cast<qint64>(flags)},
    };
    result.error_code = kOk;
    return result;
}

//--------------------------------------------------------------------------------------------------
HostConfigApplier::Result HostConfigApplier::applyModifyUser(const QJsonObject& params, const std::string& /*initiator*/,
                                                           const std::function<bool(const QString&)>& has_active_sessions)
{
    // ... (keep existing code, add active-session guard)
    Result result;
    Database& db = Database::instance();
    if (!db.isValid())
    {
        result.error_code = kInvalidRequest;
        return result;
    }

    const qint64 entry_id = params.value("entry_id").toInteger(-1);
    const QString name = params.value("name").toString().trimmed();
    const QString group = params.value("group").toString().trimmed();

    // Find the user
    QVector<User> users = db.userList();
    int idx = -1;
    for (int i = 0; i < users.size(); ++i)
    {
        if (entry_id > 0 && users[i].entry_id == entry_id)
        {
            idx = i;
            break;
        }
        if (entry_id <= 0 && !name.isEmpty() && users[i].name.compare(name, Qt::CaseInsensitive) == 0)
        {
            idx = i;
            break;
        }
    }
    if (idx < 0)
    {
        result.error_code = kNotFound;
        return result;
    }

    User& target = users[idx];

    // Guard: names starting with # are one-time users, managed by RouterManager
    if (target.name.startsWith('#'))
    {
        result.error_code = kInvalidData;
        return result;
    }

    // Guard: active sessions on this username (all sessions of the account).
    const bool force = params.value("force").toBool(false);
    if (has_active_sessions && has_active_sessions(target.name) && !force)
    {
        result.error_code = kUserHasSessions;
        return result;
    }


    // Guard: disabling the last enabled user
    if (params.contains("flags"))
    {
        const quint32 new_flags = static_cast<quint32>(params.value("flags").toInteger());
        if (!(new_flags & User::ENABLED) && (target.flags & User::ENABLED))
        {
            int enabled_count = 0;
            for (const User& u : users)
            {
                if (u.entry_id != target.entry_id && (u.flags & User::ENABLED))
                    ++enabled_count;
            }
            if (enabled_count == 0)
            {
                result.error_code = kLastEnabledUser;
                return result;
            }
        }
    }

    // Apply changes
    if (params.contains("name"))
    {
        const QString new_name = params.value("name").toString().trimmed();
        if (new_name.isEmpty() || new_name.startsWith('#'))
        {
            result.error_code = kInvalidData;
            return result;
        }
        // Duplicate name check
        for (const User& u : users)
        {
            if (u.entry_id != target.entry_id && u.name.compare(new_name, Qt::CaseInsensitive) == 0)
            {
                result.error_code = kAlreadyExists;
                return result;
            }
        }
        target.name = new_name;
    }

    if (params.contains("group"))
    {
        const QString g = params.value("group").toString().trimmed();
        if (!validateGroup(g))
        {
            result.error_code = kInvalidData;
            return result;
        }
        target.group = g;
    }

    if (params.contains("salt"))
    {
        const QString salt_hex = params.value("salt").toString().trimmed().toLower();
        const QByteArray salt = QByteArray::fromHex(salt_hex.toLatin1());
        if (salt.size() != 64 || salt.toHex() != salt_hex.toLatin1())
        {
            result.error_code = kInvalidData;
            return result;
        }
        target.salt = salt;
    }

    if (params.contains("verifier"))
    {
        const QString ver_hex = params.value("verifier").toString().trimmed().toLower();
        const QByteArray verifier = QByteArray::fromHex(ver_hex.toLatin1());
        if (verifier.isEmpty() || verifier.size() > 1024 || verifier.toHex() != ver_hex.toLatin1())
        {
            result.error_code = kInvalidData;
            return result;
        }
        target.verifier = verifier;
    }

    if (params.contains("sessions"))
    {
        const quint32 mask = static_cast<quint32>(params.value("sessions").toInteger());
        if (!validateSessionMask(mask))
        {
            result.error_code = kInvalidData;
            return result;
        }
        target.sessions = mask;
    }

    if (params.contains("flags"))
    {
        target.flags = static_cast<quint32>(params.value("flags").toInteger());
    }

    if (!db.modifyUser(target))
    {
        result.error_code = kInvalidData;
        return result;
    }

    result.applied = QJsonObject{
        {"entry_id", target.entry_id},
        {"name", target.name},
        {"group", target.group},
        {"sessions", static_cast<qint64>(target.sessions)},
        {"flags", static_cast<qint64>(target.flags)},
    };
    result.error_code = kOk;
    return result;
}

//--------------------------------------------------------------------------------------------------
HostConfigApplier::Result HostConfigApplier::applyRemoveUser(const QJsonObject& params, const std::string& /*initiator*/,
                                                           const std::function<bool(const QString&)>& has_active_sessions)
{
    Result result;
    Database& db = Database::instance();
    if (!db.isValid())
    {
        result.error_code = kInvalidRequest;
        return result;
    }

    const qint64 entry_id = params.value("entry_id").toInteger(-1);
    const QString name = params.value("name").toString().trimmed();

    QVector<User> users = db.userList();
    int idx = -1;
    for (int i = 0; i < users.size(); ++i)
    {
        if (entry_id > 0 && users[i].entry_id == entry_id)
        {
            idx = i;
            break;
        }
        if (entry_id <= 0 && !name.isEmpty() && users[i].name.compare(name, Qt::CaseInsensitive) == 0)
        {
            idx = i;
            break;
        }
    }
    if (idx < 0)
    {
        result.error_code = kNotFound;
        return result;
    }

    User& target = users[idx];

    if (target.name.startsWith('#'))
    {
        result.error_code = kInvalidData;
        return result;
    }

    // Guard: active sessions on this username (all sessions of the account).
    const bool force = params.value("force").toBool(false);
    if (has_active_sessions && has_active_sessions(target.name) && !force)
    {
        result.error_code = kUserHasSessions;
        return result;
    }


    // Guard: last enabled user
    int enabled_count = 0;
    for (const User& u : users)
    {
        if (u.entry_id != target.entry_id && (u.flags & User::ENABLED))
            ++enabled_count;
    }
    if ((target.flags & User::ENABLED) && enabled_count == 0)
    {
        result.error_code = kLastEnabledUser;
        return result;
    }

    if (!db.removeUser(target.entry_id))
    {
        result.error_code = kInvalidData;
        return result;
    }

    result.applied = QJsonObject{{"entry_id", target.entry_id}, {"name", target.name}};
    result.error_code = kOk;
    return result;
}

//--------------------------------------------------------------------------------------------------
HostConfigApplier::Result HostConfigApplier::applyGetHostUsers(const QJsonObject& /*params*/, const std::string& /*initiator*/)
{
    Result result;
    Database& db = Database::instance();
    if (!db.isValid())
    {
        result.error_code = kInvalidRequest;
        return result;
    }

    QJsonArray arr;
    for (const User& u : db.userList())
    {
        QJsonObject o;
        o["entry_id"] = u.entry_id;
        o["name"] = u.name;
        o["group"] = u.group;
        o["sessions"] = static_cast<qint64>(u.sessions);
        o["flags"] = static_cast<qint64>(u.flags);
        arr.append(o);
    }
    result.result = QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
    result.error_code = kOk;
    return result;
}

//--------------------------------------------------------------------------------------------------
HostConfigApplier::Result HostConfigApplier::applySetAccessConfig(const QJsonObject& params, const std::string& /*initiator*/)
{
    Result result;
    Database& db = Database::instance();
    if (!db.isValid())
    {
        result.error_code = kInvalidRequest;
        return result;
    }

    if (params.contains("connect_confirmation"))
        db.setConnectConfirmation(params.value("connect_confirmation").toBool());
    if (params.contains("no_user_action"))
        db.setNoUserAction(static_cast<Database::NoUserAction>(params.value("no_user_action").toInt()));
    if (params.contains("auto_confirmation_interval"))
        db.setAutoConfirmationInterval(MilliSeconds(params.value("auto_confirmation_interval").toInteger()));

    result.applied = params;
    result.error_code = kOk;
    return result;
}

//--------------------------------------------------------------------------------------------------
HostConfigApplier::Result HostConfigApplier::applySetOnetimeConfig(const QJsonObject& params, const std::string& /*initiator*/)
{
    Result result;
    Database& db = Database::instance();
    if (!db.isValid())
    {
        result.error_code = kInvalidRequest;
        return result;
    }

    if (params.contains("one_time_password"))
        db.setOneTimePassword(params.value("one_time_password").toBool());
    if (params.contains("one_time_password_expire"))
        db.setOneTimePasswordExpire(qMin(MilliSeconds(params.value("one_time_password_expire").toInteger()), MilliSeconds(Hours(12))));
    if (params.contains("one_time_password_length"))
        db.setOneTimePasswordLength(qMax(params.value("one_time_password_length").toInt(), 8));
    if (params.contains("one_time_password_characters"))
        db.setOneTimePasswordCharacters(static_cast<quint32>(params.value("one_time_password_characters").toInteger()));

    result.applied = params;
    result.error_code = kOk;
    return result;
}

//--------------------------------------------------------------------------------------------------
HostConfigApplier::Result HostConfigApplier::applyReplaceUsers(const QJsonObject& params, const std::string& /*initiator*/)
{
    Result result;
    Database& db = Database::instance();
    if (!db.isValid())
    {
        result.error_code = kInvalidRequest;
        return result;
    }

    const QJsonArray arr = params.value("users").toArray();
    QVector<User> new_users;
    for (const QJsonValue& v : arr)
    {
        const QJsonObject o = v.toObject();
        User u;
        u.name = o.value("name").toString().trimmed();
        u.group = o.value("group").toString().trimmed();
        u.salt = QByteArray::fromHex(o.value("salt").toString().toLatin1());
        u.verifier = QByteArray::fromHex(o.value("verifier").toString().toLatin1());
        u.sessions = static_cast<quint32>(o.value("sessions").toInteger());
        u.flags = static_cast<quint32>(o.value("flags").toInteger());

        if (!u.isValid() || u.name.isEmpty() || u.name.startsWith('#') ||
            !validateGroup(u.group) || !validateSessionMask(u.sessions))
        {
            result.error_code = kInvalidData;
            return result;
        }
        new_users.append(u);
    }

    // Guard: at least one enabled user in the final set
    int enabled = 0;
    for (const User& u : new_users)
        if (u.flags & User::ENABLED) ++enabled;
    if (enabled == 0)
    {
        result.error_code = kLastEnabledUser;
        return result;
    }

    // Dry-run: compute diff, don't write
    const bool dry_run = params.value("dry_run").toBool(false);
    if (dry_run)
    {
        QJsonObject diff;
        // Simplified diff: counts
        diff["old_count"] = db.userList().size();
        diff["new_count"] = new_users.size();
        result.diff = diff;
        result.error_code = kOk;
        return result;
    }

    if (!db.replaceUsers(new_users))
    {
        result.error_code = kInvalidData;
        return result;
    }

    result.applied = QJsonObject{{"count", static_cast<qint64>(new_users.size())}};
    result.error_code = kOk;
    return result;
}

//--------------------------------------------------------------------------------------------------
HostConfigApplier::Result HostConfigApplier::applySetMisc(const QJsonObject& params, const std::string& /*initiator*/)
{
    Result result;
    Database& db = Database::instance();
    if (!db.isValid())
    {
        result.error_code = kInvalidRequest;
        return result;
    }

    SystemSettings settings;
    if (params.contains("preferred_video_capturer"))
        settings.setPreferredVideoCapturer(static_cast<quint32>(params.value("preferred_video_capturer").toInteger()));
    if (params.contains("application_shutdown_disabled"))
        settings.setApplicationShutdownDisabled(params.value("application_shutdown_disabled").toBool());

    result.applied = params;
    result.error_code = kOk;
    return result;
}

//--------------------------------------------------------------------------------------------------
HostConfigApplier::Result HostConfigApplier::applyFullConfig(const QJsonObject& params, const std::string& /*initiator*/)
{
    Result result;
    Database& db = Database::instance();
    if (!db.isValid())
    {
        result.error_code = kInvalidRequest;
        return result;
    }

    const QJsonArray keep_local = params.value("keep_local").toArray();
    QSet<QString> keep;
    for (const QJsonValue& v : keep_local)
        keep.insert(v.toString());

    const bool dry_run = params.value("dry_run").toBool(false);

    // Snapshot before any write
    result.snapshot = QString::fromUtf8(QJsonDocument(snapshotConfig()).toJson(QJsonDocument::Compact));

    // Validate first (before any write)
    // Users
    if (!keep.contains("users") && params.contains("database") && params["database"].isObject())
    {
        const QJsonObject db_obj = params["database"].toObject();
        if (db_obj.contains("users"))
        {
            const QJsonArray arr = db_obj["users"].toArray();
            int enabled = 0;
            for (const QJsonValue& v : arr)
            {
                const QJsonObject o = v.toObject();
                const QString name = o.value("name").toString().trimmed();
                if (name.isEmpty() || name.startsWith('#'))
                {
                    result.error_code = kInvalidData;
                    return result;
                }
                const QString group = o.value("group").toString().trimmed();
                if (!validateGroup(group))
                {
                    result.error_code = kInvalidData;
                    return result;
                }
                const quint32 sessions = static_cast<quint32>(o.value("sessions").toInteger());
                if (!validateSessionMask(sessions))
                {
                    result.error_code = kInvalidData;
                    return result;
                }
                const quint32 flags = static_cast<quint32>(o.value("flags").toInteger());
                if (flags & ~User::ENABLED)
                {
                    result.error_code = kInvalidData;
                    return result;
                }
                if (flags & User::ENABLED)
                    ++enabled;
            }
            if (!arr.isEmpty() && enabled == 0)
            {
                result.error_code = kLastEnabledUser;
                return result;
            }
        }
    }

    if (dry_run)
    {
        // Compute diff
        QJsonObject diff;
        // System settings diff
        if (!keep.contains("system") && params.contains("system") && params["system"].isObject())
        {
            const QJsonObject sys = params["system"].toObject();
            SystemSettings settings;
            QJsonObject sys_diff;
            if (sys.contains("update_server") && sys["update_server"].toString() != settings.updateServer())
                sys_diff["update_server"] = QJsonObject{{"old", settings.updateServer()}, {"new", sys["update_server"].toString()}};
            if (sys.contains("auto_update_enabled") && sys["auto_update_enabled"].toBool() != settings.isAutoUpdateEnabled())
                sys_diff["auto_update_enabled"] = QJsonObject{{"old", settings.isAutoUpdateEnabled()}, {"new", sys["auto_update_enabled"].toBool()}};
            if (sys.contains("update_channel") && sys["update_channel"].toString() != settings.updateChannel())
                sys_diff["update_channel"] = QJsonObject{{"old", settings.updateChannel()}, {"new", sys["update_channel"].toString()}};
            if (sys.contains("update_check_frequency") && sys["update_check_frequency"].toInt() != settings.updateCheckFrequency())
                sys_diff["update_check_frequency"] = QJsonObject{{"old", settings.updateCheckFrequency()}, {"new", sys["update_check_frequency"].toInt()}};
            if (sys.contains("preferred_video_capturer") && sys["preferred_video_capturer"].toInteger() != settings.preferredVideoCapturer())
                sys_diff["preferred_video_capturer"] = QJsonObject{{"old", static_cast<qint64>(settings.preferredVideoCapturer())}, {"new", sys["preferred_video_capturer"].toInteger()}};
            if (sys.contains("application_shutdown_disabled") && sys["application_shutdown_disabled"].toBool() != settings.isApplicationShutdownDisabled())
                sys_diff["application_shutdown_disabled"] = QJsonObject{{"old", settings.isApplicationShutdownDisabled()}, {"new", sys["application_shutdown_disabled"].toBool()}};
            if (!sys_diff.isEmpty())
                diff["system"] = sys_diff;
        }
        // Users diff (counts only for simplicity)
        if (!keep.contains("users") && params.contains("database") && params["database"].isObject())
        {
            const QJsonObject db_obj = params["database"].toObject();
            if (db_obj.contains("users"))
            {
                const int old_count = db.userList().size();
                const int new_count = db_obj["users"].toArray().size();
                diff["users"] = QJsonObject{{"old_count", old_count}, {"new_count", new_count}};
            }
        }
        result.diff = diff;
        result.error_code = kOk;
        return result;
    }

    // Apply: system settings
    if (!keep.contains("system") && params.contains("system") && params["system"].isObject())
    {
        const QJsonObject sys = params["system"].toObject();
        SystemSettings settings;
        if (sys.contains("update_channel"))
            settings.setUpdateChannel(sys["update_channel"].toString());
        if (sys.contains("preferred_video_capturer"))
            settings.setPreferredVideoCapturer(static_cast<quint32>(sys["preferred_video_capturer"].toInteger()));
        if (sys.contains("application_shutdown_disabled"))
            settings.setApplicationShutdownDisabled(sys["application_shutdown_disabled"].toBool());
        if (sys.contains("auto_update_enabled"))
            settings.setAutoUpdateEnabled(sys["auto_update_enabled"].toBool());
        if (sys.contains("update_check_frequency"))
            settings.setUpdateCheckFrequency(sys["update_check_frequency"].toInt());
        if (sys.contains("update_server"))
        {
            const QString server = sys["update_server"].toString().trimmed();
            const QByteArray key = QByteArray::fromHex(sys["update_public_key"].toString().trimmed().toLatin1());
            if (validateUrl(server) && (sys["update_public_key"].toString().isEmpty() || (key.size() == 32 && key.toHex() == sys["update_public_key"].toString().trimmed().toLower().toLatin1())))
            {
                settings.setUpdateServer(server);
                if (!sys["update_public_key"].toString().isEmpty())
                    settings.setUpdatePublicKey(key);
            }
        }
        settings.sync();
    }

    // Apply: database settings (except users)
    if (!keep.contains("database") && params.contains("database") && params["database"].isObject())
    {
        const QJsonObject db_obj = params["database"].toObject();
        Database& db = Database::instance();
        if (db_obj.contains("tcp_port"))
            db.setTcpPort(static_cast<quint16>(db_obj["tcp_port"].toInt()));
        if (db_obj.contains("router_enabled"))
            db.setRouterEnabled(db_obj["router_enabled"].toBool());
        if (db_obj.contains("router_address"))
            db.setRouterAddress(Address::fromString(db_obj["router_address"].toString(), kDefaultRouterHostTcpPort));
        if (db_obj.contains("router_public_key"))
            db.setRouterPublicKey(QByteArray::fromHex(db_obj["router_public_key"].toString().toLatin1()));
        if (db_obj.contains("connect_confirmation"))
            db.setConnectConfirmation(db_obj["connect_confirmation"].toBool());
        if (db_obj.contains("no_user_action"))
            db.setNoUserAction(static_cast<Database::NoUserAction>(db_obj["no_user_action"].toInt()));
        if (db_obj.contains("auto_confirmation_interval"))
            db.setAutoConfirmationInterval(MilliSeconds(db_obj["auto_confirmation_interval"].toInteger()));
        if (db_obj.contains("one_time_password"))
            db.setOneTimePassword(db_obj["one_time_password"].toBool());
        if (db_obj.contains("one_time_password_expire"))
            db.setOneTimePasswordExpire(qMin(MilliSeconds(db_obj["one_time_password_expire"].toInteger()), MilliSeconds(Hours(12))));
        if (db_obj.contains("one_time_password_length"))
            db.setOneTimePasswordLength(qMax(db_obj["one_time_password_length"].toInt(), 8));
        if (db_obj.contains("one_time_password_characters"))
            db.setOneTimePasswordCharacters(static_cast<quint32>(db_obj["one_time_password_characters"].toInteger()));
    }

    // Apply: users
    if (!keep.contains("users") && params.contains("database") && params["database"].isObject())
    {
        const QJsonObject db_obj = params["database"].toObject();
        if (db_obj.contains("users"))
        {
            const QJsonArray arr = db_obj["users"].toArray();
            QVector<User> new_users;
            for (const QJsonValue& v : arr)
            {
                const QJsonObject o = v.toObject();
                User u;
                u.name = o.value("name").toString().trimmed();
                u.group = o.value("group").toString().trimmed();
                u.salt = QByteArray::fromHex(o.value("salt").toString().toLatin1());
                u.verifier = QByteArray::fromHex(o.value("verifier").toString().toLatin1());
                u.sessions = static_cast<quint32>(o.value("sessions").toInteger());
                u.flags = static_cast<quint32>(o.value("flags").toInteger());

                if (!u.isValid() || u.name.isEmpty() || u.name.startsWith('#') ||
                    !validateGroup(u.group) || !validateSessionMask(u.sessions))
                {
                    result.error_code = kInvalidData;
                    return result;
                }
                new_users.append(u);
            }

            // Guard: at least one enabled user
            int enabled = 0;
            for (const User& u : new_users)
                if (u.flags & User::ENABLED) ++enabled;
            if (!new_users.isEmpty() && enabled == 0)
            {
                result.error_code = kLastEnabledUser;
                return result;
            }

            if (!Database::instance().replaceUsers(new_users))
            {
                result.error_code = kInvalidData;
                return result;
            }
        }
    }

    result.applied = QJsonObject{{"status", "applied"}};
    result.error_code = kOk;
    return result;
}

