#include "PlaybackNativeReceipts.h"

namespace {
bool same(PlaybackNativeReceipts::Key a, PlaybackNativeReceipts::Key b)
{ return a.host == b.host && a.core == b.core && a.revision == b.revision && a.attempt == b.attempt; }
}

bool PlaybackNativeReceipts::begin(Key key) noexcept
{
  if (!key.host || !key.core || !key.revision || !key.attempt || state_.key.attempt) return false;
  state_.key = key;
  return true;
}

bool PlaybackNativeReceipts::consume(Key current) noexcept
{
  if (state_.consumed || state_.invalid || !same(state_.key, current)) return false;
  state_.consumed = true;
  return true;
}

void PlaybackNativeReceipts::submitted(int result) noexcept
{
  if (!state_.consumed || state_.submissionObserved) { invalidate(); return; }
  state_.submissionObserved = true;
  state_.submitted = result >= 0;
  if (result < 0) { state_.settled = true; invalidate(); }
}

void PlaybackNativeReceipts::reply(std::uint64_t attempt, bool ok, std::int64_t entry) noexcept
{
  if (attempt != state_.key.attempt || !state_.submitted) return;
  if (state_.reply || (ok && entry <= 0)) { invalidate(); return; }
  state_.reply = true;
  state_.replyOk = ok;
  state_.entry = ok ? entry : 0;
  if (!ok) { state_.settled = true; invalidate(); return; }
  match();
}

PlaybackNativeReceipts::Event* PlaybackNativeReceipts::event(std::int64_t entry) noexcept
{
  if (entry <= 0 || !state_.submitted) { invalidate(); return nullptr; }
  for (auto& value : events_) if (value.entry == entry) return &value;
  for (auto& value : events_) if (!value.entry) { value.entry = entry; return &value; }
  invalidate();
  return nullptr;
}

void PlaybackNativeReceipts::start(std::int64_t entry) noexcept
{
  if (state_.invalid) return;
  auto* value = event(entry);
  if (!value) return;
  if (value->start) { invalidate(); return; }
  value->start = true;
  match();
}

void PlaybackNativeReceipts::end(std::int64_t entry, bool redirect) noexcept
{
  if (state_.invalid && state_.replyOk && entry != state_.entry) return;
  auto* value = event(entry);
  if (!value) return;
  if (value->end) { invalidate(); return; }
  value->end = true;
  value->redirect = redirect;
  match();
}

void PlaybackNativeReceipts::match() noexcept
{
  if (!state_.replyOk) return;
  for (const auto& value : events_)
    if (value.start && value.entry != state_.entry) invalidate();
  for (const auto& value : events_) if (value.entry == state_.entry) {
    state_.started = value.start;
    state_.ended = value.end;
    state_.settled = value.end;
    if (value.redirect) invalidate();
    return;
  }
}

void PlaybackNativeReceipts::invalidate() noexcept { state_.invalid = true; }
