#include "module_identity_win.h"
#include <utility>

#ifdef _WIN32
#include <windows.h>
#include <QCryptographicHash>
#include <QByteArray>
#include <algorithm>
#include <vector>

struct mpv_render_context;
using UpdateCallback = void (*)(void*);
using UpdateFunction = void (*)(mpv_render_context*, UpdateCallback, void*);
// MSVC's imported-call slot. A future copied-renderer graph audit must prove
// that this is the exact slot used by the call being qualified.
extern "C" UpdateFunction __imp_mpv_render_context_set_update_callback;
#endif

namespace HealthyModuleIdentity {
struct StageLease::State {
  FileObservation file;
#ifdef _WIN32
  HANDLE handle = INVALID_HANDLE_VALUE;
  ~State() { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }
#endif
};
struct ImportedModule::State {
  FileObservation file;
  std::uintptr_t slot = 0, first = 0, second = 0, module = 0, executable = 0;
#ifdef _WIN32
  HMODULE handle = nullptr;
  ~State() { if (handle) FreeLibrary(handle); }
#endif
};

StageLease::StageLease(std::unique_ptr<State> state) : state_(std::move(state)) {}
StageLease::~StageLease() = default;
const FileObservation& StageLease::observation() const noexcept { return state_->file; }
ImportedModule::ImportedModule(std::unique_ptr<State> state) : state_(std::move(state)) {}
ImportedModule::~ImportedModule() = default;
const FileObservation& ImportedModule::observation() const noexcept { return state_->file; }
std::uintptr_t ImportedModule::slot() const noexcept { return state_->slot; }
std::uintptr_t ImportedModule::firstTarget() const noexcept { return state_->first; }
std::uintptr_t ImportedModule::secondTarget() const noexcept { return state_->second; }
std::uintptr_t ImportedModule::module() const noexcept { return state_->module; }
std::uintptr_t ImportedModule::executable() const noexcept { return state_->executable; }

#ifdef _WIN32
namespace {
struct FileHandle {
  HANDLE value = INVALID_HANDLE_VALUE;
  ~FileHandle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};

bool canonical(HANDLE handle, std::wstring& out)
{
  std::vector<wchar_t> buffer(32768);
  const DWORD length = GetFinalPathNameByHandleW(handle, buffer.data(), DWORD(buffer.size()), FILE_NAME_NORMALIZED);
  if (!length || length >= buffer.size()) return false;
  out.assign(buffer.data(), length);
  return true;
}

bool regularNonReparse(HANDLE handle, bool directory)
{
  FILE_ATTRIBUTE_TAG_INFO info{};
  if (!GetFileInformationByHandleEx(handle, FileAttributeTagInfo, &info, sizeof(info))) return false;
  return !(info.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
         bool(info.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) == directory;
}

bool fileInfo(HANDLE handle, FileObservation& result, NativeError& error)
{
  FILE_ID_INFO id{};
  if (!GetFileInformationByHandleEx(handle, FileIdInfo, &id, sizeof(id))) { error = NativeError::Identity; return false; }
  if (!id.VolumeSerialNumber || std::all_of(std::begin(id.FileId.Identifier), std::end(id.FileId.Identifier),
                                            [](unsigned char value) { return value == 0; })) {
    error = NativeError::Identity; return false;
  }
  LARGE_INTEGER size{};
  if (!GetFileSizeEx(handle, &size) || size.QuadPart <= 0 || size.QuadPart > 512LL * 1024 * 1024) {
    error = NativeError::Size; return false;
  }
  std::wstring path;
  if (!canonical(handle, path)) { error = NativeError::Path; return false; }
  result.canonicalPath.assign(path.begin(), path.end());
  result.identity.volume = id.VolumeSerialNumber;
  std::copy(std::begin(id.FileId.Identifier), std::end(id.FileId.Identifier), result.identity.file.begin());
  result.size = std::uint64_t(size.QuadPart);
  result.pathComplete = true;
  result.identityComplete = true;
  return true;
}

bool hashFile(HANDLE handle, FileObservation& result, NativeError& error)
{
  LARGE_INTEGER zero{};
  if (!SetFilePointerEx(handle, zero, nullptr, FILE_BEGIN)) { error = NativeError::Read; return false; }
  QCryptographicHash hash(QCryptographicHash::Sha256);
  char buffer[65536];
  std::uint64_t total = 0;
  for (;;) {
    DWORD count = 0;
    if (!ReadFile(handle, buffer, DWORD(sizeof(buffer)), &count, nullptr)) { error = NativeError::Read; return false; }
    if (!count) break;
    total += count;
    if (total > result.size) { error = NativeError::Read; return false; }
    hash.addData(QByteArrayView(buffer, qsizetype(count)));
  }
  if (total != result.size) { error = NativeError::Read; return false; }
  const auto digest = hash.result();
  if (digest.size() != int(result.digest.size())) { error = NativeError::Digest; return false; }
  std::copy(digest.cbegin(), digest.cend(), result.digest.begin());
  if (result.digest != expectedDigest()) { error = NativeError::Digest; return false; }
  result.hashComplete = true;
  return true;
}
}
#endif

std::unique_ptr<StageLease> StageLease::acquire(std::uintptr_t directoryHandle, NativeError& error)
{
#ifdef _WIN32
  try {
  error = NativeError::InvalidDirectory;
  const auto directory = reinterpret_cast<HANDLE>(directoryHandle);
  if (!directory || directory == INVALID_HANDLE_VALUE || !regularNonReparse(directory, true)) return {};
  std::wstring parent;
  if (!canonical(directory, parent) || parent.empty()) { error = NativeError::Path; return {}; }
  if (parent.back() != L'\\') parent += L'\\';
  const std::wstring expected = parent + L"libmpv-2.dll";
  auto state = std::make_unique<State>();
  state->handle = CreateFileW(expected.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
  if (state->handle == INVALID_HANDLE_VALUE) { error = NativeError::Open; return {}; }
  if (!regularNonReparse(state->handle, false)) { error = NativeError::Reparse; return {}; }
  if (!fileInfo(state->handle, state->file, error)) return {};
  const std::wstring actual(state->file.canonicalPath.begin(), state->file.canonicalPath.end());
  if (_wcsicmp(actual.c_str(), expected.c_str()) != 0) { error = NativeError::Path; return {}; }
  if (!hashFile(state->handle, state->file, error)) return {};
  error = NativeError::None;
  return std::unique_ptr<StageLease>(new StageLease(std::move(state)));
  } catch (...) { error = NativeError::Unavailable; return {}; }
#else
  (void)directoryHandle; error = NativeError::Unsupported; return {};
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
  std::vector<wchar_t> path(32768);
  const DWORD length = GetModuleFileNameW(state->handle, path.data(), DWORD(path.size()));
  if (!length || length >= path.size()) { error = NativeError::Path; return {}; }
  FileHandle file;
  file.value = CreateFileW(path.data(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
  if (file.value == INVALID_HANDLE_VALUE) { error = NativeError::Open; return {}; }
  const bool valid = regularNonReparse(file.value, false) && fileInfo(file.value, state->file, error) && hashFile(file.value, state->file, error);
  if (!valid) return {};
  state->second = reinterpret_cast<std::uintptr_t>(*observedSlot);
  if (state->second != state->first) { error = NativeError::Import; return {}; }
  error = NativeError::None;
  return std::unique_ptr<ImportedModule>(new ImportedModule(std::move(state)));
  } catch (...) { error = NativeError::Unavailable; return {}; }
#else
  error = NativeError::Unsupported; return {};
#endif
}
}
