#include "player/PlayerComponent.h"

// Link the copied Player metaobject but keep native restart execution held.
int main()
{
  (void)PlayerComponent::staticMetaObject.className();
  return 78;
}
