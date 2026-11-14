#pragma once
#include "AutomaticRecoverySession.h"
#include <cstdint>
#include <functional>
#include <mpv/client.h>

// The caller owns the admitted core lease and validates intent before dispatch.
namespace AutomaticRecoveryReload {
using Dispatch = std::function<int(std::uint64_t, mpv_node*)>;
bool submit(const AutomaticRecoverySession::Snapshot& snapshot, std::uint64_t userdata, const Dispatch& dispatch);
}
