#pragma once
#include <QSet>
#include <QString>
#include <cstdint>
#include <mpv/client.h>

namespace OrdinaryPlaylist {
bool entries(const mpv_node& playlist, QSet<std::int64_t>& ids);
bool currentMatches(const mpv_node& playlist, std::int64_t entry, const QString& encodedUrl);
}
