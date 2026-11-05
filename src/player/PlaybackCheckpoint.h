#pragma once

#include "PlaybackIntent.h"
#include <QByteArray>
#include <QString>

// One in-memory, canonical record. Callers must supply verified logical IDs;
// syntax checks cannot establish that arbitrary strings are not credentials.
struct PlaybackCheckpoint
{
  PlaybackIntent::Snapshot intent;
  QString server, account, item, source;
  bool positionKnown = false;
  std::int64_t positionMs = 0;
  bool tracksKnown = false;
  QString trackSource, audioTrack, subtitleTrack;
  bool queueIdentityKnown = false;
  QString queueIdentity;
  QString historicalSession;
};

class PlaybackCheckpointCodec
{
public:
  static constexpr int MaxBytes = 8192;
  // Failure leaves the output untouched and never includes input in an error.
  static bool encode(const PlaybackCheckpoint& value, QByteArray& output);
  static bool decode(const QByteArray& input, PlaybackCheckpoint& output);
};
