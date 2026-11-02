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

#ifndef CLIENT_CONNECT_REQUEST_H
#define CLIENT_CONNECT_REQUEST_H

#include <QString>
#include <QStringList>

#include "base/crypto/secure_string.h"
#include "proto/peer.h"

// The connection asked for on the command line of the application.
//
// Two shapes say the same thing:
//
//   aspia-client --connect 3
//   aspia-client -c 3
//   aspia-client --connect=3
//   aspia-client 3
//
// The identifier is the record ID of the address book entry of a host, or the address of a host
// that is connected to directly. For a record of the address book nothing else is taken from the
// command line: the login and the password are the ones stored in that record, so a script that
// starts the client never carries credentials of its own. A host named by its address has no
// record of it, and there are no stored credentials to take, so the login and the password have
// to be given with --username and --password.
//
// --session-type names the kind of the session to open (desktop, file, info, chat, terminal, or
// the numeric value behind those names). Without it the session type of the settings is used.
//
// An aspia:// link keeps working as a positional argument and is answered with a URL for the
// caller to open; it is never mixed with the record ID.
//
// --username and --password replace the credentials of the record for this one connection. Both
// are optional and both have to make sense together: a login without a password (or the other way
// round) is refused rather than half applied, because the session would fall back to asking for the
// rest anyway. A password on a command line is visible to the other users of the machine in the
// process list, so it belongs to a script and not to a shell history.
class ConnectRequest final
{
public:
    ConnectRequest() = default;

    // Parses the arguments of the application, without the program name. On success the request holds
    // a record ID, a URL, or neither (a plain start); |error| then carries a message fit to show
    // the user. Returns false only when an argument cannot be understood.
    static bool parse(const QStringList& arguments, ConnectRequest* request, QString* url,
                      QString* error);

    public:
    // A value that is filled in one place and read in another, so it is copied and assigned like
    // any other value: parse() hands the finished request back by assignment, and the caller keeps
    // it by value. Q_DISABLE_COPY_MOVE is not an option here, it deletes the assignment.
    ConnectRequest(const ConnectRequest&) = default;
    ConnectRequest& operator=(const ConnectRequest&) = default;
    ConnectRequest(ConnectRequest&&) = default;
    ConnectRequest& operator=(ConnectRequest&&) = default;

    // What the command line named the host by: a record ID, a host ID, or nothing at all for a
    // plain start. Kept exactly as written, since which of the two it is can only be decided when
    // the record is found.
    const QString& identifier() const { return identifier_; }
    void setIdentifier(const QString& identifier) { identifier_ = identifier; }

    // The kind of the session to open. SESSION_TYPE_UNKNOWN when the command line named none, in
    // which case the caller falls back to the settings.
    proto::peer::SessionType sessionType() const { return session_type_; }
    void setSessionType(proto::peer::SessionType session_type) { session_type_ = session_type; }

    // The credentials that replace the ones of the record. Empty when the command line named none,
    // which is not the same as an empty login: |hasCredentials| tells them apart.
    const QString& username() const { return username_; }
    const SecureString& password() const { return password_; }
    bool hasCredentials() const { return !username_.isEmpty() || !password_.isEmpty(); }

    void setCredentials(const QString& username, const SecureString& password)
    {
        username_ = username;
        password_ = password;
    }

    void clearCredentials()
    {
        username_.clear();
        password_ = SecureString();
    }

    bool isEmpty() const { return identifier_.isEmpty(); }

    // The name a --session-type value may carry. Returns SESSION_TYPE_UNKNOWN when the name names
    // no session, or is a number that is not one of the values below.
    static proto::peer::SessionType sessionTypeFromName(const QString& name);

    // Whether |str| names a host by its address and not by its ID. A number is always an ID, for
    // an address is never a number.
    static bool isHostAddress(const QString& str);

private:
    QString identifier_;
    proto::peer::SessionType session_type_ = proto::peer::SESSION_TYPE_UNKNOWN;
    QString username_;
    SecureString password_;
};

#endif // CLIENT_CONNECT_REQUEST_H