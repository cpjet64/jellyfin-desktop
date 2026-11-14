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
  void invalidate();
private:
  AutomaticRecoveryQueue queue_;
  std::uint64_t sequence_ = 0, generation_ = 0;
};
