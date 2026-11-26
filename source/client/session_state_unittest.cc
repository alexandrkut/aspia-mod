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

#include "client/session_state.h"

#include "base/peer/host_id.h"
#include "client/config.h"
#include "proto/peer.h"

#include <gtest/gtest.h>

namespace {

HostConfig makeHost()
{
    HostConfig config;
    config.setAddress("127.0.0.1");
    config.setName("test");
    return config;
}

} // namespace

TEST(session_state_test, default_view_only_is_false)
{
    SessionState state(makeHost(), ProfileConfig(), proto::peer::SESSION_TYPE_DESKTOP, "display");
    EXPECT_FALSE(state.viewOnly());
}

TEST(session_state_test, constructor_accepts_view_only_true)
{
    SessionState state(makeHost(), ProfileConfig(), proto::peer::SESSION_TYPE_DESKTOP, "display", true);
    EXPECT_TRUE(state.viewOnly());
}

TEST(session_state_test, set_and_get_view_only)
{
    SessionState state(makeHost(), ProfileConfig(), proto::peer::SESSION_TYPE_DESKTOP, "display");
    state.setViewOnly(true);
    EXPECT_TRUE(state.viewOnly());
    state.setViewOnly(false);
    EXPECT_FALSE(state.viewOnly());
}
