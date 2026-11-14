#pragma once
#include "AutomaticRecoveryLoad.h"
#include "AutomaticRecoverySession.h"
#include <QStringList>
#include <mpv/client.h>

// GUI/control-thread owner. No method exposes the saved load recipe to QML.
class AutomaticRecoveryRuntime {
public:
  using Key = AutomaticRecoverySession::Key;
  bool submitUserLoad(mpv_handle* core, Key key, const QStringList& command, const QString& url,
                      const QVariantMap& options, const QVariantMap& metadata,
                      const QVariant& audio, const QVariant& subtitle, bool paused, std::int64_t nowMs);
  void observe(const mpv_event* event, Key current, std::int64_t nowMs);
  void invalidate();
private:
  void settle(Key current, std::int64_t nowMs);
  void discardQueued();
  AutomaticRecoverySession activeSession_, queuedSession_;
  AutomaticRecoveryLoad load_;
  Key key_;
  std::uint64_t sequence_ = 0;
  std::int64_t startedEntry_ = 0, deadline_ = 0;
  bool loaded_ = false, matched_ = false, installed_ = false;
};
