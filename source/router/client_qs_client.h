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

#ifndef ROUTER_CLIENT_QS_CLIENT_H
#define ROUTER_CLIENT_QS_CLIENT_H

#include "router/client_operator.h"

// A session opened by a preconfigured Quick Support client. Its account is baked into a file that
// can be handed to anybody, so the router gives it exactly one ability: asking for a connection to
// a portable Quick Support host. Lists, status checks, password changes and every management RPC
// are refused. The account is recognized by the QS flag bit in its session mask; the "-qs" suffix
// is only a readable name, not a permission. 2FA is forced off for the flagged account.
class ClientQsClient final : public ClientOperator
{
    Q_OBJECT

public:
    ClientQsClient(Database& database, AccessLog& access_log, TcpChannel* channel, QObject* parent,
                   bool two_factor_required = false);
    ~ClientQsClient() final;

protected:
    // ClientOperator implementation.
    bool isMessageAllowed(const proto::router::ClientToRouter& message) const final;
    bool isHostIdAllowed(HostId host_id) const final;

private:
    Q_DISABLE_COPY_MOVE(ClientQsClient)
};

#endif // ROUTER_CLIENT_QS_CLIENT_H
