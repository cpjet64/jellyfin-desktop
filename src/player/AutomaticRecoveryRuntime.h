#pragma once
#include "AutomaticRecoveryLoad.h"
#include "AutomaticRecoveryQueue.h"
#include <QStringList>
#include <functional>
#include <mpv/client.h>

// GUI/control-thread owner. No method exposes the saved load recipe to QML.
class AutomaticRecoveryRuntime {
public:
  using Key = AutomaticRecoverySession::Key;
  // The production dispatcher must retain an admitted core lease for the call.
  using CommandDispatch = std::function<int(std::uint64_t, const char**)>;
  bool submitUserLoad(const CommandDispatch& dispatch, Key key, const QStringList& command, const QString& url,
                      const QVariantMap& options, const QVariantMap& metadata,
                      const QVariant& audio, const QVariant& subtitle, bool paused, std::int64_t nowMs);
  void observe(const mpv_event* event, Key current, std::int64_t nowMs);
  void userIntentChanged();
  std::uint64_t eventEpoch() const { return eventEpoch_; }
  void internalEvent(const mpv_event* event);
  Key activeKey() const { return queue_.activeKey(); }
  std::int64_t activeEntry() const { return queue_.activeEntry(); }
  // The caller must supply only a completed, epoch-bracketed native observation.
  bool recordPosition(Key key, std::int64_t entry, std::int64_t positionMs, std::int64_t nowMs);
  bool snapshot(Key current, AutomaticRecoverySession::Snapshot& output) const;
  bool reviseActive(Key previous, Key current, bool paused);
  bool updateTracks(Key current, const QVariant& audio, const QVariant& subtitle);
  bool replaceActiveEntry(Key current, std::int64_t previous, std::int64_t entry, std::int64_t positionMs);
  std::uint64_t reserveSequence();
  bool takeTimeout(Key current, std::int64_t nowMs, std::uint64_t& sequence);
  void invalidate();
private:
  void advanceEpoch();
  AutomaticRecoveryQueue queue_;
  Key revoked_;
  std::uint64_t sequence_ = 0, generation_ = 0;
  std::uint64_t eventEpoch_ = 1;
};
