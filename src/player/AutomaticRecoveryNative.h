#pragma once
#include <cstdint>
#include <mpv/client.h>

// Accept only the documented loadfile result shape; no URL or request data is logged.
namespace AutomaticRecoveryNative {
std::int64_t loadEntry(const mpv_event* event, std::uint64_t expectedUserdata);
std::int64_t playingEntry(const mpv_node& playlist);
}
