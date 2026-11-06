#include "WindowsRestartParent.h"
#ifdef _WIN32
#include "RestartWire.h"
#include <utility>

namespace FixtureRestart {
WindowsRestartParent::WindowsRestartParent(WindowsBrokerOperations& operations, WindowsEndpoint evidence, Grant binding)
  : operations_(operations), evidence_(evidence), binding_(std::move(binding)),
    broker_(operations, evidence.channel, binding_)
{
  if (evidence.io && evidence.channel) reader_ = std::make_unique<WindowsFrameReader>(*evidence.io, evidence.channel->parentHandle());
}

bool WindowsRestartParent::begin()
{
  if (state_ != State::Idle) return false;
  if (!reader_ || !operations_.ownsAChannel(evidence_.channel)) {
    fail(Failure::Channel); return false;
  }
  state_ = State::Reading;
  finishRead(reader_->begin(ULONGLONG(broker_.normalDeadline())));
  return state_ == State::Reading || state_ == State::Replacing;
}

void WindowsRestartParent::fail(Failure failure)
{
  if (failure_ == Failure::None) failure_ = failure;
  broker_.cancel();
  state_ = State::Cleaning;
}

void WindowsRestartParent::finishRead(IoProgress progress)
{
  if (progress == IoProgress::Pending) return;
  if (progress != IoProgress::Complete || !reader_->completedFrame()) { fail(Failure::Frame); return; }
  Evidence evidence;
  if (!decodeEvidence(*reader_->completedFrame(), binding_, true, evidence) ||
      !broker_.capture(evidence_.channel, evidence)) { fail(Failure::Evidence); return; }
  if (!broker_.startReplacement()) { fail(Failure::Replacement); return; }
  state_ = State::Replacing;
}

void WindowsRestartParent::step()
{
  if (state_ == State::Reading) { finishRead(reader_->poll()); return; }
  if (state_ != State::Replacing && state_ != State::Cleaning) return;
  broker_.step();
  if (state_ == State::Replacing && broker_.completed()) state_ = State::Transferred;
  else if (broker_.unconfirmed()) state_ = State::Unconfirmed;
  else if (broker_.failed()) {
    if (failure_ == Failure::None) failure_ = Failure::Replacement;
    state_ = State::Failed;
  }
}

Broker::Cancellation WindowsRestartParent::cancel()
{
  if (state_ == State::Cleaning || state_ == State::Failed || state_ == State::Unconfirmed)
    return broker_.cancellation();
  if (state_ == State::Reading) reader_->cancel();
  if (failure_ == Failure::None) failure_ = Failure::Canceled;
  state_ = State::Cleaning;
  return broker_.cancel();
}
}
#endif
