#pragma once

#include <QtGlobal>
#include <mutex>
#include <memory>
#include "RecoveryFrameObservation.h"
#include "QtSubmissionBridge.h"

namespace RendererRuntime { struct GenerationOwner; }

// Shared only between the item and its renderer; all fields are mutex protected.
struct RecoveryExchange {
    std::mutex mutex;
    quint64 generation = 0;
    std::weak_ptr<RendererRuntime::GenerationOwner> owner;
    quint64 requestGeneration = 0;
    quint64 requestId = 0;
    qint64 deadlineMs = 0;
    quint64 resultGeneration = 0;
    quint64 resultId = 0;
    bool resultUsable = false;
    bool resultPending = false;
    bool ready = false;
    bool contextFailurePending = false;
    bool framePending = false;
    quint64 frameGeneration = 0;
    RecoveryFrameObservationV1 frame{};
    quint64 sourceFrameSerial = 0;
    quint64 attempt = 0;
    qint64 attemptDeadlineMs = 0;
    QtRecoverySwapTicketV1 ticket{};
    QtRecoveryFrameV1 ticketFrame{};
    bool ticketPending = false;
    bool submissionPending = false;
    QtRecoverySubmissionV1 submission{};
    bool bootstrapSupported = false;
};
