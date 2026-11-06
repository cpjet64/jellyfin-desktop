#include "graph.h"
#include "closure.h"
#include "closure_native.h"
#include "graph_retirement.h"
#include "player/MpvVideoItem.h"
#include <QThread>

namespace HealthyRetirement {
bool FixtureGraph::eventWorkerClosed() const
{
  return retirement_ && retirement_->closed();
}

bool FixtureGraph::approveDestruction()
{
  return eventWorkerClosed() && native_ && native_->state && item_ &&
         item_->thread() == QThread::currentThread() &&
         item_->healthyFinalizeClosed(*retirement_->closure(), *native_->state);
}
}
