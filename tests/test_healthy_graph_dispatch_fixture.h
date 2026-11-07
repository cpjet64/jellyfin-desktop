#pragma once
#include "graph.h"
#include "graph_session.h"
#include "graph_access.h"
#include "capture_capability.h"
#include "RestartWire.h"
#include "frame_capture.h"
#include "fixture_restart/OwnedFixtureFile.h"
#include "player/PlayerComponent.h"
#include "player_test_cache.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QPainter>
#include <QTemporaryFile>
#include <QThread>
#include <chrono>
#include <algorithm>
#include <atomic>
#include <functional>
#include <limits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

using namespace HealthyRetirement;
using namespace FixtureRestart;
extern const char* dispatchCase;
#define DISPATCH_CHECK(value) do { if (!(value)) { std::fprintf(stderr, "%s: check failed at line %d: %s\n", dispatchCase, __LINE__, #value); std::_Exit(3); } } while (false)

class TestRetirementClaim {
public:
  static std::shared_ptr<RetirementClaim> reserve(std::uint64_t generation, RetirementClaim::ArmIdentity identity)
  {
    auto claim = RetirementClaim::makeArmReservation(generation);
    DISPATCH_CHECK(claim && claim->reserveArm(generation) && claim->bindArm(generation, identity));
    return claim;
  }
};

// Injected renderer boundary; no native renderer or GL context is created.
class TestCaptureCapability {
  using Bridge = CaptureBridge;
  static constexpr Identity identity{11, 12, 13};
  std::shared_ptr<Bridge> bridge_;
  std::optional<Bridge::Ticket> ticket_;
  static FramebufferObservation fbo() { return {7, 160, 90, 0, 0, 0, true, true}; }
  static QImage image(int index)
  {
    QImage result(160, 90, QImage::Format_RGBA8888); result.fill(Qt::black);
    QPainter painter(&result);
    for (int bit = 0; bit < 9; ++bit) {
      const int value = index & (1 << bit) ? 220 : 70;
      painter.fillRect(5 + bit * 16, 8, 12, 12, QColor(value, value, value));
    }
    painter.fillRect((index * 3) % 140, 35, 20, 20, QColor(220, 220, 220));
    painter.end();
    return result;
  }
public:
  explicit TestCaptureCapability(const std::shared_ptr<Slot>& slot) : bridge_(slot->captureBridge()) {}
  bool initialize() { return bridge_ && bridge_->rendererCreated(identity) && bridge_->framebuffer(identity, fbo()); }
  bool queue()
  {
    if (!bridge_->framebufferCurrent(identity, fbo())) return false;
    ticket_ = bridge_->issue(identity, fixture_restart::nextRenderSerial());
    return ticket_->request != 0;
  }
  void publish(int index)
  {
    DISPATCH_CHECK(ticket_);
    bridge_->publish(*ticket_, identity, image(index), 0, 0, 0, 0);
  }
  void replaceFramebuffer() { DISPATCH_CHECK(bridge_->framebuffer(identity, {8, 160, 90, 0, 0, 0, true, true})); }
  bool acknowledgeIndependentTarget(fixture_restart::FrameKey baseline, std::uint64_t attempt,
                                    const std::shared_ptr<const RetirementClaim>& claim)
  {
    if (!bridge_->request(baseline, attempt, Bridge::Phase::Baseline, claim) || !queue()) return false;
    auto target = baseline; target.phase = 2; target.seek = attempt;
    return bridge_->request(target, attempt, Bridge::Phase::Target, claim) && queue() &&
        bridge_->ready(attempt, Bridge::Phase::Target);
  }
};

class TestHealthyGraphDispatch {
  struct TokenStorage { int grant = 0, control = 0, ack = 0, rendererContext = 0; };
  struct Liveness {
    std::atomic_bool module{true}, endpoints{true};
    std::atomic<std::int64_t> clockOffset{0};
    std::atomic<qint64> clockNs{0};
    std::atomic_int loads{0}, seeks{0};
    std::atomic_bool pausedLoad{false};
    std::function<void()> moduleHook, endpointHook, clockHook, submissionHook;
    std::vector<char> callbacks, callbacksAtSubmission;
    std::size_t triggerAt = 0;
    std::function<void()> finalCallbackHook;
    bool trace = false, finalCallbackFired = false;
  };
  struct Fixture {
    PlayerTestCache cache;
    std::unique_ptr<PreparedGraph> prepared;
    FixtureGraph* graph = nullptr;
    std::shared_ptr<TokenStorage> tokens = std::make_shared<TokenStorage>();
    std::shared_ptr<Liveness> live = std::make_shared<Liveness>();
    std::unique_ptr<QTemporaryFile> media;
    std::unique_ptr<TestCaptureCapability> renderer;
    Grant grant;
    QByteArray digest;
    static std::int64_t now();
    static void recordCallback(const std::shared_ptr<Liveness>& state, char kind);
    void initialize(bool desiredPaused = false);
    void installRenderer();
    void acceptGrantAndPermit();
    void replyLoad(qint64 entry, bool startFirst = false, bool withStart = true);
    void observe(quint64 id, qint64 entry, const QByteArray& position);
    void qualifyBaseline();
    void armTarget();
    void completeSeekReplyAndRestart();
    bool drivePausedHold();
    void checkNoUnpause() const;
  };
  static void permitThenLostEndpointRejectsDirectLoad();
  static void permitThenLostEndpointRejectsReceiptEntry();
  static void moduleRevocationBeforeLoadAdmission();
  static void endpointRevocationBeforeLoadAdmission();
  static void claimRevocationBeforeLoadAdmission();
  static void clockCancellationBeforeLoadAdmission();
  static void clockRevokesEndpointBeforeLoadAdmission();
  static void lastLivenessCallbackRevokesLoadAuthority();
  static void lastLivenessCallbackRevokesClaimBeforeLoad();
  static void lastLivenessCallbackExpiresLoadDeadline();
  static void admittedLoadCannotRetry();
  static void cleanupRetainsOriginalOwnerAfterLoad();
  static void wrongOwnerRejectsDirectLoad();
  static void replyWithoutStartCannotCapture();
  static void originalDeadlineRejectsLateLoad();
  static void pendingLoadExpiresWithoutSuccess();
  static void bothGrantsLoadPaused();
  static void permitThenLostEndpointRejectsDirectSeek();
  static void revocationDuringSeekAuthorization();
  static void sameGenerationDifferentClaimRejectsSeek();
  static void clockInvalidatesTargetBeforeSeekAdmission();
  static void lastLivenessCallbackInvalidatesTarget();
  static void lastLivenessCallbackClosesSeekAdmission();
  static void targetAckRequiredBeforeSeek();
  static void staleTargetAckRejectsSeek();
  static void otherRendererAckCannotAuthorizeSeek();
  static void preSeekTargetCannotQualify();
  static void postSubmitTargetBeforeReplyCanQualify();
  static void restartBeforeSeekReplyCannotQualify();
  static void earlyObservationCannotQualify();
  static void wrongOwnerRejectsDirectSeek();
  static void admittedSeekCannotRetry();
  static void desiredPausedCompletesHold();
  static void retainedHoldCannotOutliveTargetQualification();
  static void cancelAfterTargetArmRejectsSeek();
  static void wrongEntryCannotQualifyBaseline();
  static void bootstrapRejectsMalformed();
public:
  static int variant;
  static bool runSelected(const QByteArray& selected);
  static void traceLoad();
  static void traceSeek();
  static void runLoadCases();
  static void runSeekCases();
};
