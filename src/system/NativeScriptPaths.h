#pragma once

#include <QStringList>

namespace {
inline QStringList nativeScriptPaths()
{
  return {
    ":/qtwebchannel/qwebchannel.js",
    ":/web-client/extension/playbackErrorPrompt.js",
    ":/web-client/extension/mpvVideoPlayer.js",
    ":/web-client/extension/mpvAudioPlayer.js",
    ":/web-client/extension/inputPlugin.js",
    ":/web-client/extension/updatePlugin.js",
    ":/web-client/extension/connectivityHelper.js",
    ":/web-client/extension/nativeshell.js"
  };
}
}
