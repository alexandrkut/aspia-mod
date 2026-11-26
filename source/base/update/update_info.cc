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

#include "base/update/update_info.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include "base/logging.h"

namespace {

const int kSchemaFormat = 1;
const int kMaxDescriptionLength = 4096;
const int kMinUrlLength = 10;
const int kMaxUrlLength = 256;
const int kSha256Length = 64;

volatile auto g_updateInfoType = qRegisterMetaType<UpdateInfo>();

//--------------------------------------------------------------------------------------------------
bool isLowerHex(const QString& text)
{
    for (QChar character : text)
    {
        if (!((character >= '0' && character <= '9') || (character >= 'a' && character <= 'f')))
            return false;
    }

    return true;
}

//--------------------------------------------------------------------------------------------------
QJsonObject rootObject(const QByteArray& buffer)
{
    QJsonParseError error;
    QJsonDocument document = QJsonDocument::fromJson(buffer, &error);
    if (!document.isObject())
    {
        LOG(ERROR) << "Unable to parse JSON:" << error.errorString();
        return QJsonObject();
    }

    QJsonObject root = document.object();
    int format = root.value("format").toInt();
    if (format != kSchemaFormat)
    {
        LOG(ERROR) << "Unsupported schema format:" << format;
        return QJsonObject();
    }

    return root;
}

} // namespace

//--------------------------------------------------------------------------------------------------
// static
std::optional<QVersionNumber> UpdateInfo::targetVersion(
    const QByteArray& buffer, const QVersionNumber& current)
{
    QJsonObject root = rootObject(buffer);
    if (root.isEmpty())
        return std::nullopt;

    const QJsonArray rules = root.value("updates").toArray();

    // Resolve a rule whose source matches the given current version. Returns an empty optional
    // when no matching rule exists, and a null optional on a hard error (unknown label / bad
    // target), so the caller can distinguish "no rule" from "broken manifest".
    auto resolve = [&](const QVersionNumber& needle, bool* hard_error) -> std::optional<QVersionNumber>
    {
        *hard_error = false;
        for (const QJsonValue& item : rules)
        {
            QJsonObject rule = item.toObject();
            QVersionNumber source = QVersionNumber::fromString(rule.value("source").toString());

            if (source.normalized() != needle.normalized())
                continue;

            QString target = rule.value("target").toString();

            // A rule may name a label instead of a version. The label is the only place where the
            // current version is written down, so a release moves it and leaves the rules alone.
            if (target.startsWith('@'))
            {
                QString label = target.mid(1);

                target = root.value("targets").toObject().value(label).toString();
                if (target.isEmpty())
                {
                    LOG(ERROR) << "Unknown label:" << label;
                    *hard_error = true;
                    return std::nullopt;
                }
            }

            QVersionNumber version = QVersionNumber::fromString(target);
            if (version.isNull())
            {
                LOG(ERROR) << "Invalid target version:" << target;
                *hard_error = true;
                return std::nullopt;
            }

            return version;
        }

        return std::nullopt;
    };

    bool hard_error = false;
    auto result = resolve(current, &hard_error);
    if (hard_error)
        return std::nullopt;
    if (result.has_value())
        return result;

    // Fallback: a four-segment client version (build number) has no exact rule, but the release
    // rules are written against the three-segment line. Try once with the build number stripped.
    // A three-segment current version is never retried, so a missing rule stays a clean "no update".
    if (current.segmentCount() > 3)
    {
        QVersionNumber trimmed(current.segments().mid(0, 3));
        LOG(INFO) << "No rule for version" << current.toString()
                  << ", falling back to" << trimmed.toString();

        bool fallback_error = false;
        auto fallback = resolve(trimmed, &fallback_error);
        if (fallback_error)
            return std::nullopt;
        if (fallback.has_value())
            return fallback;
    }

    LOG(INFO) << "No rule for version" << current.toString();
    return QVersionNumber();
}

//--------------------------------------------------------------------------------------------------
// static
std::optional<UpdateInfo> UpdateInfo::fromManifest(const QByteArray& buffer,
                                                   const QString& package, const QString& os,
                                                   const QString& arch, const QString& format)
{
    QJsonObject root = rootObject(buffer);
    if (root.isEmpty())
        return std::nullopt;

    const QJsonArray files = root.value("packages").toObject()
                                 .value(package).toObject()
                                 .value(os).toObject()
                                 .value(arch).toArray();
    if (files.isEmpty())
    {
        // A missing entry is how the manifest says the release has no build for this platform.
        LOG(INFO) << "Release has no files for" << package << os << arch;
        return UpdateInfo();
    }

    QJsonObject file = files.first().toObject();
    for (const QJsonValue& item : files)
    {
        if (!format.isEmpty() && item.toObject().value("format").toString() == format)
        {
            file = item.toObject();
            break;
        }
    }

    // The files of a release lie in one directory, so the manifest names it once and an entry
    // names nothing but the file.
    QString path = root.value("path").toString();
    while (path.endsWith('/'))
        path.chop(1);

    QString file_name = file.value("file").toString();

    UpdateInfo update_info;
    update_info.version_ = QVersionNumber::fromString(root.value("version").toString());
    update_info.description_ = root.value("description").toString();
    update_info.sha256_ = file.value("sha256").toString().toLower();
    update_info.format_ = file.value("format").toString();

    if (!path.isEmpty() && !file_name.isEmpty())
        update_info.url_ = path + "/" + file_name;

    if (update_info.version_.isNull())
    {
        LOG(ERROR) << "Manifest without a version";
    }
    else if (update_info.description_.size() > kMaxDescriptionLength)
    {
        LOG(ERROR) << "Too many characters in description";
    }
    else if (path.isEmpty())
    {
        LOG(ERROR) << "Manifest without a path";
    }
    else if (file_name.isEmpty())
    {
        LOG(ERROR) << "Entry without a file name";
    }
    else if (update_info.url_.size() < kMinUrlLength || update_info.url_.size() > kMaxUrlLength)
    {
        LOG(ERROR) << "Incorrect number of characters in URL";
    }
    else if (update_info.sha256_.size() != kSha256Length || !isLowerHex(update_info.sha256_))
    {
        LOG(ERROR) << "Invalid sha256 of file" << update_info.url_;
    }
    else if (update_info.format_.isEmpty())
    {
        LOG(ERROR) << "File without a format:" << update_info.url_;
    }
    else
    {
        update_info.valid_ = true;
    }

    if (!update_info.valid_)
        return std::nullopt;

    return update_info;
}
