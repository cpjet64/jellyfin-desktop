#include "PlayerComponent.h"
#include "settings/SettingsComponent.h"
#include "settings/SettingsSection.h"
#include <MpvController>
#include <QDebug>
#include <QThread>

namespace {
void wakePlayer(void* context)
{
  emit static_cast<PlayerComponent*>(context)->onMpvEvents();
}
}

void PlayerComponent::setupMpvEvents()
{
#ifdef MPVQT_EXTERNAL_EVENTS
  if (QThread::currentThread() != thread())
    qFatal("MpvQt event consumer must be initialized on the PlayerComponent thread");
  connect(this, &PlayerComponent::onMpvEvents, this, &PlayerComponent::handleMpvEvents,
          static_cast<Qt::ConnectionType>(Qt::QueuedConnection | Qt::UniqueConnection));
#endif
  mpv_set_wakeup_callback(m_mpv->mpv(), wakePlayer, this);
#ifdef MPVQT_EXTERNAL_EVENTS
  emit onMpvEvents();
#endif
}

void PlayerComponent::finishMpvEventSetup()
{
#ifndef MPVQT_EXTERNAL_EVENTS
  connect(this, &PlayerComponent::onMpvEvents, this, &PlayerComponent::handleMpvEvents, Qt::QueuedConnection);
#endif
  emit onMpvEvents();
}

void PlayerComponent::subscribeMpvSettings()
{
#ifdef MPVQT_EXTERNAL_EVENTS
  constexpr auto connection = Qt::UniqueConnection;
#else
  constexpr auto connection = Qt::AutoConnection;
#endif
  if (auto* section = SettingsComponent::Get().getSection(SETTINGS_SECTION_AUDIO))
    connect(section, &SettingsSection::valuesUpdated, this, &PlayerComponent::updateAudioConfiguration, connection);
  if (auto* section = SettingsComponent::Get().getSection(SETTINGS_SECTION_VIDEO))
    connect(section, &SettingsSection::valuesUpdated, this, &PlayerComponent::updateVideoConfiguration, connection);
  if (auto* section = SettingsComponent::Get().getSection(SETTINGS_SECTION_SUBTITLES))
    connect(section, &SettingsSection::valuesUpdated, this, &PlayerComponent::updateSubtitleConfiguration, connection);
  if (auto* section = SettingsComponent::Get().getSection(SETTINGS_SECTION_OTHER))
    connect(section, &SettingsSection::valuesUpdated, this, &PlayerComponent::updateConfiguration, connection);
}

#ifdef MPVQT_EXTERNAL_EVENTS
bool PlayerComponent::tryBindMpvController(MpvController* controller)
{
  if (QThread::currentThread() != thread())
    qFatal("MpvQt event consumer must be bound on the PlayerComponent thread");
  if (!controller || m_mpv) return false;
  m_mpv = controller;
  ++m_mpvGeneration;
  return true;
}

void PlayerComponent::releaseMpvController(MpvController* controller)
{
  if (QThread::currentThread() != thread())
    qFatal("MpvQt event consumer must be released on the PlayerComponent thread");
  if (m_mpv != controller) return;
  m_reloadAudioTimer.stop();
  mpv_set_wakeup_callback(controller->mpv(), nullptr, nullptr);
  m_mpv = nullptr;
  ++m_mpvGeneration;
}
#endif

void PlayerComponent::scheduleMpvHook(const std::function<void()>& resume)
{
  if (!switchDisplayFrameRate()) {
    resume();
    return;
  }
  const int pause = SettingsComponent::Get().value(SETTINGS_SECTION_VIDEO, "refreshrate.delay").toInt() * 1000;
  qInfo() << "waiting" << pause << "msec after rate switch before loading";
#ifdef MPVQT_EXTERNAL_EVENTS
  QTimer::singleShot(pause, this, resume);
#else
  QTimer::singleShot(pause, resume);
#endif
}

void PlayerComponent::scheduleMpvHook(const QString& resumeId)
{
#ifdef MPVQT_EXTERNAL_EVENTS
  const auto generation = m_mpvGeneration;
#endif
  scheduleMpvHook(std::function<void()>([=] {
#ifdef MPVQT_EXTERNAL_EVENTS
    if (!m_mpv || m_mpvGeneration != generation) return;
#endif
    qInfo() << "resuming loading";
    m_mpv->command(QStringList() << "hook-ack" << resumeId);
  }));
}

#if MPV_CLIENT_API_VERSION >= MPV_MAKE_VERSION(1, 100)
void PlayerComponent::scheduleMpvHook(uint64_t id)
{
#ifdef MPVQT_EXTERNAL_EVENTS
  const auto generation = m_mpvGeneration;
#endif
  scheduleMpvHook(std::function<void()>([=] {
#ifdef MPVQT_EXTERNAL_EVENTS
    if (!m_mpv || m_mpvGeneration != generation) return;
#endif
    qInfo() << "resuming loading";
    mpv_hook_continue(m_mpv->mpv(), id);
  }));
}
#endif
