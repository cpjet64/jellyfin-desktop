#include "RecoveryInitialOpening.h"

bool RecoveryInitialOpening::open(const QUrl& url, const std::function<bool()>& dispatch)
{
  if (m_state != State::Unopened || !url.isValid() || url.isEmpty() || !dispatch) {
    revoke();
    return false;
  }
  m_state = State::Opening;
  m_openingId = QUuid::createUuid();
  if (m_openingId.isNull()) { revoke(); return false; }
  m_expected = url;
  bool delivered = false;
  try { delivered = dispatch(); } catch (...) { revoke(); throw; }
  if (!delivered || m_state == State::Revoked) { revoke(); return false; }
  m_dispatched = true;
  settle();
  return m_state != State::Revoked;
}

void RecoveryInitialOpening::navigation(const QUrl& url, bool mainFrame, bool expectedKind)
{
  if (!mainFrame) return;
  if (m_state != State::Opening || m_openingId.isNull() || m_requested || !expectedKind || url != m_expected) { revoke(); return; }
  m_requested = true;
  settle();
}

void RecoveryInitialOpening::loadStarted(const QUrl& url)
{
  if (m_state != State::Opening || m_openingId.isNull() || m_started || url != m_expected) { revoke(); return; }
  m_started = true;
  settle();
}

void RecoveryInitialOpening::settle()
{
  if (m_state == State::Opening && m_requested && m_started && m_dispatched) {
    m_state = State::ObservationReady;
    m_expected.clear();
  }
}

void RecoveryInitialOpening::revoke()
{
  m_state = State::Revoked;
  m_expected.clear();
  m_openingId = {};
}
