#include "module_import_prepare.h"
#ifdef _WIN32
#include <windows.h>
#include <delayimp.h>
#endif

namespace HealthyModuleIdentity {
namespace {
long resolveExistingImport() noexcept
{
#if defined(_WIN32) && defined(_MSC_VER)
  __try { return __HrLoadAllImportsForDll("libmpv-2.dll"); }
  __except(EXCEPTION_EXECUTE_HANDLER) { return E_FAIL; }
#else
  return -1;
#endif
}
}

bool ImportPreparation::initialize()
{
  if (attempted_) return false;
  attempted_ = true;
  try {
#ifdef HEALTHY_MODULE_TESTING
    if (testing_) {
      if (!test_.resolve || !test_.inspect || test_.resolve("libmpv-2.dll") < 0) return false;
      return test_.inspect();
    }
#endif
    if (resolveExistingImport() < 0) return false;
    NativeError error = NativeError::Unavailable;
    observed_ = ImportedModule::inspect(error);
    return observed_ && error == NativeError::None;
  } catch (...) { return false; }
}
}
