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

#include "router/settings.h"

#include "proto/router.h"

#include <QFile>
#include <QTemporaryDir>

#include <gtest/gtest.h>

class RouterSettingsTest : public testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(temp_dir_.isValid());
        file_path_ = temp_dir_.path() + "/router.conf";
        qputenv("ASPIA_ROUTER_CONFIG_FILE", file_path_.toLocal8Bit());
    }

    void TearDown() override
    {
        qunsetenv("ASPIA_ROUTER_CONFIG_FILE");
    }

    void writeConfig(const QByteArray& content)
    {
        QFile file(file_path_);
        ASSERT_TRUE(file.open(QFile::WriteOnly));
        ASSERT_EQ(file.write(content), content.size());
    }

    QTemporaryDir temp_dir_;
    QString file_path_;
};

//--------------------------------------------------------------------------------------------------
// A configuration written before the access log existed keeps the entries for the default period.
TEST_F(RouterSettingsTest, AccessLogDaysDefaultToFortyFive)
{
    EXPECT_EQ(Settings().accessLogDays(), 45);
}

//--------------------------------------------------------------------------------------------------
TEST_F(RouterSettingsTest, AccessLogDaysReadBack)
{
    {
        Settings settings;
        settings.setAccessLogDays(30);
        ASSERT_TRUE(settings.sync());
    }

    EXPECT_EQ(Settings().accessLogDays(), 30);
}

//--------------------------------------------------------------------------------------------------
// --create-config writes the key with its default, so an administrator finds it in the file.
TEST_F(RouterSettingsTest, ResetWritesTheDefaultAccessLogDays)
{
    {
        Settings settings;
        settings.setAccessLogDays(30);
        settings.reset();
        ASSERT_TRUE(settings.sync());
    }

    EXPECT_EQ(Settings().accessLogDays(), 45);

    QFile file(file_path_);
    ASSERT_TRUE(file.open(QFile::ReadOnly));
    EXPECT_TRUE(file.readAll().contains("access_log_days"));
}

//--------------------------------------------------------------------------------------------------
// A wrong number in the file does not stop the router. It is taken as the nearest bound of the range.
TEST_F(RouterSettingsTest, AccessLogDaysOutOfRangeAreClamped)
{
    const struct { const char* value; quint16 days; } kCases[] = {
        { "0", 1 },
        { "-5", 1 },
        { "1", 1 },
        { "365", 365 },
        { "366", 365 },
        { "100000", 365 }
    };

    for (const auto& test_case : kCases)
    {
        writeConfig(QByteArray("[router]\naccess_log_days=") + test_case.value + "\n");
        EXPECT_EQ(Settings().accessLogDays(), test_case.days) << test_case.value;
    }
}

//--------------------------------------------------------------------------------------------------
TEST_F(RouterSettingsTest, AccessLogDaysThatAreNotANumberAreTheDefault)
{
    writeConfig("[router]\naccess_log_days=abc\n");
    EXPECT_EQ(Settings().accessLogDays(), 45);
}

//--------------------------------------------------------------------------------------------------
// --create-config writes the section with an empty server, so the emails stay off until an
// administrator fills it in.
TEST_F(RouterSettingsTest, ResetWritesTheSmtpDefaults)
{
    {
        Settings settings;
        settings.reset();
        ASSERT_TRUE(settings.sync());
    }

    Settings settings;
    EXPECT_TRUE(settings.smtpServer().isEmpty());
    EXPECT_EQ(settings.smtpPort(), 587);
    EXPECT_EQ(settings.smtpSecurity(), Settings::SmtpSecurity::STARTTLS);

    QFile file(file_path_);
    ASSERT_TRUE(file.open(QFile::ReadOnly));
    EXPECT_TRUE(file.readAll().contains("[smtp]"));
}

//--------------------------------------------------------------------------------------------------
TEST_F(RouterSettingsTest, SmtpSecurityReadBack)
{
    const struct { const char* value; Settings::SmtpSecurity security; } kCases[] = {
        { "none", Settings::SmtpSecurity::NONE },
        { "starttls", Settings::SmtpSecurity::STARTTLS },
        { "tls", Settings::SmtpSecurity::TLS },
        { "ssl", Settings::SmtpSecurity::STARTTLS },
        { "", Settings::SmtpSecurity::STARTTLS }
    };

    for (const auto& test_case : kCases)
    {
        writeConfig(QByteArray("[smtp]\nsecurity=") + test_case.value + "\n");
        EXPECT_EQ(Settings().smtpSecurity(), test_case.security) << test_case.value;
    }
}

TEST_F(RouterSettingsTest, RolesAreProtectedWhenTheFileSaysNothing)
{
    writeConfig("[router]\n");

    const Settings settings;

    EXPECT_TRUE(settings.isTwoFactorForAdminEnabled());
    EXPECT_TRUE(settings.isTwoFactorForManagerEnabled());
    EXPECT_TRUE(settings.isTwoFactorForOperatorEnabled());
}

//--------------------------------------------------------------------------------------------------
// A file that does not exist at all is the state of a fresh installation, and it must not be read
// as a permission to let anybody in without a code.
TEST_F(RouterSettingsTest, RolesAreProtectedWhenThereIsNoFileAtAll)
{
    const Settings settings;

    EXPECT_TRUE(settings.isTwoFactorForAdminEnabled());
    EXPECT_TRUE(settings.isTwoFactorForManagerEnabled());
    EXPECT_TRUE(settings.isTwoFactorForOperatorEnabled());
}

//--------------------------------------------------------------------------------------------------
// The three are independent of each other. One being switched off is the normal case and must not
// carry the other two with it - that is the whole reason for three settings and not one.
TEST_F(RouterSettingsTest, RolesAreSwitchedOffOneByOne)
{
    writeConfig("[router]\ntwo_factor_manager=false\n");

    const Settings settings;

    EXPECT_TRUE(settings.isTwoFactorForAdminEnabled());
    EXPECT_FALSE(settings.isTwoFactorForManagerEnabled());
    EXPECT_TRUE(settings.isTwoFactorForOperatorEnabled());

    writeConfig("[router]\ntwo_factor_admin=false\ntwo_factor_operator=false\n");

    const Settings second;

    EXPECT_FALSE(second.isTwoFactorForAdminEnabled());
    EXPECT_TRUE(second.isTwoFactorForManagerEnabled());
    EXPECT_FALSE(second.isTwoFactorForOperatorEnabled());
}

//--------------------------------------------------------------------------------------------------
// A hand written file spells the value out, while the file the router writes itself uses 1 and 0.
// Both have to mean the same thing, since the first is what an administrator writes and the
// second is what the router puts there.
TEST_F(RouterSettingsTest, ValuesAreReadInBothSpellings)
{
    writeConfig("[router]\ntwo_factor_admin=true\ntwo_factor_manager=0\n"
                "two_factor_operator=false\n");

    const Settings settings;

    EXPECT_TRUE(settings.isTwoFactorForAdminEnabled());
    EXPECT_FALSE(settings.isTwoFactorForManagerEnabled());
    EXPECT_FALSE(settings.isTwoFactorForOperatorEnabled());
}

//--------------------------------------------------------------------------------------------------
// A value that is neither of the two is not a request to let anybody in: a mistyped key, a value
// left half-edited, a file written by something that did not know this setting. The protected
// answer is the only one that cannot be reached by accident.
TEST_F(RouterSettingsTest, UnreadableValueKeepsTheRoleProtected)
{
    writeConfig("[router]\ntwo_factor_admin=yes\ntwo_factor_manager=\ntwo_factor_operator=2\n");

    const Settings settings;

    EXPECT_TRUE(settings.isTwoFactorForAdminEnabled());
    EXPECT_TRUE(settings.isTwoFactorForManagerEnabled());
    EXPECT_TRUE(settings.isTwoFactorForOperatorEnabled());
}

//--------------------------------------------------------------------------------------------------
// The role a session opens as is the value the switch is looked up by, so the three settings and
// the three roles have to meet at the right place.
TEST_F(RouterSettingsTest, TheRoleOfTheSessionDecidesTheValue)
{
    writeConfig("[router]\ntwo_factor_operator=false\n");

    const Settings settings;

    EXPECT_TRUE(settings.isTwoFactorEnabledFor(proto::router::SESSION_TYPE_ADMIN));
    EXPECT_TRUE(settings.isTwoFactorEnabledFor(proto::router::SESSION_TYPE_MANAGER));
    EXPECT_FALSE(settings.isTwoFactorEnabledFor(proto::router::SESSION_TYPE_OPERATOR));
}

//--------------------------------------------------------------------------------------------------
// A role this build does not name keeps its second factor. The settings exist to let one named
// role through, and an unknown one is not a request anybody made.
TEST_F(RouterSettingsTest, UnknownRoleIsProtected)
{
    const Settings settings;

    EXPECT_TRUE(settings.isTwoFactorEnabledFor(proto::router::SESSION_TYPE_UNKNOWN));
    EXPECT_TRUE(settings.isTwoFactorEnabledFor(9999));
}

//--------------------------------------------------------------------------------------------------
// What the router writes has to be what it reads back, otherwise an administrator who switches a
// role off through the settings object ends up with a file that does not do what it says.
TEST_F(RouterSettingsTest, WrittenValuesAreReadBack)
{
    {
        Settings settings;
        settings.setTwoFactorForAdmin(false);
        settings.setTwoFactorForManager(true);
        settings.setTwoFactorForOperator(false);
        ASSERT_TRUE(settings.sync());
    }

    const Settings reread;

    EXPECT_FALSE(reread.isTwoFactorForAdminEnabled());
    EXPECT_TRUE(reread.isTwoFactorForManagerEnabled());
    EXPECT_FALSE(reread.isTwoFactorForOperatorEnabled());
}

//--------------------------------------------------------------------------------------------------
// A fresh configuration carries the protected values, so the file a router writes on its own says
// that the second factor is on rather than leaving the question open.
TEST_F(RouterSettingsTest, FreshConfigurationProtectsEveryRole)
{
    {
        Settings settings;
        settings.reset();
        ASSERT_TRUE(settings.sync());
    }

    const Settings reread;

    EXPECT_TRUE(reread.isTwoFactorForAdminEnabled());
    EXPECT_TRUE(reread.isTwoFactorForManagerEnabled());
    EXPECT_TRUE(reread.isTwoFactorForOperatorEnabled());
}
//--------------------------------------------------------------------------------------------------
// The configuration a router writes on --create-config goes through reset(), so the three keys
// have to come out of it as the protected values rather than be absent - an absent key and a key
// that says false are different things to whoever reads the file afterwards.
TEST_F(RouterSettingsTest, ResetWritesTheThreeKeysIntoTheFile)
{
    {
        Settings settings;
        settings.reset();
        ASSERT_TRUE(settings.sync());
    }

    QFile file(file_path_);
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    const QString written = QString::fromUtf8(file.readAll());

    EXPECT_TRUE(written.contains(QStringLiteral("two_factor_admin")));
    EXPECT_TRUE(written.contains(QStringLiteral("two_factor_manager")));
    EXPECT_TRUE(written.contains(QStringLiteral("two_factor_operator")));

    // A key that says "1" or "0" is what the ini writer produces, and both are read back as the
    // protected value, so a file written by the router is as safe as one that was never touched.
    const Settings reread;
    EXPECT_TRUE(reread.isTwoFactorForAdminEnabled());
    EXPECT_TRUE(reread.isTwoFactorForManagerEnabled());
    EXPECT_TRUE(reread.isTwoFactorForOperatorEnabled());
}
