#ifndef QT_RECOVERY_QUICK_P_H
#define QT_RECOVERY_QUICK_P_H
#include "QtRecoverySubmissionAbi.h"

// All operations execute on the window's render thread. Identities are never
// reused, including after context, node, and texture destruction.
uint64_t qtRecoveryIdentity();
bool qtRecoveryBegin(const QtRecoverySwapTicketV1 &ticket);
void qtRecoveryFinish(bool endSucceeded, bool skipPresent);
void qtRecoveryAbort();
void qtRecoveryFboBegin(uint32_t fbo, uint64_t nodeLifetime);
void qtRecoveryFboEnd(uint32_t displayTexture, uint64_t textureGeneration);
void qtRecoveryFboAbort();
void qtRecoveryRegisterNode(const void *node, uint64_t lifetime);
void qtRecoveryUnregisterNode(const void *node, uint64_t lifetime);
uint64_t qtRecoveryNodeIdentity(const void *node);
void qtRecoveryMarkDraw(uint64_t nodeLifetime, uintptr_t renderTarget, bool normalColorPass);

#endif
