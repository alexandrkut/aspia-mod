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

#include "router/storage/database.h"

#include <gtest/gtest.h>

#include <QTemporaryDir>
#include <QUuid>

#include "base/crypto/secure_string.h"
#include "base/peer/router_user.h"
#include "base/peer/user.h"
#include "base/sql/sql_database.h"
#include "base/sql/sql_query.h"
#include "proto/router.h"
#include "proto/router_constants.h"
#include "router/storage/host_store.h"
#include "router/storage/user_store.h"
#include "router/storage/workspace_store.h"

namespace {

constexpr quint32 kAllSessions = proto::router::SESSION_TYPE_ADMIN |
    proto::router::SESSION_TYPE_MANAGER | proto::router::SESSION_TYPE_OPERATOR;

//--------------------------------------------------------------------------------------------------
RouterUser makeUser(const QString& name, quint32 sessions)
{
    RouterUser user = RouterUser::create(name, SecureString("Password1234!"));
    user.sessions = sessions;
    user.flags = User::ENABLED;
    return user;
}

} // namespace

// Every test opens an isolated database in a temporary directory and starts from the state
// --create-config leaves behind: the built-in administrator with id 1.
class RouterDatabaseTest : public testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(temp_dir_.isValid());
        file_path_ = temp_dir_.path() + "/router.db3";
        ASSERT_TRUE(db_.open(file_path_));

        UserStore store(db_);

        admin_ = makeUser("admin", kAllSessions);
        ASSERT_TRUE(admin_.isValid());
        ASSERT_EQ(store.add(admin_), proto::router::kErrorOk);

        ASSERT_EQ(store.find("admin", &admin_), proto::router::kErrorOk);
        ASSERT_EQ(admin_.entry_id, 1);
    }

    // Doctors rows the public API deliberately cannot produce (legacy states, induced holes).
    bool execRaw(const QString& sql)
    {
        SqlDatabase raw;
        if (!raw.open(file_path_))
            return false;
        return raw.exec(sql.toStdString().c_str());
    }

    QTemporaryDir temp_dir_;
    QString file_path_;
    Database db_;
    RouterUser admin_;
};

//--------------------------------------------------------------------------------------------------
// The users table of 3.4.2 has no guid column. Opening such a database adds it and gives every
// user a guid of its own.
TEST_F(RouterDatabaseTest, UsersTableOf342GainsTheGuidColumn)
{
    ASSERT_EQ(UserStore(db_).add(makeUser("bob", proto::router::SESSION_TYPE_OPERATOR)), proto::router::kErrorOk);
    ASSERT_TRUE(execRaw("ALTER TABLE users DROP COLUMN guid"));
    ASSERT_TRUE(execRaw("PRAGMA user_version = 0"));

    Database reopened;
    ASSERT_TRUE(reopened.open(file_path_));

    UserStore reopened_store(reopened);

    RouterUser admin;
    ASSERT_EQ(reopened_store.find(admin_.entry_id, &admin), proto::router::kErrorOk);
    EXPECT_FALSE(QUuid::fromString(admin.guid).isNull());

    RouterUser bob;
    ASSERT_EQ(reopened_store.find("bob", &bob), proto::router::kErrorOk);
    EXPECT_FALSE(QUuid::fromString(bob.guid).isNull());
    EXPECT_NE(bob.guid, admin.guid);

    Database opened_again;
    ASSERT_TRUE(opened_again.open(file_path_));

    RouterUser unchanged;
    ASSERT_EQ(UserStore(opened_again).find("bob", &unchanged), proto::router::kErrorOk);
    EXPECT_EQ(unchanged.guid, bob.guid);

    SqlDatabase raw;
    ASSERT_TRUE(raw.open(file_path_));

    SqlQuery version(raw, "PRAGMA user_version");
    ASSERT_EQ(version.next(), SqlQuery::StepResult::ROW);
    EXPECT_EQ(version.columnInt64(0), 1);
}
//--------------------------------------------------------------------------------------------------
// A regular account is the default: a client that does not send a type still gets an ordinary
// router account, and a new user inherits the column default of zero.
TEST_F(RouterDatabaseTest, UsersTypeDefaultsToRegular)
{
    UserStore store(db_);

    RouterUser admin;
    ASSERT_EQ(store.find("admin", &admin), proto::router::kErrorOk);
    EXPECT_EQ(admin.user_type, proto::router::USER_TYPE_REGULAR);

    ASSERT_EQ(store.add(makeUser("bob", proto::router::SESSION_TYPE_OPERATOR)), proto::router::kErrorOk);
    RouterUser bob;
    ASSERT_EQ(store.find("bob", &bob), proto::router::kErrorOk);
    EXPECT_EQ(bob.user_type, proto::router::USER_TYPE_REGULAR);
}

//--------------------------------------------------------------------------------------------------
// Accounts exported by older builds carry bit 32 in the session mask. Opening such a database
// once lifts that marker into the type column and clears the bit, so no reader has to know it.
TEST(RouterDatabaseLegacyTest, QuickSupportBitBecomesTheAccountType)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString file_path = dir.path() + "/router.db3";

    {
        SqlDatabase raw;
        ASSERT_TRUE(raw.open(file_path));
        ASSERT_TRUE(raw.exec(
            "CREATE TABLE \"users\" ("
            "\"id\" INTEGER NOT NULL,"
            "\"name\" TEXT NOT NULL UNIQUE,"
            "\"group\" TEXT NOT NULL DEFAULT '',"
            "\"salt\" BLOB NOT NULL,"
            "\"verifier\" BLOB NOT NULL,"
            "\"sessions\" INTEGER NOT NULL DEFAULT 0,"
            "\"flags\" INTEGER NOT NULL DEFAULT 0,"
            "\"otp_secret\" BLOB NOT NULL DEFAULT X'',"
            "\"otp_counter\" INTEGER NOT NULL DEFAULT 0,"
            "\"email\" TEXT NOT NULL DEFAULT '',"
            "PRIMARY KEY(\"id\" AUTOINCREMENT))"));
        ASSERT_TRUE(raw.exec(
            "INSERT INTO users (name, \"group\", salt, verifier, sessions, flags, email) "
            "VALUES ('legacy-qs', '', X'01', X'02', 34, 1, '')"));
    }

    Database db;
    ASSERT_TRUE(db.open(file_path));

    RouterUser user;
    ASSERT_EQ(UserStore(db).find(QStringLiteral("legacy-qs"), &user), proto::router::kErrorOk);
    EXPECT_EQ(user.user_type, proto::router::USER_TYPE_QUICK_SUPPORT);
    EXPECT_EQ(user.sessions, proto::router::SESSION_TYPE_OPERATOR);
}

//--------------------------------------------------------------------------------------------------
// A host of the single-membership era has only the scalar columns. Opening the database seeds
// host_workspaces from them, and a second open must not resurrect a membership that was removed.
TEST_F(RouterDatabaseTest, MigrationSeedsMembershipsOnce)
{
    WorkspaceStore workspaces(db_);
    HostStore hosts(db_);

    qint64 workspace_id = -1;
    ASSERT_EQ(workspaces.add("alpha", std::string_view(), {admin_.entry_id}, &workspace_id),
              proto::router::kErrorOk);
    ASSERT_GT(workspace_id, 0);

    ASSERT_TRUE(hosts.add("hash-1", "hwid"));
    HostId host_id = kInvalidHostId;
    ASSERT_EQ(hosts.findByKey("hash-1", &host_id), proto::router::kErrorOk);
    ASSERT_TRUE(execRaw(QString("UPDATE hosts SET workspace_id=%1, group_id=0 WHERE id=%2")
                            .arg(workspace_id)
                            .arg(host_id)));

    Database reopened;
    ASSERT_TRUE(reopened.open(file_path_));
    HostStore reopened_hosts(reopened);
    std::vector<qint64> seeded = reopened_hosts.workspaceIds(host_id);
    ASSERT_EQ(seeded.size(), 1);
    EXPECT_EQ(seeded[0], workspace_id);

    // A host of the single-membership era that appears after the first migration must also be
    // backfilled: the guard is per host, not "the membership table is empty".
    ASSERT_TRUE(reopened_hosts.add("hash-2", "hwid"));
    HostId late_host_id = kInvalidHostId;
    ASSERT_EQ(reopened_hosts.findByKey("hash-2", &late_host_id), proto::router::kErrorOk);
    ASSERT_TRUE(execRaw(QString("UPDATE hosts SET workspace_id=%1, group_id=0 WHERE id=%2")
                            .arg(workspace_id)
                            .arg(late_host_id)));

    Database reopened_late;
    ASSERT_TRUE(reopened_late.open(file_path_));
    HostStore late_hosts(reopened_late);
    EXPECT_EQ(late_hosts.workspaceIds(late_host_id), std::vector<qint64>{ workspace_id });
    EXPECT_EQ(late_hosts.workspaceIds(host_id), std::vector<qint64>{ workspace_id });

    // A second open leaves the seeded row as it is: a membership already present is never
    // duplicated by the scalar fallback.
    Database opened_again;
    ASSERT_TRUE(opened_again.open(file_path_));
    EXPECT_EQ(HostStore(opened_again).workspaceIds(host_id), std::vector<qint64>{ workspace_id });
    EXPECT_EQ(HostStore(opened_again).workspaceIds(late_host_id),
              std::vector<qint64>{ workspace_id });
}
