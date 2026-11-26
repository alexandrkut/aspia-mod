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

#include "router/client_qs_client.h"

#include "proto/router_client.h"

//--------------------------------------------------------------------------------------------------
ClientQsClient::ClientQsClient(Database& database, AccessLog& access_log, TcpChannel* channel,
                               QObject* parent, bool /* two_factor_required */)
    : ClientOperator(database, access_log, channel, parent, false)
{
    // The account is baked into a file handed to strangers. Even a caller that asks for a code
    // cannot get one here: there is no phone to enter it on.
    CLOG(INFO) << "Ctor";
}

//--------------------------------------------------------------------------------------------------
ClientQsClient::~ClientQsClient()
{
    CLOG(INFO) << "Dtor";
}

//--------------------------------------------------------------------------------------------------
bool ClientQsClient::isMessageAllowed(const proto::router::ClientToRouter& message) const
{
    return message.has_connection_request();
}

//--------------------------------------------------------------------------------------------------
bool ClientQsClient::isSyncAllowed() const
{
    return false;
}
