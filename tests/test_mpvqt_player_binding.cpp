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

namespace {
class ObservableVideoSettings : public SettingsSection
{
public:
  ObservableVideoSettings(const QVariantMap& defaults, QObject* parent)
      : SettingsSection(SETTINGS_SECTION_VIDEO, PLATFORM_ANY, -1, parent)
  {
    for (auto it = defaults.cbegin(); it != defaults.cend(); ++it)
      registerSetting(new SettingsValue(it.key(), it.value()));
  }
  int updateReceiverCount() const { return receivers(SIGNAL(valuesUpdated(QVariantMap))); }
};
}

class TestMpvQtPlayerBinding : public QObject
{
  Q_OBJECT
private slots:
  void rejectsSecondItemAndRebindsAfterRelease();
};

void TestMpvQtPlayerBinding::rejectsSecondItemAndRebindsAfterRelease()
{
  QTemporaryDir temp(QDir::currentPath() + "/mpvqt-binding-XXXXXX");
  QVERIFY(temp.isValid());
  Paths::setConfigDir(temp.path() + "/config");
  Paths::setCacheDir(temp.path() + "/cache");
  QVERIFY(QDir().mkpath(temp.path() + "/config/profiles"));
  Q_INIT_RESOURCE(resources);
  auto& settings = SettingsComponent::Get();
  QVERIFY(settings.componentInitialize());
  auto* originalVideoSettings = settings.getSection(SETTINGS_SECTION_VIDEO);
  QVERIFY(originalVideoSettings);
  const QVariantMap defaults = originalVideoSettings->allValues();
  auto* videoSettings = new ObservableVideoSettings(defaults, &settings);
  QCOMPARE(videoSettings->allValues(), defaults);
  settings.registerSection(videoSettings);
  QCOMPARE(videoSettings->updateReceiverCount(), 0);
  auto& player = PlayerComponent::Get();
  auto first = std::make_unique<MpvVideoItem>();
  auto second = std::make_unique<MpvVideoItem>();
  mpv_handle* secondHandle = second->controller()->mpv();
  QVERIFY(secondHandle);
  int64_t cacheBytes = 1024 * 1024;
  QCOMPARE(mpv_set_property(secondHandle, "demuxer-max-bytes", MPV_FORMAT_INT64, &cacheBytes), 0);
  first->setPlayerComponent(&player);
  QCOMPARE(videoSettings->updateReceiverCount(), 1);
  QCOMPARE(player.requestNativeStop().status, PlayerComponent::NativeStopResult::Status::Accepted);
  second->setPlayerComponent(&player);
  QCOMPARE(mpv_get_property(secondHandle, "demuxer-max-bytes", MPV_FORMAT_INT64, &cacheBytes), 0);
  QCOMPARE(cacheBytes, int64_t(1024 * 1024));
  QVERIFY(!player.tryBindMpvController(second->controller()));
  first.reset();
  QCOMPARE(player.requestNativeStop().status, PlayerComponent::NativeStopResult::Status::Unavailable);
  emit player.onMpvEvents();
  QCoreApplication::processEvents();
  second->setPlayerComponent(&player);
  const int64_t expectedCache = int64_t(videoSettings->value("cache").toInt()) * 1024 * 1024;
  QVERIFY(expectedCache > 1024 * 1024);
  QCOMPARE(mpv_get_property(secondHandle, "demuxer-max-bytes", MPV_FORMAT_INT64, &cacheBytes), 0);
  QCOMPARE(cacheBytes, expectedCache);
  QCOMPARE(videoSettings->updateReceiverCount(), 1);
  cacheBytes = 1024 * 1024;
  QCOMPARE(mpv_set_property(secondHandle, "demuxer-max-bytes", MPV_FORMAT_INT64, &cacheBytes), 0);
  emit videoSettings->valuesUpdated({});
  QCOMPARE(mpv_get_property(secondHandle, "demuxer-max-bytes", MPV_FORMAT_INT64, &cacheBytes), 0);
  QCOMPARE(cacheBytes, expectedCache);
  QCOMPARE(player.requestNativeStop().status, PlayerComponent::NativeStopResult::Status::Accepted);
  second.reset();
  emit player.onMpvEvents();
  QCoreApplication::processEvents();
  QCOMPARE(player.requestNativeStop().status, PlayerComponent::NativeStopResult::Status::Unavailable);
}

int main(int argc, char** argv)
{
  QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
  QGuiApplication app(argc, argv);
  TestMpvQtPlayerBinding test;
  return QTest::qExec(&test, argc, argv);
}

#include "test_mpvqt_player_binding.moc"
