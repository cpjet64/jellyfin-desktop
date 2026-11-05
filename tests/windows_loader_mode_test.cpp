#include "../src/player/WindowsLoaderMode.h"
#include <cstdlib>
#include <initializer_list>
#include <stdexcept>
#define CHECK(value) do { if (!(value)) std::abort(); } while (false)

struct FakeApi
{
  using Handle = void *;
  unsigned long current = 0;
  unsigned long error = 0;
  unsigned long loadedError = 71;
  unsigned long restoreError = 81;
  int setCalls = 0;
  int loadCalls = 0;
  bool failEnter = false;
  bool failRestore = false;
  bool failLoad = false;
  bool throwLoad = false;
  const wchar_t *seenPath = nullptr;
  unsigned long readOverride = ~0ul;
  unsigned long mode() const { return readOverride == ~0ul ? current : readOverride; }
  unsigned long failCriticalErrors() const { return 1; }
  unsigned long lastError() const { return error; }
  bool setMode(unsigned long value, unsigned long *previous)
  {
    ++setCalls;
    if ((previous && failEnter) || (!previous && failRestore)) {
      error = previous ? 61 : restoreError;
      return false;
    }
    if (previous) *previous = current;
    current = value;
    error = 999;
    return true;
  }
  Handle load(const wchar_t *path)
  {
    ++loadCalls;
    seenPath = path;
    CHECK((current & 1) != 0);
    if (throwLoad) throw std::runtime_error("injected");
    error = loadedError;
    return failLoad ? nullptr : this;
  }
};

static void normalModes()
{
  for (unsigned long initial : {0ul, 2ul, 3ul}) {
    FakeApi api;
    api.current = initial;
    const wchar_t *path = L"fixture";
    auto result = loadWithThreadErrorMode(api, path);
    CHECK(result.entered && result.restored && result.module == &api && result.error == 0);
    CHECK(api.current == initial && api.loadCalls == 1 && api.setCalls == 2 && api.seenPath == path);
  }
  FakeApi changed;
  changed.current = 4;
  changed.readOverride = 2;
  auto result = loadWithThreadErrorMode(changed, L"fixture");
  CHECK(result.restored && changed.current == 4 && changed.setCalls == 2);
}

static void failureModes()
{
  FakeApi entry;
  entry.failEnter = true;
  auto failedEntry = loadWithThreadErrorMode(entry, L"fixture");
  CHECK(!failedEntry.entered && failedEntry.error == 61 && entry.loadCalls == 0 && entry.setCalls == 1);
  for (bool failLoad : {false, true}) {
    FakeApi api;
    api.failLoad = failLoad;
    api.failRestore = true;
    auto result = loadWithThreadErrorMode(api, L"fixture");
    CHECK(result.entered && !result.restored && result.restoreError == 81);
    CHECK(result.error == (failLoad ? 71ul : 0ul));
    CHECK((result.module == nullptr) == failLoad && api.loadCalls == 1 && api.setCalls == 2);
  }
  FakeApi load;
  load.failLoad = true;
  auto failedLoad = loadWithThreadErrorMode(load, L"fixture");
  CHECK(failedLoad.restored && failedLoad.error == 71 && load.setCalls == 2);
}

static void unwindRestores()
{
  FakeApi api;
  api.current = 2;
  api.throwLoad = true;
  try { loadWithThreadErrorMode(api, L"fixture"); CHECK(false); }
  catch (const std::runtime_error &) {}
  CHECK(api.current == 2 && api.loadCalls == 1 && api.setCalls == 2);
}

int main()
{
  normalModes();
  failureModes();
  unwindRestores();
}
