#include "PlayerComponent.h"
#include "display/DisplayComponent.h"
#include "settings/SettingsComponent.h"
#include "settings/SettingsSection.h"
#include <MpvController>
#include <QDebug>
#include <cmath>

void PlayerComponent::setAudioDelay(qint64 milliseconds)
{
  m_playbackAudioDelay = milliseconds;
  if (!m_mpv) return;
  const double displayFps = DisplayComponent::Get().currentRefreshRate();
  const char* key = "audio_delay.normal";
  if (std::fabs(displayFps - 24) < 0.5) key = "audio_delay.24hz";
  else if (std::fabs(displayFps - 25) < 0.5) key = "audio_delay.25hz";
  else if (std::fabs(displayFps - 50) < 0.5) key = "audio_delay.50hz";
  const double fixed = SettingsComponent::Get().value(SETTINGS_SECTION_VIDEO, key).toFloat();
  m_mpv->setProperty("audio-delay", (fixed + m_playbackAudioDelay) / 1000.0);
}

void PlayerComponent::updateAudioDevice()
{
  if (!m_mpv) return;
  QString device = SettingsComponent::Get().value(SETTINGS_SECTION_AUDIO, "device").toString();
  if (!m_audioDevices.contains(device)) {
    qWarning() << "Not using audio device" << device << "because it's not present.";
    device = "auto";
  }
  m_mpv->setProperty("audio-device", device);
}

QString PlayerComponent::configureAudioPassthrough(const QString& deviceType)
{
  m_passthroughCodecs.clear();
  if (deviceType != AUDIO_DEVICE_TYPE_BASIC) {
    SettingsSection* audio = SettingsComponent::Get().getSection(SETTINGS_SECTION_AUDIO);
    QStringList codecs;
    if (deviceType == AUDIO_DEVICE_TYPE_SPDIF) codecs = AudioCodecsSPDIF();
    else if (deviceType == AUDIO_DEVICE_TYPE_HDMI) codecs = AudioCodecsAll();
    for (const QString& key : codecs)
      if (audio->value("passthrough." + key).toBool()) m_passthroughCodecs << key;
    // dts-hd includes dts; listing dts first may disable dts-hd.
    if (m_passthroughCodecs.contains("dts-hd")) m_passthroughCodecs.removeAll("dts");
  }
  const QString codecs = m_passthroughCodecs.join(",");
  m_mpv->setProperty("audio-spdif", codecs);
  return codecs;
}

void PlayerComponent::userCommand(QString command)
{
  if (!m_mpv) return;
  QByteArray cmdUtf8 = command.toUtf8();
  mpv_command_string(m_mpv->mpv(), cmdUtf8.data());
}
