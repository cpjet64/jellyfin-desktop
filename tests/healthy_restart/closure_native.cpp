#include "closure_native.h"
#include <QThread>
#include <mpv/client.h>
#include <exception>
#include <limits>
#include <stdexcept>

namespace HealthyRetirement {
NativeTeardownState::NativeTeardownState(QThread* worker, mpv_handle* core, QThread* gui)
  : worker_(worker), core_(core), gui_(gui) {}
NativeTeardownState::~NativeTeardownState()
{
  if (worker_ || core_) std::terminate();
}
void NativeTeardownState::quit()
{
  if (QThread::currentThread() != gui_ || !worker_) throw std::logic_error("invalid worker owner");
  worker_->quit();
}
bool NativeTeardownState::wait(std::int64_t remainingNanoseconds)
{
  if (QThread::currentThread() == gui_ || !worker_ || remainingNanoseconds <= 0) return false;
  const auto ms = static_cast<std::uint64_t>(remainingNanoseconds / 1000000);
  const auto limit = static_cast<std::uint64_t>(std::numeric_limits<unsigned long>::max() - 1);
  const bool joined = worker_->wait(static_cast<unsigned long>(ms < limit ? ms : limit));
  if (joined) joined_.store(true);
  return joined;
}
void NativeTeardownState::destroy()
{
  if (QThread::currentThread() == gui_ || !joined_.load() || destroying_.exchange(true) || !core_)
    throw std::logic_error("invalid core destruction");
  mpv_terminate_destroy(core_);
  destroyed_.store(true);
}
bool NativeTeardownState::finalize()
{
  if (QThread::currentThread() != gui_ || !joined_.load() || !destroyed_.load()) return false;
  delete worker_;
  worker_ = nullptr;
  core_ = nullptr;
  return true;
}
}
