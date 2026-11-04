#include <QtTest>
#include <limits>
#include "c2_fault_restore_frame.h"

class TestC2FaultRestoreFrame : public QObject
{
  Q_OBJECT
private slots:
  void counter();
};

void TestC2FaultRestoreFrame::counter()
{
  QImage image(640, 360, QImage::Format_RGB32);
  image.fill(Qt::black);
  const int index = 61;
  for (int bit = 0; bit < 9; ++bit) {
    const int x = (11 + bit * 16) * image.width() / 160;
    const int y = 14 * image.height() / 90;
    image.setPixelColor(x, y, index & (1 << bit) ? Qt::white : Qt::black);
  }
  QCOMPARE(c2RestoreFrameIndex(image), index);
  QVERIFY(c2RestoreFrameMatches(image, 2.0));
  QVERIFY(!c2RestoreFrameMatches(image, 3.0));
  QVERIFY(!c2RestorePositionMatches(std::numeric_limits<double>::quiet_NaN(), 2.0));
  QVERIFY(!c2RestorePositionMatches(2.0, std::numeric_limits<double>::infinity()));
  QVERIFY(c2RestorePositionMatches(2.099, 2.0));
  QVERIFY(!c2RestorePositionMatches(2.101, 2.0));
  image.setPixelColor(11 * 4, 14 * 4, QColor(145, 145, 145));
  QCOMPARE(c2RestoreFrameIndex(image), -1);
  QVERIFY(!c2RestoreFrameMatches(QImage(), 2.0));
}

QTEST_GUILESS_MAIN(TestC2FaultRestoreFrame)
#include "test_c2_fault_restore_frame.moc"
