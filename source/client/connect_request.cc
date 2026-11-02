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

#include <optional>

#include <QCoreApplication>
#include <QStringLiteral>

#include "base/build_config.h"
#include "base/crypto/secure_string.h"
#include "base/net/address.h"
#include "client/host_url.h"

namespace {

using namespace Qt::StringLiterals;

struct SessionTypeName
{
    const char* name;
    proto::peer::SessionType type;
};

// Every kind a session may be, by the short name --session-type accepts and by the value behind
// that name. A kind missing from the list cannot be asked for from the command line.
constexpr SessionTypeName kSessionTypes[] = {
    { "desktop", proto::peer::SESSION_TYPE_DESKTOP },
    { "file", proto::peer::SESSION_TYPE_FILE_TRANSFER },
    { "info", proto::peer::SESSION_TYPE_SYSTEM_INFO },
    { "chat", proto::peer::SESSION_TYPE_CHAT },
    { "terminal", proto::peer::SESSION_TYPE_TERMINAL }};

//--------------------------------------------------------------------------------------------------
// What identifies a host on the command line. It is either a number, which is a record ID of the
// address book or a host ID, or an address, which names a host that is connected to directly. The
// value is not checked against the database here - the client may be starting before it was
// unlocked - so only its shape is checked, and a misspelt identifier is refused with a reason
// instead of being looked up in vain.
std::optional<QString> identifierFromString(const QString& str)
{
    const QString trimmed = str.trimmed();

    if (trimmed.isEmpty())
        return std::nullopt;

    // A number is the ID of a record of the address book or of a host, and a number is never an
    // address: a host that is in no book is named by its address, and an address is never a number.
    bool number = true;

    for (const QChar character : trimmed)
    {
        if (!character.isDigit())
        {
            number = false;
            break;
        }
    }

    if (number)
    {
        bool number_ok = false;
        const qulonglong identifier = trimmed.toULongLong(&number_ok);

        if (!number_ok || identifier == 0)
            return std::nullopt;

        return trimmed;
    }

    // Everything else is an address, and the client connects to such a host directly, the way it
    // does for a host of the address book that has no router of its own.
    if (ConnectRequest::isHostAddress(trimmed))
        return trimmed;

    return std::nullopt;
}

//--------------------------------------------------------------------------------------------------
// The value of an option, written either "--connect 3" or "--connect=3". |recognized| tells a value
// that was written from an option that is not there at all, which is not a refusal.
struct OptionValue
{
    std::optional<QString> value;
    bool recognized = false;
};

OptionValue optionValue(const QStringList& arguments, int* index, const QString& option,
                         const QString& short_option = QString())
{
    const QString& argument = arguments.at(*index);

    if (argument == option || (!short_option.isEmpty() && argument == short_option))
    {
        // The value is the next argument; there is nothing else on this one.
        if (*index + 1 >= arguments.size())
            return { std::nullopt, true };

        ++(*index);
        return { arguments.at(*index), true };
    }

    if (argument.startsWith(option + u'='))
        return { argument.mid(static_cast<qsizetype>(option.size()) + 1), true };

    return {};
}

} // namespace

//--------------------------------------------------------------------------------------------------
bool ConnectRequest::parse(const QStringList& arguments, ConnectRequest* request, QString* url,
                           QString* error)
{
    ConnectRequest result;
    QString start_url;

    for (int i = 0; i < arguments.size(); ++i)
    {
        const QString& argument = arguments.at(i);

        if (const OptionValue connect = optionValue(arguments, &i, u"--connect"_s, u"-c"_s);
            connect.recognized)
        {
            // An option at the end of the line with nothing behind it is a refusal, not a plain
            // start: something was asked for and could not be read.
            const std::optional<QString> identifier =
                connect.value ? identifierFromString(*connect.value) : std::nullopt;

            if (!identifier)
            {
                if (error)
                {
                    *error = connect.value ?
                        QCoreApplication::translate(
                            "Client", "\"%1\" is neither a host ID nor a record ID nor a host "
                                       "address.").arg(*connect.value) :
                        QCoreApplication::translate(
                            "Client", "The host ID of --connect is missing.");
                }
                return false;
            }

            // Spelled out, so it wins over a bare argument found anywhere on the line.
            result.identifier_ = *identifier;
            continue;
        }

        if (const OptionValue session = optionValue(arguments, &i, u"--session-type"_s);
            session.recognized)
        {
            proto::peer::SessionType session_type = session.value ?
                sessionTypeFromName(*session.value) : proto::peer::SESSION_TYPE_UNKNOWN;

            if (session_type == proto::peer::SESSION_TYPE_UNKNOWN)
            {
                if (error)
                {
                    *error = session.value ?
                        QCoreApplication::translate(
                            "Client", "\"%1\" is not a valid session type. Use desktop, file, "
                                       "info, chat or terminal.").arg(*session.value) :
                        QCoreApplication::translate(
                            "Client", "The session type of --session-type is missing.");
                }
                return false;
            }

            result.session_type_ = session_type;
            continue;
        }

        if (const OptionValue username = optionValue(arguments, &i, u"--username"_s, u"-u"_s);
            username.recognized)
        {
            if (!username.value)
            {
                if (error)
                {
                    *error = QCoreApplication::translate(
                        "Client", "The user name of --username is missing.");
                }
                return false;
            }

            result.username_ = *username.value;
            continue;
        }

        if (const OptionValue password = optionValue(arguments, &i, u"--password"_s); password.recognized)
        {
            if (!password.value)
            {
                if (error)
                {
                    *error = QCoreApplication::translate(
                        "Client", "The password of --password is missing.");
                }
                return false;
            }

            result.password_ = SecureString(*password.value);
            continue;
        }

        if (argument.startsWith(u'-'))
        {
            // An option this parser knows nothing about, including the ones of Qt itself
            // (--help, --version). Deciding what to do with those belongs to the caller, which
            // hands over a line its own parser has already accepted.
            continue;
        }

        // What is left is a link or a bare identifier. A link wins: it names a host of its own.
        if (HostUrl::isHostUrl(argument))
        {
            start_url = argument;
            continue;
        }

        std::optional<QString> identifier = identifierFromString(argument);
        if (!identifier)
        {
            if (error)
            {
                *error = QCoreApplication::translate(
                    "Client", "\"%1\" is neither an aspia:// link nor a host ID nor a host "
                               "address.").arg(argument);
            }
            return false;
        }

        if (result.identifier_.isEmpty())
            result.identifier_ = *identifier;
    }

    // Half of a pair is no pair: the session would fall back to asking for the rest in a dialog,
    // which is not what a script asked for, and mixing one given value with one stored from the
    // record is not something anybody means on a command line.
    if (result.username_.isEmpty() != result.password_.isEmpty())
    {
        if (error)
        {
            *error = result.username_.isEmpty() ?
                QCoreApplication::translate(
                    "Client", "--username requires --password as well.") :
                QCoreApplication::translate(
                    "Client", "--password requires --username as well.");
        }
        return false;
    }

    // Credentials on their own are a connection to no record at all.
    if (!result.username_.isEmpty() && result.identifier_.isEmpty())
    {
        if (error)
        {
            *error = QCoreApplication::translate(
                "Client", "--username and --password need a host to connect to "
                           "(--connect or an aspia:// link).");
        }
        return false;
    }

    *request = result;

    if (url)
        *url = start_url;

    return true;
}

//--------------------------------------------------------------------------------------------------
bool ConnectRequest::isHostAddress(const QString& str)
{
    const QString trimmed = str.trimmed();

    if (trimmed.isEmpty())
        return false;

    // A number is always an ID and never an address.
    bool number = true;

    for (const QChar character : trimmed)
    {
        if (!character.isDigit())
        {
            number = false;
            break;
        }
    }

    if (number)
        return false;

    // The name of a host cannot begin with a hyphen, so a negative number is not an address
    // either, and a name that reads as no address at all is not one.
    const Address address = Address::fromString(trimmed, kDefaultHostTcpPort);

    return address.isValid() && !address.host().startsWith('-');
}

//--------------------------------------------------------------------------------------------------
proto::peer::SessionType ConnectRequest::sessionTypeFromName(const QString& name)
{
    const QString lower = name.trimmed().toLower();

    for (const auto& entry : kSessionTypes)
    {
        if (lower == QLatin1String(entry.name))
            return entry.type;
    }

    // A number is accepted too, so that a script which already speaks the wire values needs no
    // table. Only the values of the kinds above: a combination of their bits names no session.
    bool number_ok = false;
    const int value = lower.toInt(&number_ok);

    if (number_ok)
    {
        for (const auto& entry : kSessionTypes)
        {
            if (value == static_cast<int>(entry.type))
                return entry.type;
        }
    }

    return proto::peer::SESSION_TYPE_UNKNOWN;
}