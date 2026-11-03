#pragma once

#include <QString>
#include <functional>

namespace MpvTestBootstrap {
enum class Entry { Directory, File, Missing, Link, Error };
using Inspector = std::function<Entry(const QString&)>;

// The inspector overload allows deterministic tests of inaccessible paths.
bool checkCandidate(const QString& candidate, const QString& ownedRoot,
                    const Inspector& inspect, QString* error);
bool checkCandidateOnDisk(const QString& candidate, const QString& ownedRoot, QString* error);
bool prepareMpvTestEnvironment(const QString& ownedRoot, QString* error);
}
