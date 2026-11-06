#include "RestartChild.h"
#include <QtEndian>
#include <utility>

namespace FixtureRestart {
RestartChild::RestartChild(ChildGate& gate, const void* grantChannel, const void* controlChannel,
                           Grant binding, Actions actions)
  : gate_(gate), grantChannel_(grantChannel), controlChannel_(controlChannel),
    binding_(std::move(binding)), actions_(std::move(actions)), currentRevision_(binding_.revision)
{
}

RestartChild::RestartChild(ChildGate& gate, const void* grantChannel, const void* controlChannel,
                           Grant binding, quint64 launch, QByteArray expectedDigest, Actions actions)
  : gate_(gate), grantChannel_(grantChannel), controlChannel_(controlChannel),
    binding_(std::move(binding)), launch_(launch), expectedDigest_(std::move(expectedDigest)),
    actions_(std::move(actions)), currentRevision_(binding_.revision)
{
}

RestartChild::RestartChild(ChildGate& gate, const void* grantChannel, const void* controlChannel,
                           quint64 expectedRun, quint64 launch, QByteArray expectedDigest, Actions actions)
  : gate_(gate), grantChannel_(grantChannel), controlChannel_(controlChannel),
    launch_(launch), expectedDigest_(std::move(expectedDigest)), actions_(std::move(actions))
{
  binding_.run = expectedRun;
}

bool RestartChild::grant(const void* channel, const QByteArray& frame, qint64 now, bool eof)
{
  Q_UNUSED(channel); Q_UNUSED(frame); Q_UNUSED(now); Q_UNUSED(eof);
  return false;
}

bool RestartChild::grant(const void* channel, const QByteArray& frame, qint64 now, bool eof,
                         QByteArray& response)
{
  if (!grantChannel_ || channel != grantChannel_) return false;
  if (currentRevision_ != binding_.revision) return false;
  Grant decoded;
  if (!decodeCommittedGrant(frame, binding_.run, expectedDigest_, decoded)) { revoke(); return false; }
  if (!binding_.grant) { binding_ = decoded; currentRevision_ = decoded.revision; }
  if (granted_ || !actions_.arm || !actions_.cancel || !launch_ || expectedDigest_.size() != 32 ||
      frame != encodeGrant(binding_) ||
      !gate_.accept(channel, frame, binding_.run, expectedDigest_, now, eof)) {
    revoke(); return false;
  }
  granted_ = actions_.arm();
  if (!granted_ || !gate_.allowed(binding_.revision)) return false;
  const auto ack = encodeGrantAccepted({binding_.run, launch_, binding_.grant,
                                        binding_.revision, expectedDigest_});
  if (ack.isEmpty() || !gate_.allowed(binding_.revision)) return false;
  response = ack;
  return true;
}

bool RestartChild::control(const void* channel, const QByteArray& request, QByteArray& response)
{
  if (!controlChannel_ || channel != controlChannel_) return false;
  if (request.size() == 80 && request.left(4) == "JRP1") {
    const auto sequence = qFromBigEndian<quint64>(reinterpret_cast<const uchar*>(request.constData() + 40));
    RestorePermit permit, expected{binding_.run, launch_, binding_.grant, binding_.revision,
                                   sequence, expectedDigest_};
    if (!granted_ || permitted_ || sequence != 1 || sequence <= lastSequence_ || !gate_.allowed(binding_.revision) ||
        !decodeRestorePermit(request, expected, permit)) { revoke(); return false; }
    lastSequence_ = sequence;
    permitted_ = true;
    return true;
  }
  Control control;
  if (!decodeControl(request, control) ||
      (control.kind != ControlKind::UserStop && control.kind != ControlKind::NewPlay) ||
      control.sequence <= lastSequence_ || control.revision <= currentRevision_ || !actions_.cancel) {
    revoke(); return false;
  }
  lastSequence_ = control.sequence;
  currentRevision_ = control.revision;
  if (!revoke()) return false;
  const QByteArray ack = encodeControl({ControlKind::Ack, control.sequence, control.revision});
  if (ack.isEmpty()) return false;
  response = ack;
  return true;
}

bool RestartChild::allowed() const
{
  return granted_ && permitted_ && gate_.allowed(binding_.revision);
}

bool RestartChild::revoke()
{
  permitted_ = false;
  if (gate_.canceled()) return false;
  return actions_.cancel && actions_.cancel();
}

bool RestartChild::load() { return allowed() && actions_.load && actions_.load() && allowed(); }
bool RestartChild::seek() { return allowed() && actions_.seek && actions_.seek() && allowed(); }
}
