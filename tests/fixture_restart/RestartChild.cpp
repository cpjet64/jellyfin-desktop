#include "RestartChild.h"
#include <utility>

namespace FixtureRestart {
RestartChild::RestartChild(ChildGate& gate, const void* grantChannel, const void* controlChannel,
                           Grant binding, Actions actions)
  : gate_(gate), grantChannel_(grantChannel), controlChannel_(controlChannel),
    binding_(std::move(binding)), actions_(std::move(actions)), currentRevision_(binding_.revision)
{
}

bool RestartChild::grant(const void* channel, const QByteArray& frame, qint64 now, bool eof)
{
  if (canceled_ || granted_ || !grantChannel_ || channel != grantChannel_ || !actions_.arm ||
      !gate_.accept(channel, frame, binding_, now, eof)) return false;
  granted_ = actions_.arm();
  return granted_ && !canceled_ && gate_.allowed(binding_.revision);
}

bool RestartChild::control(const void* channel, const QByteArray& request, QByteArray& response)
{
  Control control;
  if (!controlChannel_ || channel != controlChannel_ || canceled_ || !decodeControl(request, control) ||
      (control.kind != ControlKind::UserStop && control.kind != ControlKind::NewPlay) ||
      control.sequence <= lastSequence_ || control.revision <= currentRevision_ || !actions_.cancel) return false;
  canceled_ = true;
  lastSequence_ = control.sequence;
  currentRevision_ = control.revision;
  if (!actions_.cancel()) return false;
  const QByteArray ack = encodeControl({ControlKind::Ack, control.sequence, control.revision});
  if (ack.isEmpty()) return false;
  response = ack;
  return true;
}

bool RestartChild::allowed() const
{
  return granted_ && !canceled_ && gate_.allowed(binding_.revision);
}

bool RestartChild::load() { return allowed() && actions_.load && actions_.load() && allowed(); }
bool RestartChild::seek() { return allowed() && actions_.seek && actions_.seek() && allowed(); }
}
