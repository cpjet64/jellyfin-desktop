#include <QtTest/QtTest>
#include <QDir>
#include <QGuiApplication>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QTemporaryDir>
#include <MpvController>
#include <mpv/client.h>
#include <memory>

#include "player/MpvVideoItem.h"
#include "player/PlayerComponent.h"
#include "settings/SettingsComponent.h"
#include "settings/SettingsSection.h"
#include "shared/Paths.h"
#include "mpv_test_bootstrap.h"
#include "mpv_test_profile.h"

class ObservableOtherSettings : public SettingsSection
{
public:
  ObservableOtherSettings(const QVariantMap& defaults, QObject* parent)
      : SettingsSection(SETTINGS_SECTION_OTHER, PLATFORM_ANY, -1, parent)
  {
    for (auto it = defaults.cbegin(); it != defaults.cend(); ++it)
      registerSetting(new SettingsValue(it.key(), it.value()));
  }
  int updateReceiverCount() const { return receivers(SIGNAL(valuesUpdated(QVariantMap))); }
};

class TestPlayerReleasedState : public QObject
{
  Q_OBJECT
private slots:
  void releasedStateAndExplicitRebind();
private:
  void changeReleasedSettings(PlayerComponent& player, SettingsSection* audio,
                              SettingsSection* subtitles, SettingsSection* other, mpv_handle* oldCore);
};

void TestPlayerReleasedState::releasedStateAndExplicitRebind()
{
  QTemporaryDir temp(QDir::currentPath() + "/player-released-XXXXXX");
  QVERIFY(temp.isValid());
  QString error;
  QVERIFY2(prepareMpvTestProfile(temp.path(), &error), qPrintable(error));
  Q_INIT_RESOURCE(resources);
  auto& settings = SettingsComponent::Get();
  QVERIFY(settings.componentInitialize());
  auto* audio = settings.getSection(SETTINGS_SECTION_AUDIO);
  auto* subtitles = settings.getSection(SETTINGS_SECTION_SUBTITLES);
  auto* originalOther = settings.getSection(SETTINGS_SECTION_OTHER);
  QVERIFY(audio && subtitles && originalOther);
  const QVariantMap otherDefaults = originalOther->allValues();
  auto* otherSettings = new ObservableOtherSettings(otherDefaults, &settings);
  QCOMPARE(otherSettings->allValues(), otherDefaults);
  settings.registerSection(otherSettings);

  int restorations = 0;
  PlayerComponent other;
  PlayerComponent player;
  player.m_restoreDisplayAction = [&restorations] { ++restorations; };
  auto first = std::make_unique<MpvVideoItem>();
  first->setPlayerComponent(&player);
  QCOMPARE(otherSettings->updateReceiverCount(), 1);
  mpv_handle* oldCore = first->controller()->mpv();
  QVERIFY(oldCore);
  QCOMPARE(player.requestNativeStop().status, PlayerComponent::NativeStopResult::Status::Accepted);
  first->setPlayerComponent(&other);
  const bool ownerPreserved = first->m_player.data() == &player;
  if (!ownerPreserved) first->m_player = &player; // Keep failure teardown safe.
  QVERIFY(ownerPreserved);

  double oldDelay = 8.0;
  QCOMPARE(mpv_set_property(oldCore, "audio-delay", MPV_FORMAT_DOUBLE, &oldDelay), 0);
  double oldScale = 2.0;
  QCOMPARE(mpv_set_property(oldCore, "sub-scale", MPV_FORMAT_DOUBLE, &oldScale), 0);
  player.m_reloadAudioTimer.start(0);
  player.m_restoreDisplayTimer.start(0);
  player.releaseMpvController(first->controller());
  QVERIFY(!player.m_reloadAudioTimer.isActive());
  QVERIFY(player.m_restoreDisplayTimer.isActive());
  QTRY_COMPARE_WITH_TIMEOUT(restorations, 1, 5000);
  player.onRestoreDisplay();
  QCOMPARE(restorations, 2);

  changeReleasedSettings(player, audio, subtitles, otherSettings, oldCore);
  if (QTest::currentTestFailed()) return;
  double observed = 0;
  QCOMPARE(mpv_get_property(oldCore, "audio-delay", MPV_FORMAT_DOUBLE, &observed), 0);
  QCOMPARE(observed, oldDelay);
  QCOMPARE(mpv_get_property(oldCore, "sub-scale", MPV_FORMAT_DOUBLE, &observed), 0);
  QCOMPARE(observed, oldScale);

  auto second = std::make_unique<MpvVideoItem>();
  second->setPlayerComponent(&player);
  QCOMPARE(otherSettings->updateReceiverCount(), 1);
  mpv_handle* newCore = second->controller()->mpv();
  QVERIFY(newCore);
  QCOMPARE(player.requestNativeStop().status, PlayerComponent::NativeStopResult::Status::Accepted);
  QCOMPARE(mpv_get_property(newCore, "audio-delay", MPV_FORMAT_DOUBLE, &observed), 0);
  QVERIFY(qAbs(observed - 1.234) < 0.001);
  QCOMPARE(mpv_get_property(newCore, "sub-scale", MPV_FORMAT_DOUBLE, &observed), 0);
  QVERIFY(qAbs(observed - 1.25) < 0.001);
  otherSettings->setValueNoSave("other_conf", QStringLiteral("sub-scale=1.5"));
  QCOMPARE(otherSettings->updateReceiverCount(), 1);
  QCOMPARE(mpv_get_property(newCore, "sub-scale", MPV_FORMAT_DOUBLE, &observed), 0);
  QVERIFY(qAbs(observed - 1.5) < 0.001);
  QCOMPARE(mpv_get_property(oldCore, "audio-delay", MPV_FORMAT_DOUBLE, &observed), 0);
  QCOMPARE(observed, oldDelay);
  QCOMPARE(mpv_get_property(oldCore, "sub-scale", MPV_FORMAT_DOUBLE, &observed), 0);
  QCOMPARE(observed, oldScale);
}

void TestPlayerReleasedState::changeReleasedSettings(PlayerComponent& player, SettingsSection* audio,
                                                      SettingsSection* subtitles, SettingsSection* other,
                                                      mpv_handle* oldCore)
{
  audio->setValueNoSave("normalize", !audio->value("normalize").toBool());
  subtitles->setValueNoSave("font", QStringLiteral("RecoveryFixtureFont"));
  other->setValueNoSave("other_conf", QStringLiteral("sub-scale=1.25"));
  player.updateAudioDevice();
  player.setAudioConfiguration();
  player.setSubtitleConfiguration();
  player.userCommand(QStringLiteral("set audio-delay 11"));
  player.setAudioDelay(1234);
  QVERIFY(player.videoInformation().isEmpty());
  double delay = 0;
  QCOMPARE(mpv_get_property(oldCore, "audio-delay", MPV_FORMAT_DOUBLE, &delay), 0);
  QCOMPARE(delay, 8.0);
}

int main(int argc, char** argv)
{
  QCoreApplication::setAttribute(Qt::AA_DisableShaderDiskCache);
  QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
  QGuiApplication app(argc, argv);
  QTemporaryDir scratch(QDir::currentPath() + "/mpv-bootstrap-XXXXXX");
  QString error;
  const QString root = qEnvironmentVariable("JELLYFIN_TEST_OWNED_ROOT", scratch.path());
  if (!scratch.isValid() || !MpvTestBootstrap::prepareMpvTestEnvironment(root, &error)) {
    qCritical().noquote() << "Mpv test bootstrap failed:" << error;
    return 1;
  }
  TestPlayerReleasedState test;
  return QTest::qExec(&test, argc, argv);
}

#include "test_player_released_state.moc"
