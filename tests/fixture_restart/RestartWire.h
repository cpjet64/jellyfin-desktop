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
}
