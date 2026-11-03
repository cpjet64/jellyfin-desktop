// Standalone native test: compile this translation unit without the production
// loader object so its internal selection seam stays private to the application.
#include "../src/player/WindowsMpvLoader.cpp"
#include <cassert>
#include <cstdint>

namespace {
int calls = 0;
bool selectedPrimary = false;
HMODULE success(bool primary)
{
  ++calls;
  selectedPrimary = primary;
  return reinterpret_cast<HMODULE>(static_cast<uintptr_t>(1));
}
HMODULE failure(bool)
{
  ++calls;
  return nullptr;
}
}

int main()
{
  assert(selectMpv(true, success) != nullptr);
  assert(calls == 1 && selectedPrimary);
  assert(selectMpv(false, success) != nullptr);
  assert(calls == 2 && !selectedPrimary);
  assert(selectMpv(false, failure) == nullptr);
  assert(calls == 3);
}
