#pragma once
#include "module_identity_win.h"
#include <functional>
#include <memory>
#include <utility>

namespace HealthyModuleIdentity {
// One pre-item attempt. Success is an observation only; owned channel/lease
// binding and actual call-site equivalence are still required for authority.
class ImportPreparation {
public:
  ImportPreparation() = default;
#ifdef HEALTHY_MODULE_TESTING
  struct TestOps {
    std::function<long(const char*)> resolve;
    std::function<bool()> inspect;
  };
  explicit ImportPreparation(TestOps operations) : test_(std::move(operations)), testing_(true) {}
#endif
  bool initialize();
  bool attempted() const noexcept { return attempted_; }
  const ImportedModule* observation() const noexcept { return observed_.get(); }
private:
  friend class ChildModuleGate;
  std::unique_ptr<ImportedModule> takeObservation() { return std::move(observed_); }
  bool attempted_ = false;
  std::unique_ptr<ImportedModule> observed_;
#ifdef HEALTHY_MODULE_TESTING
  TestOps test_;
  bool testing_ = false;
#endif
};
}
