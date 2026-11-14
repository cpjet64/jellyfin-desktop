#include "OrdinaryLoadLedger.h"

bool OrdinaryLoadLedger::admitted(std::uint64_t userdata)
{
  if (!userdata || ambiguous_ || loads_.size() >= 64 || loads_.count(userdata)) {
    ambiguous_ = true; loads_.clear(); return false;
  }
  loads_.emplace(userdata, Load{}); return true;
}
bool OrdinaryLoadLedger::started()
{
  if (ambiguous_) return false;
  auto candidate = loads_.end();
  for (auto it = loads_.begin(); it != loads_.end(); ++it) {
    if (it->second.started) continue;
    if (candidate != loads_.end()) { ambiguous_ = true; loads_.clear(); return false; }
    candidate = it;
  }
  if (candidate == loads_.end()) return false;
  candidate->second.started = true;
  if (candidate->second.replied) loads_.erase(candidate);
  return true;
}
void OrdinaryLoadLedger::reply(std::uint64_t userdata, bool success)
{
  const auto it = loads_.find(userdata);
  if (it == loads_.end()) return;
  if (it->second.replied) { ambiguous_ = true; loads_.clear(); return; }
  if (!success || it->second.started) { loads_.erase(it); return; }
  it->second.replied = true;
}
bool OrdinaryLoadLedger::available() const
{
  if (ambiguous_) return false;
  for (const auto& load : loads_) if (!load.second.started) return false;
  return true;
}
void OrdinaryLoadLedger::failClosed() { ambiguous_ = true; loads_.clear(); }
void OrdinaryLoadLedger::clear() { loads_.clear(); ambiguous_ = false; }
