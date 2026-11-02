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

#include "client/connect_request.h"

#include <gtest/gtest.h>

#include "proto/peer.h"

namespace {

ConnectRequest parse(const QStringList& rest, QString* url = nullptr, QString* error = nullptr)
{
    ConnectRequest request;
    QString parsed_url;
    QString parsed_error;

    EXPECT_TRUE(ConnectRequest::parse(rest, &request, &parsed_url, &parsed_error))
        << parsed_error.toStdString();

    if (url)
        *url = parsed_url;

    if (error)
        *error = parsed_error;

    return request;
}

bool fails(const QStringList& rest)
{
    ConnectRequest request;
    QString url;
    QString error;

    if (ConnectRequest::parse(rest, &request, &url, &error))
        return false;

    // A refusal always says why.
    EXPECT_FALSE(error.isEmpty());
    return true;
}

} // namespace

//--------------------------------------------------------------------------------------------------
// Nothing on the command line is no request at all.
TEST(ConnectRequestTest, EmptyCommandLine)
{
    const ConnectRequest request = parse({});

    EXPECT_TRUE(request.isEmpty());
    EXPECT_TRUE(request.identifier().isEmpty());
    EXPECT_EQ(request.sessionType(), proto::peer::SESSION_TYPE_UNKNOWN);
}

//--------------------------------------------------------------------------------------------------
// The identifier of a record of the address book, in every shape the option is written in.
TEST(ConnectRequestTest, ConnectOptionForms)
{
    const QStringList forms[] = {
        { QStringLiteral("--connect"), QStringLiteral("7") },
        { QStringLiteral("--connect=7") },
        { QStringLiteral("-c"), QStringLiteral("7") }};

    for (const QStringList& rest : forms)
    {
        const ConnectRequest request = parse(rest);

        EXPECT_FALSE(request.isEmpty()) << rest.join(' ').toStdString();
        EXPECT_EQ(request.identifier(), QStringLiteral("7")) << rest.join(' ').toStdString();
    }
}

//--------------------------------------------------------------------------------------------------
// A bare number is the same request, spelled without the option.
TEST(ConnectRequestTest, BareIdentifier)
{
    const ConnectRequest request = parse({ QStringLiteral("42") });

    EXPECT_FALSE(request.isEmpty());
    EXPECT_EQ(request.identifier(), QStringLiteral("42"));
}

//--------------------------------------------------------------------------------------------------
// The option wins over a bare number: what was spelled out is what was meant.
TEST(ConnectRequestTest, OptionWinsOverBareIdentifier)
{
    const ConnectRequest request = parse({ QStringLiteral("42"),
                                           QStringLiteral("--connect"),
                                           QStringLiteral("7") });

    EXPECT_EQ(request.identifier(), QStringLiteral("7"));
}

//--------------------------------------------------------------------------------------------------
// The kind of the session named on the command line.
TEST(ConnectRequestTest, SessionTypeNames)
{
    struct { const char* name; proto::peer::SessionType type; } kCases[] = {
        { "desktop", proto::peer::SESSION_TYPE_DESKTOP },
        { "file", proto::peer::SESSION_TYPE_FILE_TRANSFER },
        { "info", proto::peer::SESSION_TYPE_SYSTEM_INFO },
        { "chat", proto::peer::SESSION_TYPE_CHAT },
        { "terminal", proto::peer::SESSION_TYPE_TERMINAL }};

    for (const auto& test_case : kCases)
    {
        EXPECT_EQ(ConnectRequest::sessionTypeFromName(QString::fromLatin1(test_case.name)),
                  test_case.type);

        EXPECT_EQ(parse({ QStringLiteral("--connect=7"),
                          QStringLiteral("--session-type=") + QString::fromLatin1(test_case.name) })
                      .sessionType(),
                  test_case.type);
    }
}

//--------------------------------------------------------------------------------------------------
// The name of a kind is not case sensitive and tolerates the spaces around it, and the value
// behind the name is accepted for a script that speaks the wire values already.
TEST(ConnectRequestTest, SessionTypeNameIsForgiving)
{
    EXPECT_EQ(ConnectRequest::sessionTypeFromName(QStringLiteral(" Terminal ")),
              proto::peer::SESSION_TYPE_TERMINAL);

    EXPECT_EQ(parse({ QStringLiteral("--connect=7"), QStringLiteral("--session-type=32") })
                  .sessionType(),
              proto::peer::SESSION_TYPE_TERMINAL);

    EXPECT_EQ(parse({ QStringLiteral("--connect=7"), QStringLiteral("--session-type"), QStringLiteral("file") })
                  .sessionType(),
              proto::peer::SESSION_TYPE_FILE_TRANSFER);
}

//--------------------------------------------------------------------------------------------------
// A value that names no session is refused: a combination of the bits of several kinds opens
// nothing, and the desktop would be opened silently instead.
TEST(ConnectRequestTest, UnknownSessionTypeIsRefused)
{
    EXPECT_EQ(ConnectRequest::sessionTypeFromName(QStringLiteral("3")),
              proto::peer::SESSION_TYPE_UNKNOWN);

    EXPECT_EQ(ConnectRequest::sessionTypeFromName(QStringLiteral("0")),
              proto::peer::SESSION_TYPE_UNKNOWN);

    EXPECT_EQ(ConnectRequest::sessionTypeFromName(QString()),
              proto::peer::SESSION_TYPE_UNKNOWN);

    EXPECT_TRUE(fails({ QStringLiteral("--connect=7"), QStringLiteral("--session-type=console") }));
}

//--------------------------------------------------------------------------------------------------
// A record ID is a positive number. Zero, a negative number and an option without its value are
// all refused instead of connecting to nothing. A word is a name of a host now, and a host named
// by its address is connected to directly.
TEST(ConnectRequestTest, BadIdentifiersAreRefused)
{
    EXPECT_TRUE(fails({ QStringLiteral("--connect=0") }));
    EXPECT_TRUE(fails({ QStringLiteral("--connect=-1") }));
    EXPECT_TRUE(fails({ QStringLiteral("--connect=") }));
    EXPECT_TRUE(fails({ QStringLiteral("--connect") }));
    EXPECT_TRUE(fails({ QStringLiteral("host/name") }));
}

//--------------------------------------------------------------------------------------------------
// A host of no book is named by its address, and the client connects to it directly. An address
// may carry a port of its own, and a name of a host is an address as well.
TEST(ConnectRequestTest, AddressIsAccepted)
{
    const QStringList addresses = {
        QStringLiteral("192.168.1.5"),
        QStringLiteral("192.168.1.5:8050"),
        QStringLiteral("myhost.local"),
        QStringLiteral("myhost"),
        QStringLiteral("host-one"),
        QStringLiteral("12.5"),
        QStringLiteral("[::1]") };

    for (const QString& address : addresses)
    {
        const ConnectRequest request = parse({ QStringLiteral("--connect"), address });

        EXPECT_FALSE(request.isEmpty()) << address.toStdString();
        EXPECT_EQ(request.identifier(), address) << address.toStdString();
    }
}

//--------------------------------------------------------------------------------------------------
// The same, spelled without the option.
TEST(ConnectRequestTest, BareAddress)
{
    const ConnectRequest request = parse({ QStringLiteral("192.168.1.5") });

    EXPECT_FALSE(request.isEmpty());
    EXPECT_EQ(request.identifier(), QStringLiteral("192.168.1.5"));
}

//--------------------------------------------------------------------------------------------------
// An aspia:// link keeps being a link and is not read as an identifier.
TEST(ConnectRequestTest, LinkStaysALink)
{
    QString url;
    const ConnectRequest request = parse({ QStringLiteral("aspia://connect?d=ZT00MiZzPTE") }, &url);

    EXPECT_TRUE(request.isEmpty());
    EXPECT_EQ(url, QStringLiteral("aspia://connect?d=ZT00MiZzPTE"));
}

//--------------------------------------------------------------------------------------------------
// An option this parser knows nothing about is left to the caller, including the ones of Qt itself
// (--help, --version). Only a bare argument has to mean something here.
TEST(ConnectRequestTest, UnknownOptionsAreIgnored)
{
    const ConnectRequest request = parse({ QStringLiteral("--platform"), QStringLiteral("--connect=7") });

    EXPECT_EQ(request.identifier(), QStringLiteral("7"));
}

//--------------------------------------------------------------------------------------------------
// What a forwarded request carries: the record, the kind, and the credentials only when the command
// line named them.
TEST(ConnectRequestTest, RequestCarriesNoCredentialsByDefault)
{
    const ConnectRequest request = parse({ QStringLiteral("--connect=7"),
                                           QStringLiteral("--session-type=file") });

    EXPECT_EQ(request.identifier(), QStringLiteral("7"));
    EXPECT_EQ(request.sessionType(), proto::peer::SESSION_TYPE_FILE_TRANSFER);
    EXPECT_FALSE(request.hasCredentials());
    EXPECT_TRUE(request.username().isEmpty());
}

//--------------------------------------------------------------------------------------------------
// Credentials of the caller, in every shape the options are written in. They replace the ones of
// the record for this one connection.
TEST(ConnectRequestTest, CredentialsReplaceTheStoredOnes)
{
    const QStringList forms[] = {
        { QStringLiteral("--connect=7"), QStringLiteral("--username=admin"),
          QStringLiteral("--password=secret") },
        { QStringLiteral("--connect=7"), QStringLiteral("-u"), QStringLiteral("admin"),
          QStringLiteral("--password"), QStringLiteral("secret") }};

    for (const QStringList& rest : forms)
    {
        const ConnectRequest request = parse(rest);

        EXPECT_TRUE(request.hasCredentials()) << rest.join(' ').toStdString();
        EXPECT_EQ(request.username(), QStringLiteral("admin"));
        EXPECT_EQ(request.password().toString(), QStringLiteral("secret"));
    }
}

//--------------------------------------------------------------------------------------------------
// A login and a password may hold anything at all, the separator included.
TEST(ConnectRequestTest, CredentialsAreTakenVerbatim)
{
    const ConnectRequest request = parse({ QStringLiteral("--connect=7"),
                                           QStringLiteral("--username=user:name"),
                                           QStringLiteral("--password=pa:ss word") });

    EXPECT_EQ(request.username(), QStringLiteral("user:name"));
    EXPECT_EQ(request.password().toString(), QStringLiteral("pa:ss word"));
}

//--------------------------------------------------------------------------------------------------
// Half a pair is refused: the session would otherwise fall back to the stored credentials and ask
// for a dialog, which is not what a script asked for.
TEST(ConnectRequestTest, HalfCredentialsAreRefused)
{
    EXPECT_TRUE(fails({ QStringLiteral("--connect=7"), QStringLiteral("--username=admin") }));
    EXPECT_TRUE(fails({ QStringLiteral("--connect=7"), QStringLiteral("--password=secret") }));

    EXPECT_TRUE(fails({ QStringLiteral("--connect=7"), QStringLiteral("--username") }));
    EXPECT_TRUE(fails({ QStringLiteral("--connect=7"), QStringLiteral("--password") }));
}

//--------------------------------------------------------------------------------------------------
// Credentials with no host to use them are refused: there is nothing to log in to.
TEST(ConnectRequestTest, CredentialsWithoutAHostAreRefused)
{
    EXPECT_TRUE(fails({ QStringLiteral("--username=admin"), QStringLiteral("--password=secret") }));
}

//--------------------------------------------------------------------------------------------------
// The host ID is a number the host was given, the one a connection is made with and the one the
// "Address / ID" column shows. It is written exactly like a record ID, so it comes through as it is
// and is told apart from a record only by what the address book holds.
TEST(ConnectRequestTest, HostIdentifierIsAccepted)
{
    const QStringList forms[] = {
        { QStringLiteral("--connect"), QStringLiteral("1234567890123") },
        { QStringLiteral("--connect=1234567890123") },
        { QStringLiteral("-c"), QStringLiteral("1234567890123") },
        { QStringLiteral("1234567890123") }};

    for (const QStringList& rest : forms)
    {
        const ConnectRequest request = parse(rest);

        EXPECT_FALSE(request.isEmpty()) << rest.join(' ').toStdString();
        EXPECT_EQ(request.identifier(), QStringLiteral("1234567890123"));
    }
}

//--------------------------------------------------------------------------------------------------
// A host ID takes nothing away from the session type and the credentials given next to it.
TEST(ConnectRequestTest, HostIdentifierWithTheRest)
{
    const ConnectRequest request = parse({ QStringLiteral("--connect=1234567890123"),
                                           QStringLiteral("--session-type=terminal"),
                                           QStringLiteral("--username=admin"),
                                           QStringLiteral("--password=secret") });

    EXPECT_EQ(request.identifier(), QStringLiteral("1234567890123"));
    EXPECT_EQ(request.sessionType(), proto::peer::SESSION_TYPE_TERMINAL);
    EXPECT_EQ(request.username(), QStringLiteral("admin"));
    EXPECT_EQ(request.password().toString(), QStringLiteral("secret"));
}

//--------------------------------------------------------------------------------------------------
// Neither a record ID nor a host ID nor an address: a misspelt identifier is refused with a reason
// instead of being looked up in vain. A number is always an ID, and a name of a host is an
// address, so what is left is refused.
TEST(ConnectRequestTest, NeitherIdentifierIsRefused)
{
    EXPECT_TRUE(fails({ QStringLiteral("--connect=0") }));
    EXPECT_TRUE(fails({ QStringLiteral("--connect=-3") }));
    EXPECT_TRUE(fails({ QStringLiteral("--connect=") }));
    EXPECT_TRUE(fails({ QStringLiteral("--connect=host/name") }));
    EXPECT_TRUE(fails({ QStringLiteral("--connect=a host") }));
}