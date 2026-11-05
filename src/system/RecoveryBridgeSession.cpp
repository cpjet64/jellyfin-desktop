#include "RecoveryBridgeSession.h"

#include <QCoreApplication>
#include <QThread>
#include <QUuid>
#include <QWebChannel>

namespace {
constexpr quint64 MaxCommand = 9007199254740991ULL;

bool commandNumber(const QVariant& value, quint64& number)
{
  if (value.metaType().id() != QMetaType::QString) return false;
  const QString text = value.toString();
  if (text.isEmpty() || text.size() > 16 || text[0] == QLatin1Char('0')) return false;
  quint64 result = 0;
  for (const auto c : text) {
    if (c < QLatin1Char('0') || c > QLatin1Char('9')) return false;
    const auto digit = static_cast<quint64>(c.unicode() - '0');
    if (result > (MaxCommand - digit) / 10) return false;
    result = result * 10 + digit;
  }
  number = result;
  return number != 0;
}

QVariantMap receipt(const QString& status, quint64 id = 0)
{
  return {{QStringLiteral("version"), 1},
          {QStringLiteral("commandId"), id ? QString::number(id) : QString()},
          {QStringLiteral("status"), status}};
}
}

RecoveryBridgeEndpoint::RecoveryBridgeEndpoint(QObject* parent)
  : QObject(parent), m_sessionId(QUuid::createUuid().toString(QUuid::WithoutBraces))
{
}

void RecoveryBridgeEndpoint::revoke()
{
  if (m_revoked) return;
  m_revoked = true;
  emit revoked();
}

QVariantMap RecoveryBridgeEndpoint::invoke(const QVariantMap& request)
{
  if (QThread::currentThread() != thread() || m_revoked) return receipt(QStringLiteral("revoked"));
  quint64 id = 0;
  const auto version = request.value(QStringLiteral("version"));
  const auto session = request.value(QStringLiteral("sessionId"));
  const auto operation = request.value(QStringLiteral("operation"));
  const auto type = version.metaType().id();
  if (request.size() != 4 || (type != QMetaType::Int && type != QMetaType::UInt &&
      type != QMetaType::LongLong && type != QMetaType::ULongLong && type != QMetaType::Double) || version.toDouble() != 1.0 ||
      session.metaType().id() != QMetaType::QString || session.toString() != m_sessionId ||
      operation.metaType().id() != QMetaType::QString || operation.toString() != QLatin1String("handshake") ||
      !commandNumber(request.value(QStringLiteral("commandId")), id))
    return receipt(QStringLiteral("rejected"));
  if (id < m_lastCommand) return receipt(QStringLiteral("rejected"), id);
  if (id == m_lastCommand) return receipt(QStringLiteral("duplicate"), id);
  m_lastCommand = id;
  return receipt(QStringLiteral("accepted"), id);
}

RecoveryBridgeSession::RecoveryBridgeSession(QObject* window, QWebChannel* channel)
  : QObject(channel), m_endpoint(new RecoveryBridgeEndpoint(this))
{
  if (!window || !channel || window->thread() != thread() || channel->thread() != thread() ||
      QThread::currentThread() != thread()) { revoke(); return; }
  connect(window, &QObject::destroyed, this, &RecoveryBridgeSession::revoke);
  if (QCoreApplication::instance())
    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, &RecoveryBridgeSession::revoke);
}

RecoveryBridgeSession::~RecoveryBridgeSession()
{
  revoke();
}

void RecoveryBridgeSession::revoke()
{
  if (m_endpoint) m_endpoint->revoke();
}
