#pragma once
#include "player/PlayerComponent.h"
#include "player_test_cache.h"
#include <QtTest/QtTest>

class TestPlayerNativeRestore : public QObject
{
  Q_OBJECT
  PlayerTestCache cache_;
private:
  static void prepare(PlayerComponent& player);
  static void observationReply(PlayerComponent& player, quint64 id, const QByteArray& text, int error = 0);
  static void loadReplyAndStart(PlayerComponent& player, quint64 attempt, qint64 entry);
private slots:
  void actualObservationBoundary();
  void lateAndDuplicateObservation();
  void pausedLoadAndExactSeek();
  void cancellationBeforeLoad();
  void staleObservationAndGrant();
  void cancellationAfterLoad();
  void fixtureBridgeControl();
  void fixtureBridgeCapture();
  void unavailableSystemMode();
  void lateAfterEndAndCoreChange();
  void malformedAndUnrelatedReplies();
  void reorderedLoadAndNativeConsumption();
  void cancellationDuringSeek();
  void reentrantCancellationAndSeekFailure();
};
