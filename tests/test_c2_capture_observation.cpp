#include <QtTest>
#include <QJsonDocument>
#include <limits>
#include "c2_replay_recipe.h"

class TestC2CaptureObservation : public QObject
{
  Q_OBJECT
private slots:
  void windowsPaths();
  void posixPaths();
  void capturePredicates();
};

void TestC2CaptureObservation::windowsPaths()
{
  using P = C2PathPlatform;
  const QString expected = QString::fromUtf8("C:/media/é 100% #/clip.y4m");
  QString native = expected;
  native.replace('/', '\\');
  const QUrl oldUrl(native);
  QVERIFY((oldUrl.isLocalFile() ? oldUrl.toLocalFile() : native) != expected);
  QVERIFY(c2SamePath(native, expected, P::Windows));
  QVERIFY(c2SamePath(QString::fromUtf8("C:\\media/é 100% #\\clip.y4m"), expected, P::Windows));
  const QString encoded = QString::fromUtf8("file:///C:/media/%C3%A9%20100%25%20%23/clip.y4m");
  QVERIFY(c2SamePath(encoded, expected, P::Windows));
  QVERIFY(c2SamePath("file:///C:/x/%252F.y4m", "C:/x/%2F.y4m", P::Windows));
  QVERIFY(!c2SamePath("file:///C:/x/%252F.y4m", "C:/x//.y4m", P::Windows));
  QVERIFY(!c2SamePath("C:/x/a", "C:/x/b", P::Windows));
  QVERIFY(!c2SamePath("C:/x/a", "C:/y/a", P::Windows));
  QVERIFY(!c2SamePath("C:/x/a", "D:/x/a", P::Windows));
  QVERIFY(!c2SamePath("c:/x/a", "C:/x/a", P::Windows));
  for (const auto& text : {QStringLiteral("file://user@/C:/x/a"), QStringLiteral("file://:80/C:/x/a")}) {
    const QUrl url = QUrl::fromEncoded(text.toUtf8(), QUrl::StrictMode);
    QVERIFY(url.isValid());
    QVERIFY(url.isLocalFile());
    QVERIFY(url.host().isEmpty());
    QVERIFY(!url.authority().isEmpty());
  }
  for (const QString& bad : {QStringLiteral("C:relative"), QStringLiteral("/rooted"),
       QStringLiteral("\\\\server\\share\\a"), QStringLiteral("\\\\?\\C:\\a"),
       QStringLiteral("//./C:/a"), QStringLiteral("file://server/share/a"),
       QStringLiteral("https://example/a"), QStringLiteral("C:/x/http://a"),
       QStringLiteral("file:///C:/x/%00a"), QStringLiteral("file://user@/C:/x/a"),
       QStringLiteral("file://:80/C:/x/a")})
    QVERIFY(!c2PathValue(bad, P::Windows).valid);
#ifdef Q_OS_WIN
  QVERIFY(c2SamePath(QUrl::fromLocalFile(expected).toString(QUrl::FullyEncoded), native, P::Windows));
#endif
}

void TestC2CaptureObservation::posixPaths()
{
  using P = C2PathPlatform;
  const QString expected = QString::fromUtf8("/tmp/é 100% #/a\\b");
  QVERIFY(c2SamePath(expected, expected, P::Posix));
  QVERIFY(c2SamePath(QString::fromUtf8("file:///tmp/%C3%A9%20100%25%20%23/a%5Cb"), expected, P::Posix));
#ifndef Q_OS_WIN
  QVERIFY(c2SamePath(QUrl::fromLocalFile(expected).toString(QUrl::FullyEncoded), expected, P::Posix));
#endif
  QVERIFY(!c2SamePath("/tmp/a\\b", "/tmp/a/b", P::Posix));
  QVERIFY(!c2SamePath("/tmp/A", "/tmp/a", P::Posix));
  for (const QString& bad : {QStringLiteral("relative"), QStringLiteral("//server/file"),
       QStringLiteral("file://remote/tmp/file"), QStringLiteral("https://example/file")})
    QVERIFY(!c2PathValue(bad, P::Posix).valid);
}

void TestC2CaptureObservation::capturePredicates()
{
  C2CaptureObservation value;
  value.platform = C2PathPlatform::Posix;
  value.rawPath = "/tmp/a"; value.expectedPath = "/tmp/a"; value.pathPresent = true;
  value.pathStatus = 0; value.timeStatus = 0; value.pauseStatus = 0; value.entry = 42;
  value.position = 2.0; value.paused = 0;
  C2ReplayRecipe recipe;
  QVERIFY(recipe.captureObservation(value, C2ReplayRecipe::Intent::Playing));
  const auto checkRejected = [&](const C2CaptureObservation& bad) {
    QVERIFY(!bad.accepted());
    QVERIFY(!recipe.captureObservation(bad, C2ReplayRecipe::Intent::Paused));
    QCOMPARE(recipe.file, QByteArray("/tmp/a"));
    QCOMPARE(recipe.position, 2.0);
    QCOMPARE(recipe.entryId, qint64(42));
    QCOMPARE(recipe.intent, C2ReplayRecipe::Intent::Playing);
    QVERIFY(!recipe.wasPaused);
  };
  auto bad = value; bad.pathStatus = -1; checkRejected(bad);
  bad = value; bad.pathPresent = false; checkRejected(bad);
  bad = value; bad.pathTruncated = true; checkRejected(bad);
  bad = value; bad.entry = 0; checkRejected(bad);
  bad = value; bad.timeStatus = -1; checkRejected(bad);
  QCOMPARE(bad.record().value("time_class").toString(), QStringLiteral("unavailable"));
  QVERIFY(bad.record().value("time_value").isNull());
  bad = value; bad.pauseStatus = -1; checkRejected(bad);
  QVERIFY(bad.record().value("pause_value").isNull());
  bad = value; bad.position = std::numeric_limits<double>::quiet_NaN(); checkRejected(bad);
  bad = value; bad.position = std::numeric_limits<double>::infinity(); checkRejected(bad);
  bad = value; bad.rawPath = "/tmp/b"; checkRejected(bad);
  QVERIFY(!bad.record().value("accepted").toBool());
  QVERIFY(!bad.record().value("failed_predicates").toArray().isEmpty());
  bad = value; bad.rawPath = QString::fromUtf8("/tmp/é\n") + QString(5000, 'x'); bad.pathTruncated = true;
  const auto encoded = QJsonDocument(bad.record()).toJson(QJsonDocument::Compact);
  const auto restored = QJsonDocument::fromJson(encoded).object();
  QCOMPARE(restored.value("raw_path").toString().size(), 4096);
  QVERIFY(restored.value("raw_path_truncated").toBool());
  QVERIFY(restored.value("raw_path").toString().startsWith(QString::fromUtf8("/tmp/é\n")));
}

QTEST_GUILESS_MAIN(TestC2CaptureObservation)
#include "test_c2_capture_observation.moc"
