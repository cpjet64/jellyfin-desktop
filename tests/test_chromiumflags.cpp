#include <QtTest/QtTest>
#include "utils/ChromiumFlags.h"

class TestChromiumFlags : public QObject
{
  Q_OBJECT
private slots:
  void defaults();
  void preservesQuotedEnvironment();
  void explicitGpuDisable();
  void explicitChoicesTakePrecedence();
  void switchNamesAreMatchedExactly();
};

void TestChromiumFlags::defaults()
{
  QCOMPARE(ChromiumFlags::compose({}, false, true, false, false),
           QByteArray("--enable-gpu-rasterization --disable-features=MediaSessionService --disable-pinch"));
  QCOMPARE(ChromiumFlags::compose({}, false, false, false, true),
           QByteArray("--enable-gpu-rasterization --disable-features=MediaSessionService,HardwareMediaKeyHandling"));
}

void TestChromiumFlags::preservesQuotedEnvironment()
{
  const QByteArray original("  --user-agent=\"a b\"  --disable-features=\"Foo,Bar\" ");
  const QByteArray result = ChromiumFlags::compose(original, false, false, false, true);
  QVERIFY(result.startsWith(original));
  QCOMPARE(result, original + " --enable-gpu-rasterization");
}

void TestChromiumFlags::explicitGpuDisable()
{
  QCOMPARE(ChromiumFlags::compose({}, true, false, false, false),
           QByteArray("--disable-gpu --disable-features=MediaSessionService"));
  QCOMPARE(ChromiumFlags::compose("--disable-gpu", true, false, false, false),
           QByteArray("--disable-gpu --disable-features=MediaSessionService"));
}

void TestChromiumFlags::explicitChoicesTakePrecedence()
{
  const QByteArray original("--disable-gpu-rasterization --enable-pinch --disable-features=Custom");
  QCOMPARE(ChromiumFlags::compose(original, false, true, false, true), original);
  const QByteArray custom("--enable-gpu-rasterization=custom --disable-features=Custom");
  QCOMPARE(ChromiumFlags::compose(custom, false, false, false, false), custom);
}

void TestChromiumFlags::switchNamesAreMatchedExactly()
{
  QCOMPARE(ChromiumFlags::compose("--disable-gpu-extra", false, false, true, false),
           QByteArray("--disable-gpu-extra --enable-gpu-rasterization --disable-features=MediaSessionService --ignore-certificate-errors"));
}

QTEST_APPLESS_MAIN(TestChromiumFlags)
#include "test_chromiumflags.moc"
