#pragma once
#include <QString>
#include <QVariant>
#include <QVariantMap>
#include <cstdint>

// Control-thread only. Credentials in the original load remain in private memory.
class AutomaticRecoverySession {
public:
  struct Key { std::uint64_t session = 0, revision = 0, generation = 0; };
  struct Snapshot {
    Key key;
    std::int64_t entry = 0, positionMs = -1;
    bool paused = true;
    QString url;
    QVariantMap options, metadata;
    QVariant audio, subtitle;
  };
  void begin(Key key, const QString& url, const QVariantMap& options, const QVariantMap& metadata,
             const QVariant& audio, const QVariant& subtitle, bool paused);
  bool accept(Key key, std::int64_t entry);
  bool position(Key key, std::int64_t entry, std::int64_t positionMs);
  bool revise(Key previous, Key current, bool paused);
  void invalidate();
  bool snapshot(Key current, Snapshot& result) const;
private:
  static bool same(Key a, Key b);
  Snapshot candidate_;
  bool accepted_ = false;
};
