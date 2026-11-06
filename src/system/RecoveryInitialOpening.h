#pragma once

#include <QUrl>
#include <QUuid>
#include <functional>

// Consistency checks for one host-dispatched initial navigation, not document identity.
class RecoveryInitialOpening
{
public:
  enum class State { Unopened, Opening, ObservationReady, Revoked };
  bool open(const QUrl& url, const std::function<bool()>& dispatch);
  void navigation(const QUrl& url, bool mainFrame, bool expectedKind);
  void loadStarted(const QUrl& url);
  void revoke();
  State state() const { return m_state; }
private:
  void settle();
  State m_state = State::Unopened;
  QUrl m_expected;
  QUuid m_openingId;
  bool m_requested = false, m_started = false, m_dispatched = false;
};
