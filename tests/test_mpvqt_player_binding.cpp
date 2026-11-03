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
#include "shared/Paths.h"

namespace {
QString forceWindow(mpv_handle* handle)
{
  char* value = mpv_get_property_string(handle, "force-window");
  const QString result = value ? QString::fromUtf8(value) : QString();
  if (value) mpv_free(value);
  return result;
}
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
  Q_INIT_RESOURCE(resources);
  QVERIFY(SettingsComponent::Get().componentInitialize());
  auto& player = PlayerComponent::Get();
  auto first = std::make_unique<MpvVideoItem>();
  auto second = std::make_unique<MpvVideoItem>();
  mpv_handle* secondHandle = second->controller()->mpv();
  QVERIFY(secondHandle);
  QCOMPARE(mpv_set_property_string(secondHandle, "force-window", "no"), 0);
  first->setPlayerComponent(&player);
  QCOMPARE(player.requestNativeStop().status, PlayerComponent::NativeStopResult::Status::Accepted);
  second->setPlayerComponent(&player);
  QCOMPARE(forceWindow(secondHandle), QStringLiteral("no"));
  QVERIFY(!player.tryBindMpvController(second->controller()));
  first.reset();
  QCOMPARE(player.requestNativeStop().status, PlayerComponent::NativeStopResult::Status::Unavailable);
  emit player.onMpvEvents();
  QCoreApplication::processEvents();
  second->setPlayerComponent(&player);
  QCOMPARE(forceWindow(secondHandle), QStringLiteral("yes"));
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
