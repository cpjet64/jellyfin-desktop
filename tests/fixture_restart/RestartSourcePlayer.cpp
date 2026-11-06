#include "RestartSourcePlayer.h"
#include "FixtureRestartAdapter.h"
#include <utility>

namespace FixtureRestart {
RestartSourcePlayer::RestartSourcePlayer(FixtureRestartAdapter& player, const void* evidenceChannel,
                                         const void* controlChannel, Grant binding)
  : source_(evidenceChannel, controlChannel, binding.revision,
      [&player, binding = std::move(binding)](Evidence& evidence) { return player.captureA(binding, evidence); },
      [&player](const Evidence& evidence) { return player.internalReplacementStop(evidence); })
{
}
}
