#pragma once

#include <functional>
#include "c2_error_terminal.h"

inline bool c2QuiescenceTerminalPredicate(int idleStatus, int idle, int voStatus, int forceStatus, bool forceNo,
    const std::function<bool()>& canceled, const std::function<bool()>& failures,
    const std::function<bool()>& binding, const std::function<bool()>& events)
{
  return c2ErrorTerminalProperties(idleStatus, idle, voStatus, forceStatus, forceNo) &&
    canceled() && failures() && binding() && events();
}
