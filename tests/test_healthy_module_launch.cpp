#include "healthy_restart/module_channel.h"
#include "healthy_restart/module_import_prepare.h"
#include <cstdlib>

using HealthyModuleIdentity::VerifiedModule;
#define CHECK(x) do { if (!(x)) std::abort(); } while (false)

namespace {
bool valid(void* context) noexcept { return *static_cast<bool*>(context); }

void accessorUsesRetainedLaunch()
{
  bool live = true;
  auto module = VerifiedModule::forAccessorTest(7, 42, valid, &live);
  CHECK(module);
  std::uint64_t output = 999;
  CHECK(module->launchId(7, output) && output == 42);
  output = 999;
  CHECK(!module->launchId(8, output) && output == 999);
  CHECK(!module->launchId(0, output) && output == 999);
  live = false;
  CHECK(!module->launchId(7, output) && output == 999);
}

void invalidSeamCannotCreateModule()
{
  bool live = true;
  CHECK(!VerifiedModule::forAccessorTest(0, 42, valid, &live));
  CHECK(!VerifiedModule::forAccessorTest(7, 0, valid, &live));
  CHECK(!VerifiedModule::forAccessorTest(7, 42, nullptr, &live));
}

void nativeInspectionIsUnavailable()
{
  HealthyModuleIdentity::NativeError error = HealthyModuleIdentity::NativeError::None;
  CHECK(!HealthyModuleIdentity::ImportedModule::inspect(error));
  CHECK(error == HealthyModuleIdentity::NativeError::Unsupported);
  HealthyModuleIdentity::ImportPreparation preparation;
  CHECK(!preparation.initialize() && !preparation.observation());
}
}

int main()
{
  accessorUsesRetainedLaunch();
  invalidSeamCannotCreateModule();
  nativeInspectionIsUnavailable();
}
