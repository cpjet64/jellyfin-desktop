#pragma once

namespace HealthyRestart {
enum class FailureCause { None, ProtocolViolation, EndpointLost, InitializationFailure, OwnershipLost, Deadline };
}
