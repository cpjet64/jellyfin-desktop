#include <QtTest/QtTest>
#include "ui/WindowManager.h"
#include "settings/SettingsComponent.h"
#include "settings/SettingsSection.h"

class TestWindowManager : public QObject
{
  Q_OBJECT
private slots:
  void initTestCase();
  void restoredSizeSurvivesUnchangedSave();
  void fullscreenTransitionsNotifyOnce();
  void nonWindowedGeometryIsPreserved();
};

void TestWindowManager::initTestCase()
{
  auto& settings = SettingsComponent::Get();
  auto* state = new SettingsSection(SETTINGS_SECTION_STATE, PLATFORM_ANY, -1, &settings);
  settings.registerSection(state);
  auto* main = new SettingsSection(SETTINGS_SECTION_MAIN, PLATFORM_ANY, -1, &settings);
  main->registerSetting(new SettingsValue("fullscreen", false));
  main->registerSetting(new SettingsValue("forceAlwaysFS", false));
  settings.registerSection(main);
}

void TestWindowManager::restoredSizeSurvivesUnchangedSave()
{
  QQuickWindow window;
  WindowManager manager;
  manager.m_window = &window;
  auto* state = SettingsComponent::Get().getSection(SETTINGS_SECTION_STATE);
  state->setValue(manager.sizeWidthKey(), 800);
  state->setValue(manager.sizeHeightKey(), 600);
  state->setValue(manager.positionXKey(), 20);
  state->setValue(manager.positionYKey(), 20);
  manager.loadGeometry();
  QCOMPARE(window.size(), QSize(800, 600));
  manager.saveWindowSize();
  QCOMPARE(state->value(manager.sizeWidthKey()).toInt(), 800);
  QCOMPARE(state->value(manager.sizeHeightKey()).toInt(), 600);
  QCOMPARE(manager.loadGeometryRect().size(), QSize(800, 600));
}

void TestWindowManager::fullscreenTransitionsNotifyOnce()
{
  QQuickWindow window;
  WindowManager manager;
  manager.m_window = &window;
  QSignalSpy spy(&manager, &WindowManager::fullScreenSwitched);
  auto* main = SettingsComponent::Get().getSection(SETTINGS_SECTION_MAIN);
  manager.onVisibilityChanged(QWindow::Maximized);
  manager.onVisibilityChanged(QWindow::FullScreen);
  manager.onVisibilityChanged(QWindow::FullScreen);
  QCOMPARE(spy.count(), 1);
  QVERIFY(main->value("fullscreen").toBool());
  manager.onVisibilityChanged(QWindow::Windowed);
  QCOMPARE(spy.count(), 2);
  QVERIFY(!main->value("fullscreen").toBool());
}

void TestWindowManager::nonWindowedGeometryIsPreserved()
{
  QQuickWindow window;
  WindowManager manager;
  manager.m_window = &window;
  auto* state = SettingsComponent::Get().getSection(SETTINGS_SECTION_STATE);
  state->setValue(manager.sizeWidthKey(), 800);
  state->setValue(manager.sizeHeightKey(), 600);
  state->setValue(manager.positionXKey(), 30);
  state->setValue(manager.positionYKey(), 40);
  state->setValue(manager.maximizedKey(), true);
  for (Qt::WindowState mode : {Qt::WindowFullScreen, Qt::WindowMinimized})
  {
    window.setWindowState(mode);
    manager.saveWindowSize();
    manager.saveWindowPosition();
    QCOMPARE(state->value(manager.sizeWidthKey()).toInt(), 800);
    QCOMPARE(state->value(manager.sizeHeightKey()).toInt(), 600);
    QCOMPARE(state->value(manager.positionXKey()).toInt(), 30);
    QCOMPARE(state->value(manager.positionYKey()).toInt(), 40);
    QVERIFY(state->value(manager.maximizedKey()).toBool());
  }
}

QTEST_MAIN(TestWindowManager)
#include "test_windowmanager.moc"
