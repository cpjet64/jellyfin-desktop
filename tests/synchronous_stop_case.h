#pragma once

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonObject>
#include <QThread>
#include "c2_event_observer.h"
#include "player/PlayerIntentObservation.h"
#include "synchronous_stop_events.h"
#include "synchronous_stop_trace.h"

// SOURCE ONLY: ordinary QQuickView rendering, synchronous GUI-thread mpv
// calls and view destruction have not been qualified together. Keep this
// scenario held until its render/context ownership and teardown are reviewed.
// Current route: Qt chooses the QQuickView render loop; GUI control calls use
// synchronous mpv commands and grabWindow. The view owns the video item; the
// item releases its binding before base teardown in external-events builds.
// Neither worker/context affinity nor teardown order is established here.
using IntentTrace = PlayerIntentObservation;

struct SynchronousStopTrace
{
  explicit SynchronousStopTrace(const QString& directory) : path(directory + "/synchronous-stop.json")
  { data.insert("status", "incomplete"); }
  ~SynchronousStopTrace()
  {
    try {
    const auto snapshot = IntentTrace::snapshot();
    QJsonArray events;
    for (std::size_t i = 0; i < snapshot.count; ++i) {
      const auto& event = snapshot.events[i];
      events.append(QJsonObject{{"sequence", qint64(event.sequence)}, {"invocation", qint64(event.invocation)},
        {"parent", qint64(event.parent)}, {"kind", int(event.kind)}, {"outcome", int(event.outcome)},
        {"error", event.error}});
    }
    data.insert("observer_healthy", snapshot.healthy);
    data.insert("observer_events", events);
    if (!persistSynchronousTraceFile(path, QJsonDocument(data).toJson()))
      QTest::qFail("Cannot preserve complete synchronous Stop trace", __FILE__, __LINE__);
    } catch (...) {
      QTest::qFail("Synchronous Stop trace preservation raised an exception", __FILE__, __LINE__);
    }
  }
  QString path;
  QJsonObject data;
};

class SynchronousMpvEvents
{
public:
  explicit SynchronousMpvEvents(mpv_handle* primary) : client(mpv_create_client(primary, "synchronous-stop-observer")) {}
  ~SynchronousMpvEvents() { if (client) mpv_destroy(client); }
  void drain()
  {
    for (int i = 0; i < 4096 && client; ++i) {
      const mpv_event* event = mpv_wait_event(client, 0);
      if (!event || event->event_id == MPV_EVENT_NONE) return;
      if (events.size() == 4096 || event->event_id == MPV_EVENT_QUEUE_OVERFLOW) { healthy = false; return; }
      C2EventObserver::Event copy{event->event_id};
      copy.error = event->error;
      if (event->event_id == MPV_EVENT_START_FILE && event->data)
        copy.playlistId = static_cast<const mpv_event_start_file*>(event->data)->playlist_entry_id;
      if (event->event_id == MPV_EVENT_END_FILE && event->data) {
        const auto* end = static_cast<const mpv_event_end_file*>(event->data);
        copy.playlistId = end->playlist_entry_id; copy.reason = end->reason; copy.error = end->error;
      }
      events.push_back(copy);
    }
    healthy = false;
  }
  int64_t latestLoadedEntry() const
  {
    int64_t pending = 0, loaded = 0;
    for (const auto& event : events) {
      if (event.kind == MPV_EVENT_START_FILE) pending = event.playlistId;
      if (event.kind == MPV_EVENT_FILE_LOADED) loaded = event.error == 0 ? pending : 0;
    }
    return loaded;
  }
  mpv_handle* client = nullptr;
  std::vector<C2EventObserver::Event> events;
  bool healthy = true;
};

struct SynchronousMpvTrace
{
  SynchronousMpvTrace(SynchronousStopTrace& target, SynchronousMpvEvents& source) : trace(target), observer(source) {}
  ~SynchronousMpvTrace()
  {
    QJsonArray events;
    for (const auto& event : observer.events)
      events.append(QJsonObject{{"kind", int(event.kind)}, {"entry", qint64(event.playlistId)},
                                {"reason", event.reason}, {"error", event.error}});
    trace.data.insert("mpv_events", events);
    trace.data.insert("mpv_observer_healthy", observer.healthy);
  }
  SynchronousStopTrace& trace;
  SynchronousMpvEvents& observer;
};

static bool withinOwnedRoot(const QString& path, const QString& root)
{
  const QString relative = QDir(root).relativeFilePath(path);
  return !path.isEmpty() && relative != ".." && !relative.startsWith("../") && !QDir::isAbsolutePath(relative);
}

static QByteArray fileDigest(const QString& path)
{
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) return {};
  QCryptographicHash hash(QCryptographicHash::Sha256);
  return hash.addData(&file) ? hash.result().toHex() : QByteArray{};
}

static bool saveNewFrame(const QImage& image, const QString& path)
{
  QFile file(path);
  return file.open(QIODevice::WriteOnly | QIODevice::NewOnly) && image.save(&file, "PNG") && file.flush();
}

static void evaluateFixture(QObject* root, const QString& code)
{
  QVERIFY(QMetaObject::invokeMethod(root, "evaluate", Q_ARG(QVariant, QVariant(code))));
  QTRY_VERIFY_WITH_TIMEOUT(root->property("result").toString() != "pending", 10000);
}

static void installDispatchPage(QObject* root, SynchronousStopTrace& trace)
{
  const QString base = QStringLiteral(TEST_SOURCE_DIR "/../native/");
  const QStringList files{base + "PlaybackIntent.js", base + "PlaybackStreams.js", base + "playbackErrorPrompt.js", base + "mpvVideoPlayer.js",
                          QStringLiteral(TEST_SOURCE_DIR "/synchronous_stop_page.js")};
  QJsonArray hashes;
  for (const auto& path : files) {
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray source = file.readAll();
    QVERIFY(!source.isEmpty() && source.size() <= 256 * 1024);
    hashes.append(QJsonObject{{"file", QFileInfo(path).fileName()},
                              {"sha256", QString::fromLatin1(QCryptographicHash::hash(source, QCryptographicHash::Sha256).toHex())}});
    evaluateFixture(root, QString::fromUtf8(source) + "\n;true;");
    QVERIFY(!QTest::currentTestFailed());
    QCOMPARE(root->property("result").toString(), QStringLiteral("true"));
  }
  trace.data.insert("scripts", hashes);
}

static bool matchingDispatch(const IntentTrace::Snapshot& snapshot)
{
  using K = IntentTrace::Kind;
  using O = IntentTrace::Outcome;
  if (!snapshot.healthy || snapshot.count != 9) return false;
  const auto& e = snapshot.events;
  for (std::size_t i = 0; i < snapshot.count; ++i) if (e[i].sequence != i + 1) return false;
  return e[0].kind == K::LoadEnter && e[0].parent == 0 &&
      e[1].kind == K::StopEnter && e[1].parent == e[0].invocation &&
      e[2].kind == K::StopResult && e[2].invocation == e[1].invocation && e[2].outcome == O::Accepted && e[2].error >= 0 &&
      e[3].kind == K::Exit && e[3].invocation == e[1].invocation &&
      e[4].kind == K::QueueDispatch && e[4].invocation == e[0].invocation &&
      e[5].kind == K::Exit && e[5].invocation == e[0].invocation &&
      e[6].kind == K::StopEnter && e[6].parent == 0 && e[6].invocation != e[1].invocation &&
      e[7].kind == K::StopResult && e[7].invocation == e[6].invocation && e[7].outcome == O::Accepted && e[7].error >= 0 &&
      e[8].kind == K::Exit && e[8].invocation == e[6].invocation;
}

static StopEventState terminalState(const SynchronousMpvEvents& observer, std::size_t boundary, int64_t aEntry)
{
  if (!observer.healthy) return StopEventState::Invalid;
  std::vector<StopEvent> copied;
  copied.reserve(observer.events.size());
  for (const auto& event : observer.events) {
    const auto kind = event.kind == MPV_EVENT_START_FILE ? StopEventKind::Start :
      event.kind == MPV_EVENT_END_FILE ? StopEventKind::End :
      event.kind == MPV_EVENT_QUEUE_OVERFLOW ? StopEventKind::Overflow : StopEventKind::Other;
    copied.push_back({kind, event.playlistId});
  }
  return classifyStopEvents(copied, boundary, aEntry);
}

static void awaitStartedEntryTerminal(SynchronousMpvEvents& observer, mpv_handle* primary,
                                      std::size_t boundary, int64_t aEntry)
{
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < 10000) {
    observer.drain();
    const auto state = terminalState(observer, boundary, aEntry);
    QVERIFY(state != StopEventState::Invalid);
    QVERIFY(idle(primary) && matchingDispatch(IntentTrace::snapshot()));
    if (state == StopEventState::Settled) return;
    QTest::qWait(20);
  }
  QFAIL("post-boundary started entry has no matching terminal event");
}

static void waitPageSettled(QObject* root)
{
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < 10000) {
    evaluateFixture(root, "fixtureDispatchState()");
    if (QTest::currentTestFailed()) return;
    const auto state = QJsonDocument::fromJson(root->property("result").toString().toUtf8()).object();
    QVERIFY(state.value("errors").isArray() && state.value("errors").toArray().isEmpty());
    QVERIFY(state.value("overflow").isBool() && !state.value("overflow").toBool());
    if (state.value("pending").isDouble() && state.value("pending").toInt() == 0) return;
    QTest::qWait(20);
  }
  QFAIL("web player callbacks did not settle");
}

static void provePlayingA(QObject* root, QQuickView& view, PlayerComponent& player, mpv_handle* primary,
                          const QString& a, const QString& evidence, SynchronousMpvEvents& observer,
                          SynchronousStopTrace& trace)
{
  evaluateFixture(root, "fixtureStartA(" + jsonString(QUrl::fromLocalFile(a).toString()) + ")");
  QCOMPARE(root->property("result").toString(), QStringLiteral("sent"));
  waitPageSettled(root);
  QVERIFY(!QTest::currentTestFailed());
  QTRY_VERIFY_WITH_TIMEOUT((observer.drain(), observer.latestLoadedEntry() > 0), 10000);
  QVERIFY(observer.healthy);
  char* rawPath = mpv_get_property_string(primary, "path");
  QVERIFY(rawPath);
  const QString path = QString::fromUtf8(rawPath);
  mpv_free(rawPath);
  QCOMPARE(QFileInfo(QUrl(path).isLocalFile() ? QUrl(path).toLocalFile() : path).canonicalFilePath(), a);
  trace.data.insert("a_entry", qint64(observer.latestLoadedEntry()));
  QTRY_VERIFY_WITH_TIMEOUT(player.getPosition() > 500, 10000);
  const auto firstPosition = player.getPosition();
  const QImage first = view.grabWindow();
  QTRY_VERIFY_WITH_TIMEOUT(player.getPosition() > firstPosition + 200, 10000);
  trace.data.insert("a_position_first_ms", firstPosition);
  trace.data.insert("a_position_second_ms", player.getPosition());
  const QImage second = view.grabWindow();
  QVERIFY(visibleVideo(first) && visibleVideo(second) && first != second);
  QVERIFY(saveNewFrame(first, evidence + "/synchronous-a-1.png") &&
          saveNewFrame(second, evidence + "/synchronous-a-2.png"));
}

static void runSynchronousStop(QObject* root, QQuickView& view, PlayerComponent& player,
                               mpv_handle* primary, const QString& media, const QString& evidence)
{
  const QString owned = QFileInfo(qEnvironmentVariable("JELLYFIN_TEST_OWNED_ROOT")).canonicalFilePath();
  const QString evidencePath = QFileInfo(evidence).canonicalFilePath();
  const QString a = QFileInfo(media).canonicalFilePath();
  const QString b = QFileInfo(qEnvironmentVariable("RECOVERY_MEDIA_B")).canonicalFilePath();
  QVERIFY(!owned.isEmpty() && QFileInfo(evidencePath).isDir());
  QVERIFY(withinOwnedRoot(evidencePath, owned) && withinOwnedRoot(a, owned) && withinOwnedRoot(b, owned));
  QVERIFY(QFileInfo(a).isFile() && QFileInfo(b).isFile() && a != b);
  SynchronousStopTrace trace(evidencePath);
  QVERIFY(!QFileInfo::exists(trace.path));
  trace.data.insert("claimed_source_sha", qEnvironmentVariable("JELLYFIN_TEST_SOURCE_SHA"));
  trace.data.insert("media_a", a);
  trace.data.insert("media_b", b);
  trace.data.insert("executable_sha256", QString::fromLatin1(fileDigest(QCoreApplication::applicationFilePath())));
  trace.data.insert("media_a_sha256", QString::fromLatin1(fileDigest(a)));
  trace.data.insert("media_b_sha256", QString::fromLatin1(fileDigest(b)));
  QVERIFY(!trace.data.value("claimed_source_sha").toString().isEmpty());
  QVERIFY(!trace.data.value("executable_sha256").toString().isEmpty());
  SynchronousMpvEvents observer(primary);
  QVERIFY(observer.client);
  SynchronousMpvTrace mpvTrace(trace, observer);
  installDispatchPage(root, trace);
  QVERIFY(!QTest::currentTestFailed());
  provePlayingA(root, view, player, primary, a, evidence, observer, trace);
  QVERIFY(!QTest::currentTestFailed());
  observer.drain();
  QVERIFY(observer.healthy);
  const auto boundary = observer.events.size();
  const auto aEntry = observer.latestLoadedEntry();
  trace.data.insert("b_observer_boundary", qint64(boundary));
  QVERIFY(IntentTrace::reset());
  evaluateFixture(root, "fixturePlayBStop(" + jsonString(QUrl::fromLocalFile(b).toString()) + ")");
  QCOMPARE(root->property("result").toString(), QStringLiteral("sent"));
  waitPageSettled(root);
  QVERIFY(!QTest::currentTestFailed());
  QTRY_VERIFY_WITH_TIMEOUT(matchingDispatch(IntentTrace::snapshot()), 10000);
  QTRY_VERIFY_WITH_TIMEOUT(idle(primary), 10000);
  const auto count = IntentTrace::snapshot().count;
  QElapsedTimer stable;
  stable.start();
  while (stable.elapsed() < 750) {
    observer.drain();
    QVERIFY(observer.healthy && matchingDispatch(IntentTrace::snapshot()));
    QCOMPARE(IntentTrace::snapshot().count, count);
    QVERIFY(idle(primary));
    QTest::qWait(20);
  }
  int64_t position = 0;
  QCOMPARE(mpv_get_property(primary, "playlist-pos", MPV_FORMAT_INT64, &position), 0);
  QCOMPARE(position, int64_t(-1));
  waitPageSettled(root);
  QVERIFY(!QTest::currentTestFailed());
  const auto page = QJsonDocument::fromJson(root->property("result").toString().toUtf8()).object();
  QCOMPARE(page.value("actions").toArray(), QJsonArray({"playA", "playB", "stop"}));
  observer.drain();
  QVERIFY(observer.healthy && idle(primary) && matchingDispatch(IntentTrace::snapshot()));
  awaitStartedEntryTerminal(observer, primary, boundary, aEntry);
  QVERIFY(!QTest::currentTestFailed());
  trace.data.insert("post_boundary_terminal_identity", "settled or no new start");
  trace.data.insert("web_state", page);
  trace.data.insert("status", "assertions passed; teardown requires independent proof");
}
