#pragma once

#include "mpvcontroller.h"
#include "RendererOwners.h"
#include <functional>
#include <memory>

namespace MpvRecoveryBootstrap {
using Completion = std::function<bool(std::shared_ptr<RendererRuntime::CoreOwner>, quint64)>;
void start(quint64 reservation, quint64 revision, MpvController::EventMode mode, Completion completion);
}
