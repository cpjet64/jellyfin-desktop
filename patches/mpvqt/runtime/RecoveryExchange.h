#pragma once

#include <QtGlobal>
#include <mutex>
#include <memory>
#include <deque>
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
    quint64 readyGeneration = 0;
    bool contextFailurePending = false;
    quint64 contextFailureGeneration = 0;
    bool framePending = false;
    quint64 frameGeneration = 0;
    RecoveryFrameObservationV1 frame{};
    quint64 sourceFrameSerial = 0;
    quint64 attempt = 0;
    qint64 attemptDeadlineMs = 0;
    QtRecoverySwapTicketV1 ticket{};
    QtRecoveryFrameV1 ticketFrame{};
    bool ticketPending = false;
    quint64 lastQtFrameSerial = 0;
    std::deque<QtRecoverySubmissionV1> submissions;
    QtRecoverySubmissionV1 submission{};
    bool bootstrapSupported = false;

    // Caller holds mutex for these operations.
    bool owns(const std::shared_ptr<RendererRuntime::GenerationOwner> &candidate, quint64 id) const
    {
        return candidate && owner.lock() == candidate && generation == id;
    }
    void install(const std::shared_ptr<RendererRuntime::GenerationOwner> &candidate, quint64 id)
    {
        owner = candidate;
        generation = id;
        requestGeneration = requestId = resultGeneration = resultId = 0;
        deadlineMs = 0;
        resultUsable = resultPending = ready = contextFailurePending = framePending = false;
        readyGeneration = contextFailureGeneration = frameGeneration = 0;
        frame = {};
        sourceFrameSerial = attempt = lastQtFrameSerial = 0;
        attemptDeadlineMs = 0;
        ticket = {};
        ticketFrame = {};
        ticketPending = bootstrapSupported = false;
        submissions.clear();
        submission = {};
    }
    bool recordFailure(const std::shared_ptr<RendererRuntime::GenerationOwner> &candidate, quint64 id)
    {
        if (!owns(candidate, id)) return false;
        contextFailurePending = true;
        contextFailureGeneration = id;
        bootstrapSupported = false;
        return true;
    }
    bool recordUsability(const std::shared_ptr<RendererRuntime::GenerationOwner> &candidate,
                         quint64 id, bool usable)
    {
        if (!owns(candidate, id) || !requestId || requestGeneration != id) return false;
        resultId = requestId;
        resultGeneration = id;
        resultUsable = usable;
        resultPending = true;
        requestId = 0;
        return true;
    }
    template<typename Revoke>
    bool retireIfCurrent(const std::shared_ptr<RendererRuntime::GenerationOwner> &candidate,
                         quint64 id, Revoke revoke)
    {
        if (!owns(candidate, id)) return false;
        ticketPending = false;
        submissions.clear();
        revoke();
        return true;
    }
};
