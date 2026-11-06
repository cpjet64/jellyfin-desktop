#ifndef HEALTHY_MODULE_TESTING
#error This non-loading stub is restricted to the module accessor test.
#endif
#include "module_import_prepare.h"

namespace HealthyModuleIdentity {
struct ImportedModule::State {};
ImportedModule::~ImportedModule() = default;
std::unique_ptr<ImportedModule> ImportedModule::inspect(NativeError& error)
{ error = NativeError::Unsupported; return {}; }
const FileObservation& ImportedModule::observation() const noexcept
{ static const FileObservation empty; return empty; }
std::uintptr_t ImportedModule::slot() const noexcept { return 0; }
std::uintptr_t ImportedModule::firstTarget() const noexcept { return 0; }
std::uintptr_t ImportedModule::secondTarget() const noexcept { return 0; }
std::uintptr_t ImportedModule::module() const noexcept { return 0; }
std::uintptr_t ImportedModule::executable() const noexcept { return 0; }
bool ImportedModule::stillBound() const noexcept { return false; }
bool ImportPreparation::initialize() { return false; }
}
