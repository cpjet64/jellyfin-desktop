#include <QtTest/QtTest>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <MpvController>
#include <atomic>
#include <mpv/client.h>

namespace {
struct ControllerFixture {
  ControllerFixture() { controller.init(); }
  explicit ControllerFixture(MpvController::EventMode mode) : controller(mode) { controller.init(); }
  ~ControllerFixture()
  {
    mpv_set_wakeup_callback(controller.mpv(), nullptr, nullptr);
    mpv_terminate_destroy(controller.mpv());
  }
  MpvController controller;
};

void countWake(void* context)
{
  static_cast<std::atomic<int>*>(context)->fetch_add(1);
}

void logMpvVersion(mpv_handle* mpv)
{
  char* version = mpv_get_property_string(mpv, "mpv-version");
  qInfo() << "libmpv version:" << (version ? version : "unavailable")
          << "client API (hex):" << QString::number(mpv_client_api_version(), 16);
  if (version) mpv_free(version);
}

bool drainUntilPause(mpv_handle* mpv)
{
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < 3000) {
    mpv_event* event = mpv_wait_event(mpv, 0.05);
    if (event && event->event_id == MPV_EVENT_PROPERTY_CHANGE) {
      const auto* change = static_cast<const mpv_event_property*>(event->data);
      if (change && change->name && QByteArray(change->name) == "pause") {
        while (timer.elapsed() < 3000)
          if (mpv_wait_event(mpv, 0)->event_id == MPV_EVENT_NONE) return true;
        return false;
      }
    }
  }
  return false;
}
}

class TestMpvQtEventOwnership : public QObject
{
  Q_OBJECT
private slots:
  void internalModeRemainsCompatible();
  void externalModeLeavesEventsForOwner();
};

void TestMpvQtEventOwnership::internalModeRemainsCompatible()
{
  ControllerFixture fixture;
  QCOMPARE(fixture.controller.eventMode(), MpvController::EventMode::Internal);
  mpv_handle* mpv = fixture.controller.mpv();
  logMpvVersion(mpv);
  QSignalSpy changes(&fixture.controller, &MpvController::propertyChanged);
  QCOMPARE(mpv_observe_property(mpv, 11, "pause", MPV_FORMAT_FLAG), 0);
  QTRY_VERIFY_WITH_TIMEOUT(changes.count() > 0, 5000);
}

void TestMpvQtEventOwnership::externalModeLeavesEventsForOwner()
{
  std::atomic<int> wakes{0};
  ControllerFixture fixture(MpvController::EventMode::External);
  auto& controller = fixture.controller;
  mpv_handle* mpv = controller.mpv();
  logMpvVersion(mpv);
  QCOMPARE(controller.eventMode(), MpvController::EventMode::External);
  QCOMPARE(mpv_observe_property(mpv, 12, "pause", MPV_FORMAT_FLAG), 0);
  controller.eventHandler(); // Must not drain an externally owned queue.
  mpv_set_wakeup_callback(mpv, countWake, &wakes);
  QVERIFY(drainUntilPause(mpv)); // Includes events queued before callback registration.
  const int beforeChange = wakes.load();
  int paused = 1;
  QCOMPARE(mpv_set_property(mpv, "pause", MPV_FORMAT_FLAG, &paused), 0);
  QTRY_VERIFY_WITH_TIMEOUT(wakes.load() > beforeChange, 5000);
  mpv_set_wakeup_callback(mpv, nullptr, nullptr);
  QTest::qWait(100);
  const int afterDetach = wakes.load();
  paused = 0;
  QCOMPARE(mpv_set_property(mpv, "pause", MPV_FORMAT_FLAG, &paused), 0);
  QTest::qWait(100);
  QCOMPARE(wakes.load(), afterDetach);
}

QTEST_GUILESS_MAIN(TestMpvQtEventOwnership)
#include "test_mpvqt_event_ownership.moc"
