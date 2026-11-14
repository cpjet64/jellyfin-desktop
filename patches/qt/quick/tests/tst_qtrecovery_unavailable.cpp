// Source-only negative tests for the first, deliberately disabled slice.
#include <QtTest/QtTest>
#include "../QtRecoverySubmissionAbi.h"

class RecoveryUnavailable : public QObject
{
    Q_OBJECT
private slots:
    void missingScopeRejectsPublication();
    void missingDrawRejectsCompletion();
    void revokeUnknownWindowIsHarmless();
    void malformedCallsAreRejected();
};

void RecoveryUnavailable::missingScopeRejectsPublication()
{
    QtRecoveryFrameV1 frame{};
    frame.size = sizeof(frame);
    frame.version = QT_RECOVERY_SUBMISSION_ABI_VERSION;
    frame.renderer_generation = frame.entry_id = frame.source_frame_serial = frame.render_serial = 1;
    frame.actual_render_fbo = 1;
    QtRecoverySwapTicketV1 ticket{};
    QCOMPARE(qt_recovery_publish_native_frame_v1(&frame, &ticket), 0);
}

void RecoveryUnavailable::missingDrawRejectsCompletion()
{
    QtRecoverySubmissionV1 result{};
    result.size = sizeof(result);
    result.version = QT_RECOVERY_SUBMISSION_ABI_VERSION;
    QCOMPARE(qt_recovery_take_submission_v1(1, 1, 1, &result), 0);
}

void RecoveryUnavailable::revokeUnknownWindowIsHarmless()
{
    qt_recovery_revoke_v1(1);
    QtRecoverySubmissionV1 result{};
    QCOMPARE(qt_recovery_take_submission_v1(1, 1, 1, &result), 0);
}

void RecoveryUnavailable::malformedCallsAreRejected()
{
    QtRecoveryFrameV1 frame{};
    QtRecoverySwapTicketV1 ticket{};
    QCOMPARE(qt_recovery_publish_native_frame_v1(nullptr, &ticket), 0);
    QCOMPARE(qt_recovery_publish_native_frame_v1(&frame, nullptr), 0);
    QtRecoverySubmissionV1 result{};
    QCOMPARE(qt_recovery_take_submission_v1(0, 0, 0, nullptr), 0);
    QCOMPARE(qt_recovery_take_submission_v1(0, 0, 0, &result), 0);
}

QTEST_APPLESS_MAIN(RecoveryUnavailable)
#include "tst_qtrecovery_unavailable.moc"
