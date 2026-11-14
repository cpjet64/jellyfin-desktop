#include "player/AutomaticRecoveryFrameGate.h"
#include <cstdio>
#include <cstdlib>
#include <limits>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #x); std::abort(); } } while (false)

using Gate = AutomaticRecoveryFrameGate;
static constexpr Gate::Key key{1, 2, 3};

static QtRecoverySubmissionV1 submission()
{
  QtRecoverySubmissionV1 s{};
  s.size = sizeof(s); s.version = QT_RECOVERY_SUBMISSION_ABI_VERSION;
  s.ticket.size = sizeof(s.ticket); s.ticket.version = QT_RECOVERY_SWAP_ABI_VERSION;
  s.ticket.window_lifetime = 31; s.ticket.scenegraph_generation = 32; s.ticket.frame_serial = 33;
  s.ticket.render_thread = 1; s.ticket.swapchain = 2; s.ticket.render_target = 3;
  s.ticket.surface = 4; s.ticket.platform_context = 5;
  s.frame.size = sizeof(s.frame); s.frame.version = QT_RECOVERY_SUBMISSION_ABI_VERSION;
  s.frame.attempt = 4; s.frame.renderer_generation = 30; s.frame.entry_id = 7;
  s.frame.discontinuity_id = 8; s.frame.source_frame_serial = 11; s.frame.render_serial = 21;
  s.frame.media_pts = 10.0; s.frame.actual_render_fbo = 6;
  s.node_lifetime = 7; s.texture_generation = 8; s.fbo_content_serial = 9; s.display_texture = 10;
  return s;
}

static bool begin(Gate& gate)
{ return gate.begin(key, 4, 7, 10000, 9, 10, 30, 31, 32, 0, 15000); }

static void atomicSubmissionNeedsFreshPausedPosition()
{
  Gate gate; auto s = submission();
  CHECK(begin(gate));
  CHECK(Gate::validSubmission(s));
  CHECK(gate.completeSubmission(key, 4, 9, s, 1));
  CHECK(!gate.ready(key, 4, 9, 1));
  CHECK(gate.position(key, 4, 7, 10000, true, 9, 2));
  CHECK(gate.ready(key, 4, 9, 2));
  CHECK(!gate.ready(key, 4, 9, 2002));
  CHECK(!gate.ready(key, 4, 10, 3));
}

static void wrongNativeIdentityOrPtsCannotComplete()
{
  for (int failure = 0; failure != 6; ++failure) {
    Gate gate; auto s = submission(); CHECK(begin(gate));
    if (failure == 0) s.frame.entry_id = 6;
    if (failure == 1) s.frame.discontinuity_id = 0;
    if (failure == 2) s.frame.source_frame_serial = 10;
    if (failure == 3) s.frame.render_serial = 0;
    if (failure == 4) s.frame.media_pts = std::numeric_limits<double>::quiet_NaN();
    if (failure == 5) s.frame.media_pts = 12.001;
    CHECK(!gate.completeSubmission(key, 4, 9, s, 1));
    CHECK(!gate.ready(key, 4, 9, 2));
  }
}

static void firstDiscontinuityIsBoundToNativeFrame()
{
  Gate gate; auto s = submission(); CHECK(begin(gate));
  CHECK(gate.completeSubmission(key, 4, 9, s, 1));
  s.ticket.frame_serial++; s.frame.discontinuity_id++;
  s.frame.source_frame_serial++; s.frame.render_serial++;
  CHECK(!gate.completeSubmission(key, 4, 9, s, 2));
}

static void staleAuthorityAndWindowIdentityFail()
{
  for (int failure = 0; failure != 7; ++failure) {
    Gate gate; auto s = submission(); CHECK(begin(gate));
    auto current = key;
    if (failure == 0) s.frame.attempt = 5;
    if (failure == 1) s.frame.renderer_generation++;
    if (failure == 2) s.ticket.window_lifetime++;
    if (failure == 3) s.ticket.scenegraph_generation++;
    if (failure == 4) current.revision++;
    CHECK(!gate.completeSubmission(current, failure == 5 ? 0 : 4,
                                   failure == 6 ? 10 : 9, s, 1));
  }
  Gate deadline; auto s = submission(); CHECK(begin(deadline));
  CHECK(!deadline.completeSubmission(key, 4, 9, s, 15000));
}

static void malformedPrivatePacketCannotComplete()
{
  for (int failure = 0; failure != 9; ++failure) {
    Gate gate; auto s = submission(); CHECK(begin(gate));
    if (failure == 0) --s.size;
    if (failure == 1) ++s.version;
    if (failure == 2) s.reserved = 1;
    if (failure == 3) --s.ticket.size;
    if (failure == 4) ++s.ticket.version;
    if (failure == 5) --s.frame.size;
    if (failure == 6) ++s.frame.version;
    if (failure == 7) s.frame.reserved = 1;
    if (failure == 8) s.display_texture = 0;
    CHECK(!Gate::validSubmission(s));
    CHECK(!gate.completeSubmission(key, 4, 9, s, 1));
  }
}

static void positionAndTicketReplayBoundaries()
{
  Gate gate; auto s = submission(); CHECK(begin(gate));
  CHECK(gate.completeSubmission(key, 4, 9, s, 1));
  CHECK(!gate.completeSubmission(key, 4, 9, s, 2));
  CHECK(gate.position(key, 4, 7, 10000, true, 9, 3));
  CHECK(gate.ready(key, 4, 9, 3));

  Gate again; CHECK(begin(again));
  // A failed Qt ticket emits no completion; a later ticket can submit the
  // same valid post-seek source within the original attempt deadline.
  s.ticket.frame_serial++;
  CHECK(again.completeSubmission(key, 4, 9, s, 3));
  CHECK(again.position(key, 4, 7, 12000, true, 9, 4));
  CHECK(again.ready(key, 4, 9, 4));
  Gate unpaused; CHECK(begin(unpaused));
  CHECK(!unpaused.position(key, 4, 7, 10000, false, 9, 1));
  Gate tooFar; CHECK(begin(tooFar));
  CHECK(!tooFar.position(key, 4, 7, 12001, true, 9, 1));
}

int main()
{
  atomicSubmissionNeedsFreshPausedPosition();
  wrongNativeIdentityOrPtsCannotComplete();
  firstDiscontinuityIsBoundToNativeFrame();
  staleAuthorityAndWindowIdentityFail();
  malformedPrivatePacketCannotComplete();
  positionAndTicketReplayBoundaries();
}
