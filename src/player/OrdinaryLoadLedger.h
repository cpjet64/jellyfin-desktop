#pragma once
#include <cstdint>
#include <map>

// Tracks native load submissions without guessing which of several starts belongs to which command.
class OrdinaryLoadLedger {
public:
  bool admitted(std::uint64_t userdata);
  bool started();
  void reply(std::uint64_t userdata, bool success);
  bool available() const;
  void failClosed();
  void clear();
private:
  struct Load { bool started = false, replied = false; };
  std::map<std::uint64_t, Load> loads_;
  bool ambiguous_ = false;
};
