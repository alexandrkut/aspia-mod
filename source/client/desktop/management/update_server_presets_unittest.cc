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

#include "client/desktop/management/update_server_presets.h"

#include <QUrl>

#include <gtest/gtest.h>

TEST(update_server_presets_test, original_server_is_valid_https_url)
{
    const QUrl url(QString::fromLatin1(kOriginalUpdateServer));
    EXPECT_TRUE(url.isValid());
    EXPECT_EQ(url.scheme(), "https");
    EXPECT_FALSE(url.host().isEmpty());
}

TEST(update_server_presets_test, original_public_key_is_64_hex_chars)
{
    const QString key = QString::fromLatin1(kOriginalUpdatePublicKey);
    EXPECT_EQ(key.size(), 64);
    const QByteArray bytes = QByteArray::fromHex(key.toLower().toLatin1());
    EXPECT_EQ(bytes.size(), 32);
    EXPECT_EQ(bytes.toHex(), key.toLower().toLatin1());
}
