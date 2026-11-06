#pragma once
#include "player/PlayerComponent.h"
#include "player_test_cache.h"
#include <QtTest/QtTest>
#ifdef HEALTHY_COPIED_SOURCE_TEST
#include <RestartBroker.h>
#include <memory>
#endif

class TestPlayerNativeRestore : public QObject
{
  Q_OBJECT
  PlayerTestCache cache_;
private:
  static void prepare(PlayerComponent& player);
  static void observationReply(PlayerComponent& player, quint64 id, const QByteArray& text, int error = 0);
  static void loadReplyAndStart(PlayerComponent& player, quint64 attempt, qint64 entry);
#ifdef HEALTHY_COPIED_SOURCE_TEST
  static std::shared_ptr<FixtureRestart::ChildGate> bindCleanup(PlayerComponent& player);
  static bool beginCleanup(PlayerComponent& player, const std::shared_ptr<FixtureRestart::ChildGate>& gate);
  static void completeNoLoad(PlayerComponent& player);
  static void completeStopped(PlayerComponent& player, qint64 entry);
#endif
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
