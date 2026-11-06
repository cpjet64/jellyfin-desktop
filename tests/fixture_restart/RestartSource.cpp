#include "RestartSource.h"
#include <utility>

namespace FixtureRestart {
RestartSource::RestartSource(const void* evidenceChannel, const void* controlChannel, quint64 revision,
                             Capture capture, Stop stop)
  : evidenceChannel_(evidenceChannel), controlChannel_(controlChannel), revision_(revision),
    capture_(std::move(capture)), stop_(std::move(stop))
{
}

bool RestartSource::freeze(const void* channel, QByteArray& frame)
{
  if (captureAttempted_ || !evidenceChannel_ || channel != evidenceChannel_ || !capture_ || !revision_) return false;
  captureAttempted_ = true;
  Evidence evidence;
  if (!capture_(evidence) || evidence.grant.revision != revision_) return false;
  const QByteArray encoded = encodeEvidence(evidence);
  if (encoded.isEmpty()) return false;
  evidence_ = evidence;
  frozen_ = true;
  frame = encoded;
  return true;
}

bool RestartSource::control(const void* channel, const QByteArray& request, QByteArray& response)
{
  Control command;
  if (!frozen_ || stopAttempted_ || !controlChannel_ || channel != controlChannel_ || !stop_ ||
      !decodeControl(request, command) || command.kind != ControlKind::InternalStop ||
      command.sequence != 1 || command.revision != revision_) return false;
  stopAttempted_ = true;
  if (!stop_(evidence_)) return false;
  response = encodeControl({ControlKind::Ack, command.sequence, command.revision});
  return !response.isEmpty();
}
}
