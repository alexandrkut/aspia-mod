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

#include "router/client_admin.h"

#include <QJsonDocument>
#include <QJsonObject>

#include <gtest/gtest.h>

namespace {

QJsonObject parse(const std::string& params)
{
    const QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(params));
    EXPECT_TRUE(doc.isObject());
    return doc.object();
}

} // namespace

//--------------------------------------------------------------------------------------------------
// The router stamps the name of the authenticated session. A client that claims to be somebody
// else must not be able to put its own name in the host's audit trail.
TEST(ClientAdminTest, InitiatorReplacesTheClientSuppliedValue)
{
    const QJsonObject object = parse(ClientAdmin::paramsWithInitiator(
        R"({"initiator":"spoofed","channel":"beta"})", "real-admin"));

    EXPECT_EQ(object.value("initiator").toString(), QStringLiteral("real-admin"));
    EXPECT_EQ(object.value("channel").toString(), QStringLiteral("beta"));
}

//--------------------------------------------------------------------------------------------------
// A command with no params at all still carries the author of the change.
TEST(ClientAdminTest, InitiatorIsAddedToEmptyParams)
{
    const QJsonObject object = parse(ClientAdmin::paramsWithInitiator("", "admin-2"));

    EXPECT_EQ(object.value("initiator").toString(), QStringLiteral("admin-2"));
}

//--------------------------------------------------------------------------------------------------
// Garbage in the params field is not a reason to lose the author of the command.
TEST(ClientAdminTest, InitiatorIsAddedToInvalidJson)
{
    const QJsonObject object = parse(ClientAdmin::paramsWithInitiator("not json", "admin-3"));

    EXPECT_EQ(object.value("initiator").toString(), QStringLiteral("admin-3"));
}
