#pragma once

#include "../../qt/runtime/QtRecoveryAbi.h"
#include "../../qt/quick/QtRecoverySubmissionAbi.h"

namespace QtSubmissionBridge {
class Resolver final {
public:
    Resolver();
    ~Resolver();
    Resolver(const Resolver &) = delete;
    Resolver &operator=(const Resolver &) = delete;
    bool available() const noexcept;
    bool publish(const QtRecoveryFrameV1 &frame, QtRecoverySwapTicketV1 &ticket) const;
    bool take(uint64_t window, uint64_t generation, uint64_t attempt, QtRecoverySubmissionV1 &out) const;
    bool takeUnsupported(uint64_t window, uint64_t generation, uint64_t sceneGeneration) const;
    void revoke(uint64_t window) const;
private:
    void *moduleLease_ = nullptr;
    int (*publish_)(const QtRecoveryFrameV1 *, QtRecoverySwapTicketV1 *) = nullptr;
    int (*take_)(uint64_t, uint64_t, uint64_t, QtRecoverySubmissionV1 *) = nullptr;
    void (*revoke_)(uint64_t) = nullptr;
    int (*unsupported_)(uint64_t, uint64_t, uint64_t) = nullptr;
};
}
