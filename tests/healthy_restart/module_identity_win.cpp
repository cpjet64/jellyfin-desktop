#include "module_identity_win.h"
#include "module_identity_file.h"
#include <utility>

#ifdef _WIN32
#include <windows.h>
#include <QCryptographicHash>
#include <QByteArray>
#include <algorithm>
#include <vector>

#endif

namespace HealthyModuleIdentity {
struct StageLease::State {
  FileObservation file;
#ifdef _WIN32
  HANDLE handle = INVALID_HANDLE_VALUE;
  decltype(&CloseHandle) close = &CloseHandle;
  ~State() { if (handle != INVALID_HANDLE_VALUE) close(handle); }
#endif
};
StageLease::StageLease(std::unique_ptr<State> state) : state_(std::move(state)) {}
StageLease::~StageLease() = default;
const FileObservation& StageLease::observation() const noexcept { return state_->file; }

#ifdef _WIN32
namespace NativeDetail {
FileHandle::~FileHandle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }

bool canonical(HANDLE handle, std::wstring& out, const StageApi& api)
{
  std::vector<wchar_t> buffer(32768);
  const DWORD length = api.finalPath(handle, buffer.data(), DWORD(buffer.size()), FILE_NAME_NORMALIZED);
  if (!length || length >= buffer.size()) return false;
  out.assign(buffer.data(), length);
  return true;
}

bool regularNonReparse(HANDLE handle, bool directory, const StageApi& api)
{
  FILE_ATTRIBUTE_TAG_INFO info{};
  if (!api.information(handle, FileAttributeTagInfo, &info, sizeof(info))) return false;
  return !(info.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
         bool(info.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) == directory;
}

bool fileInfo(HANDLE handle, FileObservation& result, NativeError& error, const StageApi& api)
{
  FILE_ID_INFO id{};
  if (!api.information(handle, FileIdInfo, &id, sizeof(id))) { error = NativeError::Identity; return false; }
  if (!id.VolumeSerialNumber || std::all_of(std::begin(id.FileId.Identifier), std::end(id.FileId.Identifier),
                                            [](unsigned char value) { return value == 0; })) {
    error = NativeError::Identity; return false;
  }
  LARGE_INTEGER size{};
  if (!api.size(handle, &size) || size.QuadPart <= 0 || size.QuadPart > 512LL * 1024 * 1024) {
    error = NativeError::Size; return false;
  }
  std::wstring path;
  if (!canonical(handle, path, api)) { error = NativeError::Path; return false; }
  result.canonicalPath.assign(path.begin(), path.end());
  result.identity.volume = id.VolumeSerialNumber;
  std::copy(std::begin(id.FileId.Identifier), std::end(id.FileId.Identifier), result.identity.file.begin());
  result.size = std::uint64_t(size.QuadPart);
  result.pathComplete = true;
  result.identityComplete = true;
  return true;
}

bool hashFile(HANDLE handle, FileObservation& result, NativeError& error, const StageApi& api)
{
  LARGE_INTEGER zero{};
  if (!api.seek(handle, zero, nullptr, FILE_BEGIN)) { error = NativeError::Read; return false; }
  QCryptographicHash hash(QCryptographicHash::Sha256);
  char buffer[65536];
  std::uint64_t total = 0;
  for (;;) {
    DWORD count = 0;
    if (!api.read(handle, buffer, DWORD(sizeof(buffer)), &count, nullptr)) { error = NativeError::Read; return false; }
    if (!count) break;
    total += count;
    if (total > result.size) { error = NativeError::Read; return false; }
    hash.addData(QByteArrayView(buffer, qsizetype(count)));
  }
  if (total != result.size) { error = NativeError::Read; return false; }
  const auto digest = hash.result();
  if (digest.size() != int(result.digest.size())) { error = NativeError::Digest; return false; }
  std::copy(digest.cbegin(), digest.cend(), result.digest.begin());
#ifdef HEALTHY_MODULE_TESTING
  if (api.overrideDigest) result.digest = api.overrideDigest(result.digest);
#endif
  if (result.digest != expectedDigest()) { error = NativeError::Digest; return false; }
  result.hashComplete = true;
  return true;
}
}
#endif

std::unique_ptr<StageLease> StageLease::acquire(std::uintptr_t directoryHandle, NativeError& error)
{
#ifdef _WIN32
  return acquireImpl(directoryHandle, StageApi{}, error);
#else
  (void)directoryHandle; error = NativeError::Unsupported; return {};
#endif
}

#if defined(_WIN32) && defined(HEALTHY_MODULE_TESTING)
std::unique_ptr<StageLease> StageLease::acquireForTest(std::uintptr_t directoryHandle, const StageApi& api, NativeError& error)
{ return acquireImpl(directoryHandle, api, error); }
#endif

#ifdef _WIN32
std::unique_ptr<StageLease> StageLease::acquireImpl(std::uintptr_t directoryHandle, const StageApi& api, NativeError& error)
{
  try {
  error = NativeError::InvalidDirectory;
  const auto directory = reinterpret_cast<HANDLE>(directoryHandle);
  if (!api.open || !api.close || !api.information || !api.finalPath || !api.size || !api.seek || !api.read ||
      !directory || directory == INVALID_HANDLE_VALUE || !NativeDetail::regularNonReparse(directory, true, api)) return {};
  std::wstring parent;
  if (!NativeDetail::canonical(directory, parent, api) || parent.empty()) { error = NativeError::Path; return {}; }
  if (parent.back() != L'\\') parent += L'\\';
  const std::wstring expected = parent + L"libmpv-2.dll";
  auto state = std::make_unique<State>();
  state->close = api.close;
  state->handle = api.open(expected.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
  if (state->handle == INVALID_HANDLE_VALUE) { error = NativeError::Open; return {}; }
  if (!NativeDetail::regularNonReparse(state->handle, false, api)) { error = NativeError::Reparse; return {}; }
  if (!NativeDetail::fileInfo(state->handle, state->file, error, api)) return {};
  const std::wstring actual(state->file.canonicalPath.begin(), state->file.canonicalPath.end());
  if (_wcsicmp(actual.c_str(), expected.c_str()) != 0) { error = NativeError::Path; return {}; }
  if (!NativeDetail::hashFile(state->handle, state->file, error, api)) return {};
  error = NativeError::None;
  return std::unique_ptr<StageLease>(new StageLease(std::move(state)));
  } catch (...) { error = NativeError::Unavailable; return {}; }
}
#endif

}
