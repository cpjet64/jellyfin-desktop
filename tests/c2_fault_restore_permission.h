#pragma once

#include <cmath>
#include <cstdint>
#include <string>
#include <utility>

// Single-writer, fixture-only permission. The caller supplies observed evidence;
// this object never owns an mpv handle or decides that a STOP is recoverable.
class C2FaultRestorePermission
{
public:
  struct Identity {
    std::uintptr_t core = 0, controller = 0;
    std::uint64_t binding = 0, g1 = 0;
    std::int64_t entry = 0;
    std::string path;
    double position = 0;
    bool playing = false;
  };
  struct Proof {
    Identity identity;
    std::uint64_t g2 = 0;
    bool selectedFault = false, internalStop = false, terminal = false;
    bool serviceClosed = false, retired = false, created = false;
  };

  C2FaultRestorePermission(Identity value, std::int64_t expires)
    : identity(std::move(value)), deadline(expires) {}

  bool consume(const Proof& proof, std::int64_t now)
  {
    if (used || cancelled || failed || now <= 0 || now >= deadline || !validIdentity() ||
        !same(proof.identity) || !proof.g2 || proof.g2 == identity.g1 || !proof.selectedFault ||
        !proof.internalStop || !proof.terminal || !proof.serviceClosed || !proof.retired || !proof.created) return false;
    used = true;
    generation2 = proof.g2;
    return true;
  }
  void reloadResult(bool success) { if (used && !reloaded) { reloaded = success; if (!success) failed = true; } }
  void cancel() { cancelled = true; }
  bool mayContinue(const Identity& current, std::uint64_t generation, std::int64_t now) const
  {
    return used && reloaded && !cancelled && !failed && now > 0 && now < deadline && same(current) &&
      generation == generation2;
  }

private:
  bool validIdentity() const
  {
    return identity.core && identity.controller && identity.binding && identity.g1 && identity.entry > 0 &&
      !identity.path.empty() && std::isfinite(identity.position) && identity.position > 0 && identity.playing && deadline > 0;
  }
  bool same(const Identity& value) const
  {
    return value.core == identity.core && value.controller == identity.controller && value.binding == identity.binding &&
      value.g1 == identity.g1 && value.entry == identity.entry && value.path == identity.path &&
      value.position == identity.position && value.playing == identity.playing;
  }
  Identity identity;
  std::int64_t deadline = 0;
  std::uint64_t generation2 = 0;
  bool used = false, reloaded = false, cancelled = false, failed = false;
};
