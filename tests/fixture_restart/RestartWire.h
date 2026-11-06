#pragma once
#include "RestartBroker.h"

namespace FixtureRestart {
// One checkpoint frame per A allocation. The receiving caller must prove that
// the bytes came from A's retained channel and that the stream has ended.
QByteArray encodeEvidence(const Evidence& evidence);
bool decodeEvidence(const QByteArray& frame, const Grant& parentBinding, bool eof, Evidence& result);

enum class ControlKind : quint8 { InternalStop = 1, UserStop = 2, NewPlay = 3, Ack = 4 };
struct Control { ControlKind kind = ControlKind::InternalStop; quint64 sequence = 0, revision = 0; };
QByteArray encodeControl(const Control& value);
bool decodeControl(const QByteArray& bytes, Control& result);

struct Closed {
  quint64 run = 0, launch = 0, stopSequence = 0, revision = 0;
  quint64 core = 0, renderer = 0, closure = 0;
};
QByteArray encodeClosed(const Closed& value);
bool decodeClosed(const QByteArray& bytes, const Closed& expected, Closed& result);

struct GrantAccepted {
  quint64 run = 0, launch = 0, grant = 0, revision = 0;
  QByteArray digest;
};
QByteArray encodeGrantAccepted(const GrantAccepted& value);
bool decodeGrantAccepted(const QByteArray& bytes, const GrantAccepted& expected, GrantAccepted& result);

struct RestorePermit {
  quint64 run = 0, launch = 0, grant = 0, revision = 0, sequence = 0;
  QByteArray digest;
};
QByteArray encodeRestorePermit(const RestorePermit& value);
bool decodeRestorePermit(const QByteArray& bytes, const RestorePermit& expected, RestorePermit& result);
}
