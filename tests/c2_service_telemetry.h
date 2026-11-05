#pragma once

#include "c2/SyntheticFrameService.h"
#include "c2/DiagnosticFailureInjection.h"
#include <QJsonObject>

inline QJsonObject c2ServiceTelemetry(const SyntheticFrameService::Telemetry& value)
{
  QJsonObject activation{{"evaluated", value.activationEvaluated}};
  if (value.activationEvaluated) {
    activation.insert("generation", qint64(value.activationGeneration));
    activation.insert("original_context", value.activationOriginal);
    activation.insert("synthetic", value.activationSynthetic);
    activation.insert("error", value.activationError);
    activation.insert("time_ns", qint64(value.activationNow));
    activation.insert("deadline_ns", qint64(value.deadline));
    activation.insert("result", value.activationResult);
  }
  return {{"sampling", "mutex_coherent"}, {"activation", activation},
    {"allow_count", qint64(value.allowCount)},
    {"first_rejection", int(value.firstRejection)}, {"last_rejection", int(value.lastRejection)}};
}

inline QJsonObject c2ServiceAtomicTelemetry()
{
  using D = DiagnosticFailureInjection;
  return {{"sampling", "sequential_not_coherent"},
    {"entry_count", D::serviceEntryCount.load()},
    {"entry_first_ns", qint64(D::serviceEntryFirstNs.load())},
    {"entry_last_ns", qint64(D::serviceEntryLastNs.load())},
    {"entry_first_generation", qint64(D::serviceEntryFirstGeneration.load())},
    {"entry_last_generation", qint64(D::serviceEntryLastGeneration.load())},
    {"entry_first_thread", QString::number(D::serviceEntryFirstThread.load(), 16)},
    {"entry_last_thread", QString::number(D::serviceEntryLastThread.load(), 16)},
    {"update_count", D::serviceUpdateCount.load()},
    {"update_first_mask", QString::number(D::serviceUpdateFirstMask.load())},
    {"update_last_mask", QString::number(D::serviceUpdateLastMask.load())},
    {"frame_count", D::serviceFrameCount.load()}, {"no_frame_count", D::serviceNoFrameCount.load()},
    {"skip_count", D::serviceSkipCount.load()}, {"skip_result", D::serviceResult.load()},
    {"skip_entered_ns", qint64(D::serviceSkipEnteredNs.load())},
    {"skip_returned_ns", qint64(D::serviceSkipReturnedNs.load())}};
}
