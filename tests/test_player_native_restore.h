#pragma once
#include "player/PlayerComponent.h"
#include "player_test_cache.h"
#include <QtTest/QtTest>
#ifdef HEALTHY_COPIED_SOURCE_TEST
#include <RestartBroker.h>
#include <RestartChild.h>
#include <memory>
#endif

class TestPlayerNativeRestore : public QObject
{
  Q_OBJECT
#ifndef HEALTHY_COPIED_SOURCE_TEST
  PlayerTestCache cache_;
#endif
private:
  static void prepare(PlayerComponent& player);
  static void observationReply(PlayerComponent& player, quint64 id, const QByteArray& text, int error = 0);
  static void loadReplyAndStart(PlayerComponent& player, quint64 attempt, qint64 entry);
#ifdef HEALTHY_COPIED_SOURCE_TEST
  struct CleanupHarness {
    std::shared_ptr<FixtureRestart::ChildGate> gate;
    std::unique_ptr<FixtureRestart::RestartChild> child;
  };
  static std::unique_ptr<CleanupHarness> armCleanup(PlayerComponent& player, const PlayerComponent::RestoreGrant& grant);
  static bool beginCleanup(PlayerComponent& player, CleanupHarness& harness);
  static void completeNoLoad(PlayerComponent& player);
  static void completeStopped(PlayerComponent& player, qint64 entry);
  static void completeGraphStopped(PlayerComponent& player, qint64 entry);
  static void diagnosticPositionRejected(qint64 position);
#endif
private slots:
  void initTestCase();
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
#ifdef HEALTHY_COPIED_SOURCE_TEST
  void diagnosticPosition1000Rejected();
  void diagnosticPosition3125Rejected();
  void diagnosticReentrantObservation();
#endif
};
