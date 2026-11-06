#include "player/PlayerComponent.h"
#include <MpvAbstractItem>
#include <MpvController>
#include <type_traits>

static_assert(std::is_constructible_v<MpvAbstractItem, MpvController::EventMode, QQuickItem*>,
              "The copied external-events item constructor must remain public");

// Link the copied Player metaobject but keep native restart execution held.
int main()
{
  (void)PlayerComponent::staticMetaObject.className();
  return 78;
}
