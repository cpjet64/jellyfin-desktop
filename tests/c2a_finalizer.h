#pragma once

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QThread>
#include <cstdlib>
#include "c2/DiagnosticFailureInjection.h"
#include "mpv_thread_probe.h"

// A failed or ambiguous teardown remains contained in the fixture process.
class C2aFinalizer
{
public:
  enum class Phase { NoneAcquired, Live, Retired, Uncertain };
  explicit C2aFinalizer(QtThreaded::Scene& value) : scene(value) {}
  ~C2aFinalizer() { if (!attempted) finish(); if (!resourcesSettled) std::_Exit(EXIT_FAILURE); }
  void beginCreate(ProbeItem* value) { item = value; phase = Phase::Uncertain; }
  void confirmNoContext() { phase = Phase::NoneAcquired; }
  void confirmLive() { phase = Phase::Live; }
  void confirmRetired() { phase = Phase::Retired; }
  Phase currentPhase() const { return phase; }
  bool finish()
  {
    if (attempted) return resourcesSettled && valid;
    attempted = true;
    if (phase == Phase::Uncertain || phase == Phase::Live) return false;
    if (phase == Phase::NoneAcquired && (DiagnosticFailureInjection::frees.load() != 0 ||
                                         DiagnosticFailureInjection::ackOrder.load() != 0)) return false;
    QString error;
    if (!scene.shutdown(&error)) return false;
    resourcesSettled = true;
    const auto audit = scene.audit();
    if (!audit.workerJoined || !audit.contextDestroyed || !audit.surfaceDestroyed) return false;
    item = nullptr; // The scene owns and destroys the item with its window.
    valid = true;
    return valid;
  }
private:
  QtThreaded::Scene& scene;
  ProbeItem* item = nullptr;
  Phase phase = Phase::NoneAcquired;
  bool attempted = false;
  bool resourcesSettled = false;
  bool valid = false;
};
