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

#include "client/desktop/management/router_host_dialog.h"

#include <gtest/gtest.h>

namespace {

using Membership = QPair<qint64, qint64>;

} // namespace

// The router reads a zero group in a membership as "move the host to the workspace root", so
// the edit dialog must send back the groups the list reply carried. A regression here silently
// drops every host out of its group on the next "Edit host".
TEST(RouterHostDialogTest, KeptMembershipsKeepTheirGroups)
{
    const QList<Membership> existing = { qMakePair(901, 0), qMakePair(902, 1) };

    const QList<Membership> result =
        RouterHostDialog::buildMemberships(existing, { 901, 902 }, 902, 1);

    EXPECT_EQ(result, (QList<Membership>{ qMakePair(901, 0), qMakePair(902, 1) }));
}

// The group picker edits the primary workspace only; its value must land on that membership,
// otherwise the picker has no effect at all on a host that stays in its workspace.
TEST(RouterHostDialogTest, PrimaryGroupFollowsThePicker)
{
    const QList<Membership> existing = { qMakePair(901, 0), qMakePair(902, 1) };

    const QList<Membership> at_root =
        RouterHostDialog::buildMemberships(existing, { 901, 902 }, 902, 0);
    EXPECT_EQ(at_root, (QList<Membership>{ qMakePair(901, 0), qMakePair(902, 0) }));

    const QList<Membership> in_group =
        RouterHostDialog::buildMemberships(existing, { 901, 902 }, 902, 7);
    EXPECT_EQ(in_group, (QList<Membership>{ qMakePair(901, 0), qMakePair(902, 7) }));
}

// Unchecking a workspace removes that membership and only that one.
TEST(RouterHostDialogTest, UncheckedMembershipIsDropped)
{
    const QList<Membership> existing = { qMakePair(901, 0), qMakePair(902, 1) };

    const QList<Membership> result =
        RouterHostDialog::buildMemberships(existing, { 902 }, 902, 1);

    EXPECT_EQ(result, (QList<Membership>{ qMakePair(902, 1) }));
}

// A workspace the host did not belong to starts at the workspace root.
TEST(RouterHostDialogTest, NewlyCheckedWorkspaceStartsAtRoot)
{
    const QList<Membership> existing = { qMakePair(901, 3) };

    const QList<Membership> result =
        RouterHostDialog::buildMemberships(existing, { 901, 903 }, 901, 3);

    EXPECT_EQ(result, (QList<Membership>{ qMakePair(901, 3), qMakePair(903, 0) }));
}

// The primary workspace unchecked: no pair for it, and the kept memberships are untouched.
TEST(RouterHostDialogTest, UncheckedPrimaryLeavesNoPair)
{
    const QList<Membership> existing = { qMakePair(901, 0), qMakePair(902, 1) };

    const QList<Membership> result =
        RouterHostDialog::buildMemberships(existing, { 901 }, 902, 1);

    EXPECT_EQ(result, (QList<Membership>{ qMakePair(901, 0) }));
}
