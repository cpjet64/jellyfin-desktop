#include "module_identity_win.h"
#include "module_identity_file.h"
#include <utility>
#ifdef _WIN32
#include <windows.h>
#include <vector>
struct mpv_render_context;
struct mpv_handle;
using UpdateCallback = void (*)(void*);
using UpdateFunction = void (*)(mpv_render_context*, UpdateCallback, void*);
using WakeFunction = void (*)(mpv_handle*, UpdateCallback, void*);
extern "C" UpdateFunction __imp_mpv_render_context_set_update_callback;
extern "C" WakeFunction __imp_mpv_set_wakeup_callback;
#endif

namespace HealthyModuleIdentity {
struct ImportedModule::State {
  FileObservation file;
  std::uintptr_t slot = 0, first = 0, second = 0, module = 0, executable = 0;
  std::uintptr_t wakeFirst = 0;
#ifdef _WIN32
  HMODULE handle = nullptr;
  ~State() { if (handle) FreeLibrary(handle); }
#endif
};
ImportedModule::ImportedModule(std::unique_ptr<State> state) : state_(std::move(state)) {}
ImportedModule::~ImportedModule() = default;
const FileObservation& ImportedModule::observation() const noexcept { return state_->file; }
std::uintptr_t ImportedModule::slot() const noexcept { return state_->slot; }
std::uintptr_t ImportedModule::firstTarget() const noexcept { return state_->first; }
std::uintptr_t ImportedModule::secondTarget() const noexcept { return state_->second; }
std::uintptr_t ImportedModule::module() const noexcept { return state_->module; }
std::uintptr_t ImportedModule::executable() const noexcept { return state_->executable; }
bool ImportedModule::stillBound() const noexcept
{
#ifdef _WIN32
  const volatile auto* slot = &__imp_mpv_render_context_set_update_callback;
  const volatile auto* wake = &__imp_mpv_set_wakeup_callback;
  return state_->handle && state_->slot == reinterpret_cast<std::uintptr_t>(slot) &&
         state_->first && state_->first == reinterpret_cast<std::uintptr_t>(*slot) &&
         state_->wakeFirst && state_->wakeFirst == reinterpret_cast<std::uintptr_t>(*wake);
#else
  return false;
#endif
}

std::unique_ptr<ImportedModule> ImportedModule::inspect(NativeError& error)
{
#ifdef _WIN32
  try {
    error = NativeError::Import;
    auto state = std::make_unique<State>();
    const auto* slot = &__imp_mpv_render_context_set_update_callback;
    const volatile auto* observedSlot = slot;
    state->slot = reinterpret_cast<std::uintptr_t>(slot);
    const auto first = *observedSlot;
    state->first = reinterpret_cast<std::uintptr_t>(first);
    if (!first) return {};
    error = NativeError::Module;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, reinterpret_cast<LPCWSTR>(first), &state->handle)) return {};
    state->module = reinterpret_cast<std::uintptr_t>(state->handle);
    state->executable = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    if (!state->executable || state->module == state->executable) return {};
    const volatile auto* wakeSlot = &__imp_mpv_set_wakeup_callback;
    const auto wake = *wakeSlot;
    state->wakeFirst = reinterpret_cast<std::uintptr_t>(wake);
    HMODULE wakeModule = nullptr;
    if (!wake || !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                                     reinterpret_cast<LPCWSTR>(wake), &wakeModule)) return {};
    const bool sameModule = wakeModule == state->handle;
    FreeLibrary(wakeModule);
    if (!sameModule) return {};
    std::vector<wchar_t> path(32768);
    const DWORD length = GetModuleFileNameW(state->handle, path.data(), DWORD(path.size()));
    if (!length || length >= path.size()) { error = NativeError::Path; return {}; }
    NativeDetail::FileHandle file;
    const StageApi api;
    file.value = CreateFileW(path.data(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                             FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (file.value == INVALID_HANDLE_VALUE) { error = NativeError::Open; return {}; }
    const bool valid = NativeDetail::regularNonReparse(file.value, false, api) &&
        NativeDetail::fileInfo(file.value, state->file, error, api) && NativeDetail::hashFile(file.value, state->file, error, api);
    if (!valid) return {};
    state->second = reinterpret_cast<std::uintptr_t>(*observedSlot);
    if (state->second != state->first || state->wakeFirst != reinterpret_cast<std::uintptr_t>(*wakeSlot)) {
      error = NativeError::Import; return {};
    }
    error = NativeError::None;
    return std::unique_ptr<ImportedModule>(new ImportedModule(std::move(state)));
  } catch (...) { error = NativeError::Unavailable; return {}; }
#else
  error = NativeError::Unsupported; return {};
#endif
}
}
