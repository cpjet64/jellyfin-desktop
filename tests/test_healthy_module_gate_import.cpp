#include <healthy_restart/module_import_prepare.h>
#include <cstdlib>
#include <stdexcept>
#include <string>
using namespace HealthyModuleIdentity;
#define CHECK(x) do { if (!(x)) std::abort(); } while (false)

// Only the injected seam is run; this stub prevents a native module inspection.
namespace HealthyModuleIdentity {
struct ImportedModule::State {};
ImportedModule::~ImportedModule() = default;
std::unique_ptr<ImportedModule> ImportedModule::inspect(NativeError& error)
{ error = NativeError::Unavailable; return {}; }
}

static void successIsNotAuthority()
{
  int calls = 0;
  ImportPreparation p({[&](const char* name) -> long {
    CHECK(std::string(name) == "libmpv-2.dll"); CHECK(calls++ == 0); return 0;
  }, [&] { CHECK(calls++ == 1); return true; }});
  CHECK(!p.attempted() && !p.observation());
  CHECK(p.initialize() && p.attempted() && !p.observation());
  CHECK(!p.initialize() && calls == 2 && !p.observation());
}

static void failedAttemptsAreFinal()
{
  int inspections = 0;
  ImportPreparation error({[](const char*) -> long { return -1; }, [&] { ++inspections; return true; }});
  CHECK(!error.initialize() && error.attempted() && !error.initialize() && inspections == 0);
  ImportPreparation reject({[](const char*) -> long { return 0; }, [&] { ++inspections; return false; }});
  CHECK(!reject.initialize() && !reject.initialize() && inspections == 1 && !reject.observation());
  ImportPreparation thrown({[](const char*) -> long { throw std::runtime_error("failure"); }, [] { return true; }});
  CHECK(!thrown.initialize() && thrown.attempted() && !thrown.initialize() && !thrown.observation());
  ImportPreparation missing({{}, {}});
  CHECK(!missing.initialize() && missing.attempted() && !missing.initialize());
}

int main() { successIsNotAuthority(); failedAttemptsAreFinal(); }
