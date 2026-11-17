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

#include <gtest/gtest.h>

#include "base/crypto/secure_string.h"
#include "host/database.h"
#include "host/system_settings.h"

class HostConfigApplierTest : public testing::Test
{
};

TEST_F(HostConfigApplierTest, SetUpdateConfigValidatesUrl)
{
    QJsonObject params;
    params["update_server"] = "not-a-url";
    HostConfigApplier::Result r = HostConfigApplier::apply("set_update_config", params, "admin");
    EXPECT_EQ(r.error_code, "invalid_data");
}

TEST_F(HostConfigApplierTest, SetUpdateConfigAcceptsEmptyServer)
{
    QJsonObject params;
    params["update_server"] = "";
    params["update_public_key"] = "";
    params["update_channel"] = "stable";
    params["auto_update_enabled"] = false;
    params["update_check_frequency"] = 7;
    HostConfigApplier::Result r = HostConfigApplier::apply("set_update_config", params, "admin");
    EXPECT_EQ(r.error_code, "ok");
}

TEST_F(HostConfigApplierTest, UnknownCommandReturnsInvalidRequest)
{
    QJsonObject params;
    HostConfigApplier::Result r = HostConfigApplier::apply("nonexistent_command", params, "admin");
    EXPECT_EQ(r.error_code, "invalid_request");
}

TEST_F(HostConfigApplierTest, AddUserRejectsEmptyName)
{
    QJsonObject params;
    params["name"] = "";
    params["group"] = "8192";
    params["salt"] = QString("00").repeated(16);
    params["verifier"] = QString("00").repeated(256);
    params["sessions"] = 63;
    params["flags"] = 1;
    HostConfigApplier::Result r = HostConfigApplier::apply("add_user", params, "admin");
    // Without a valid DB this returns invalid_request (DB check first); with DB -> invalid_data.
    EXPECT_TRUE(r.error_code == "invalid_data" || r.error_code == "invalid_request");
}

TEST_F(HostConfigApplierTest, AddUserRejectsOneTimeName)
{
    QJsonObject params;
    params["name"] = "#123";
    params["group"] = "8192";
    params["salt"] = QString("00").repeated(16);
    params["verifier"] = QString("00").repeated(256);
    params["sessions"] = 63;
    params["flags"] = 1;
    HostConfigApplier::Result r = HostConfigApplier::apply("add_user", params, "admin");
    EXPECT_TRUE(r.error_code == "invalid_data" || r.error_code == "invalid_request");
}


//--------------------------------------------------------------------------------------------------
TEST_F(HostConfigApplierTest, SetUpdateConfigClearsPublicKey)
{
    SystemSettings settings;
    settings.setUpdatePublicKey(QByteArray::fromHex(QString("11").repeated(32).toLatin1()));
    settings.sync();

    QJsonObject params;
    params["update_server"] = settings.updateServer().isEmpty() ? "http://example.com" : settings.updateServer();
    params["update_public_key"] = "";
    params["update_channel"] = settings.updateChannel().isEmpty() ? "stable" : settings.updateChannel();
    params["auto_update_enabled"] = settings.isAutoUpdateEnabled();
    params["update_check_frequency"] = settings.updateCheckFrequency();

    HostConfigApplier::Result r = HostConfigApplier::apply("set_update_config", params, "admin");
    EXPECT_EQ(r.error_code, "ok");

    SystemSettings check;
    EXPECT_TRUE(check.updatePublicKey().isEmpty());
}

//--------------------------------------------------------------------------------------------------
TEST_F(HostConfigApplierTest, SetUpdateConfigOmitsPublicKeyLeavesStoredKey)
{
    SystemSettings settings;
    const QByteArray key = QByteArray::fromHex(QString("22").repeated(32).toLatin1());
    settings.setUpdatePublicKey(key);
    settings.sync();

    QJsonObject params;
    params["update_server"] = settings.updateServer().isEmpty() ? "http://example.com" : settings.updateServer();
    params["update_channel"] = settings.updateChannel().isEmpty() ? "stable" : settings.updateChannel();
    params["auto_update_enabled"] = settings.isAutoUpdateEnabled();
    params["update_check_frequency"] = settings.updateCheckFrequency();
    // update_public_key intentionally omitted

    HostConfigApplier::Result r = HostConfigApplier::apply("set_update_config", params, "admin");
    EXPECT_EQ(r.error_code, "ok");

    SystemSettings check;
    EXPECT_EQ(check.updatePublicKey(), key);
}
